#include "scheduling.hpp"

#include <dlfcn.h>
#include <cctype>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <deque>
#include <vector>
#include <fstream>
#include "basic-net.hpp"
#include "cpp-func.hpp"
#include "cpp-runner.hpp"
#include "cpp/cown.h"
#include "resource.hpp"
#include "rust/cxx.h"
#include "types.hpp"
#include "wasm-func.hpp"
#include "wasm-runner.hpp"
#include "thread-safe-queue.hpp"
#include "graph.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace detersl::worker {

std::unordered_map<std::string, detersl::types::FunctionType> all_functions;
std::unordered_map<std::string, std::pair<cown_ptr<detersl::types::Resource>, uint64_t>> resource_map;
BasicMPSCQueue<std::pair<std::string, uint64_t>> deleted_resources_queue;
std::unordered_map<int, detersl::func::WasmFuncInfo> wasm_func_registry;
int next_wasm_func_id = 1;
std::unordered_map<std::string, Node*> workflow_registry;
int next_workflow_request_id = 1;

static bool parse_resource_placeholder(const std::string& placeholder,
                                       bool &immediate_val,
                                       std::string& name,
                                       bool &read_only,
                                       std::string* err) {
                                        
  auto trim_copy = [](std::string input) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    input.erase(input.begin(), std::find_if(input.begin(), input.end(), not_space));
    input.erase(std::find_if(input.rbegin(), input.rend(), not_space).base(), input.end());
    return input;
  };
  
  const auto trimmed = trim_copy(placeholder);
  if (trimmed.size() < 2) {
    if (err) *err = "invalid resource placeholder: " + placeholder;
    return false;
  }
  const char lead = trimmed[0];
  if (lead != '$' && lead != '&') {
    if (err) *err = "unsupported placeholder scope: " + placeholder;
    return false;
  }
  size_t start = 1;
  if (start >= trimmed.size()) {
    if (err) *err = "empty resource key in placeholder: " + placeholder;
    return false;
  }

  if (lead == '$') {
    const std::string key = trimmed.substr(start);
    if (key.empty()) {
      if (err) *err = "empty value key in placeholder: " + placeholder;
      return false;
    }
    immediate_val = true;
    name = key;
    return true;
  }

  const auto colon_pos = trimmed.rfind(':');
  if (colon_pos == std::string::npos || colon_pos + 2 != trimmed.size()) {
    const std::string key = trimmed.substr(start);
    if (key.empty()) {
      if (err) *err = "empty value key in placeholder: " + placeholder;
      return false;
    }
    immediate_val = false;
    name = key;
    return true;
  }
  const char mode_char = trimmed[colon_pos + 1];
  if (mode_char != 'r' && mode_char != 'w') {
    if (err) *err = "unsupported access mode in placeholder: " + placeholder;
    return false;
  }
  const std::string key = trimmed.substr(start, colon_pos - start);
  if (key.empty()) {
    if (err) *err = "empty resource key in placeholder: " + placeholder;
    return false;
  }
  immediate_val = false;
  name = key;
  read_only = (mode_char == 'r');
  return true;
}

static bool resolve_choice_resources(const Node* node,
                              const nlohmann::json& invocation_resources,
                              const std::string &request_id,
                              std::unordered_map<std::string, std::string>& resolved,
                              std::unordered_map<std::string, json>& value_inputs,
                              std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>& workflow_resources,
                              std::string* err){
  if (!node) {
    if (err) *err = "missing task node";
    return false;
  }

  std::unordered_set<std::string> seen;

  for(const auto& entry : node->Choices){
    if(seen.count(entry.Variable) > 0){
      continue;
    }

    seen.insert(entry.Variable);
    bool immediate_val = false;
    bool is_read_only = false; //not used since everything here is read_only anyways
    std::string key;
    std::string parse_err;
    if (!parse_resource_placeholder(entry.Variable, immediate_val, key, is_read_only, &parse_err)) {
      if (err) *err = parse_err;
      return false;
    }
    if (immediate_val) {
      if (!invocation_resources.contains(key)) {
        if (err) *err = "missing value resource \"" + key + "\" in invocation";
        return false;
      }
      value_inputs[entry.Variable] = invocation_resources[key];
      continue;
    }

    std::string runtime_name;
    if (key[0] != '_') {
      //global resource
      if (!invocation_resources.contains(key)) {
        if (err) *err = "missing resource \"" + key + "\" in invocation";
        return false;
      }
      if (!invocation_resources.at(key).is_string()) {
        if (err) *err = "resource \"" + key + "\" must map to a string";
        return false;
      }
      runtime_name = invocation_resources.at(key).get<std::string>();
    } else {
      //local_resource
      runtime_name = request_id + key;
      if (workflow_resources.find(runtime_name) == workflow_resources.end()) {
        workflow_resources[runtime_name] = make_cown<detersl::types::Resource>();
      }
    }

    resolved[entry.Variable] = runtime_name;  
  }
  
  return true;  
}

static bool resolve_task_resources(const Node* node,
                              const nlohmann::json& invocation_resources,
                              const std::string &request_id,
                              std::unordered_map<std::string, std::string>& resolved,
                              std::unordered_set<std::string>& read_only,
                              std::unordered_map<std::string, nlohmann::json>& value_inputs,
                              std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>& workflow_resources,
                              std::string* err) {
  if (!node) {
    if (err) *err = "missing task node";
    return false;
  }

  for (const auto& entry : node->Resources) {
    bool immediate_val = false;
    bool is_read_only = false;
    std::string key;
    std::string parse_err;
    if (!parse_resource_placeholder(entry.second, immediate_val, key, is_read_only, &parse_err)) {
      if (err) *err = parse_err;
      return false;
    }

    if (immediate_val) {
      if (!invocation_resources.contains(key)) {
        if (err) *err = "missing value resource \"" + key + "\" in invocation";
        return false;
      }
      value_inputs[entry.first] = invocation_resources.at(key);
      continue;
    }

    std::string runtime_name;
    if (key[0] != '_') {
      //global resource
      if (!invocation_resources.contains(key)) {
        if (err) *err = "missing resource \"" + key + "\" in invocation";
        return false;
      }
      if (!invocation_resources.at(key).is_string()) {
        if (err) *err = "resource \"" + key + "\" must map to a string";
        return false;
      }
      runtime_name = invocation_resources.at(key).get<std::string>();
    } else {
      //local_resource
      runtime_name = request_id + key;
      if (workflow_resources.find(runtime_name) == workflow_resources.end()) {
        workflow_resources[runtime_name] = make_cown<detersl::types::Resource>();
      }
      
    }

    resolved[entry.first] = runtime_name;
    if (is_read_only) {
      read_only.insert(runtime_name);
    } 
  }
  return true;
}

static void schedule_function(detersl::func::WasmFuncInfo func_info,
                                      std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>& workflow_resources,
                                      const detersl::types::BranchGuard* guard)
{
  const size_t num_res = func_info.resources.size();
  const size_t num_ro_res = func_info.read_only_resources.size();
  const size_t num_rw_res = num_res - num_ro_res;
  std::unordered_map<std::string, uint64_t> local_ref_counts;

  cown_ptr<detersl::types::Resource> read_write_resources[num_rw_res];
  cown_ptr<detersl::types::Resource> read_only_resources[num_ro_res];
  size_t ro_index = 0;
  size_t rw_index = 0;

  for(auto & res_name : func_info.resources){
    if(workflow_resources.find(res_name) != workflow_resources.end()){
      // Resource is a workflow-scoped resource
      if(func_info.read_only_resources.find(res_name) != func_info.read_only_resources.end()){
        read_only_resources[ro_index++] = workflow_resources[res_name];
      }
      else{
        read_write_resources[rw_index++] = workflow_resources[res_name];
      }
      continue;
    }

    if (resource_map.find(res_name) == resource_map.end())
    {
      resource_map[res_name] = std::make_pair(make_cown<detersl::types::Resource>(), 0);
    }

    if(func_info.read_only_resources.find(res_name) != func_info.read_only_resources.end()){
      read_only_resources[ro_index++] = resource_map[res_name].first;
    }
    else{
      read_write_resources[rw_index++] = resource_map[res_name].first;
    }
    resource_map[res_name].second++; // increment ref count
    local_ref_counts[res_name] = resource_map[res_name].second;
  }

  cown_array<detersl::types::Resource> rw_cowns{read_write_resources, num_rw_res};
  cown_array<detersl::types::Resource> ro_cowns{read_only_resources, num_ro_res};

  auto run = [func_info, local_ref_counts](auto rw_c, auto ro_c) {
    detersl::runner::WasmRunner runner(rw_c, ro_c, func_info);

      // TODO: maybe a return code of function?
      // Like previous version
    runner.run();
      
    for (const auto& res_name : runner.get_deleted_resources()) {
      deleted_resources_queue.push({res_name, local_ref_counts.at(res_name)});
    }
  };

  if (guard) {
    cown_ptr<detersl::types::ChoiceControl> guard_cown = guard->control;
    size_t guard_edge = guard->edge_index;
    when(rw_cowns, read(ro_cowns), read(guard_cown)) << [guard_edge, run, func_info](auto rw_c, auto ro_c, auto guard_c) {  
      if (!guard_c->decided || guard_c->selected != guard_edge) {
        return;
      }
      run(rw_c, ro_c);
    };
    return;
  }

  when(rw_cowns, read(ro_cowns)) << [run](auto rw_c, auto ro_c) {  
    run(rw_c, ro_c);
  };
}


static bool run_task_node(Node* node,
                          const nlohmann::json& invocation_resources,
                          const std::string &request_id,
                          std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>& workflow_resources,
                          const detersl::types::BranchGuard* guard,
                          std::string* err) {
  if (!node || !node->FuncID) {
    if (err) *err = "task missing func_id";
    return false;
  }
  auto it = wasm_func_registry.find(*node->FuncID);
  if (it == wasm_func_registry.end()) {
    if (err) *err = "unknown func_id " + std::to_string(*node->FuncID);
    return false;
  }

  std::unordered_map<std::string, std::string> resolved;
  std::unordered_set<std::string> read_only;
  std::unordered_map<std::string, nlohmann::json> value_inputs;
  if (!resolve_task_resources(node, invocation_resources, request_id, resolved, read_only, value_inputs, workflow_resources, err)) {
    return false;
  }

  std::unordered_set<std::string> resource_names;
  resource_names.reserve(resolved.size());
  for (const auto& item : resolved) {
    resource_names.insert(item.second);
  }
  std::vector<std::string> resources;
  resources.reserve(resource_names.size());
  for (const auto& name : resource_names) {
    resources.push_back(name);
  }

  detersl::func::WasmFuncInfo func_info = it->second;
  func_info.resources = std::move(resources);
  func_info.read_only_resources = std::move(read_only);

  nlohmann::json input_payload = nlohmann::json::object();
  input_payload["resources"] = nlohmann::json::object();
  input_payload["values"] = nlohmann::json::object();
  for (const auto& item : resolved) {
    input_payload["resources"][item.first] = item.second;
  }
  for (const auto& item : value_inputs) {
    input_payload["values"][item.first] = item.second;
  }
  func_info.func_input_event.data = input_payload.dump();

  schedule_function(func_info, workflow_resources, guard);
  return true;
}

static bool schedule_choice_node(const Node* node,
                                 const nlohmann::json& resources,
                                 const std::string &request_id,
                                 std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>& workflow_resources,
                                 cown_ptr<detersl::types::ChoiceControl> control,
                                 const detersl::types::BranchGuard* guard,
                                 std::string* err)
{  

  std::unordered_map<std::string, std::string> resolved;
  std::unordered_map<std::string, json> value_inputs;
  if (!resolve_choice_resources(node, resources, request_id, resolved, value_inputs, workflow_resources, err)) {
    return false;
  }
  
  std::vector<std::string> resource_names;  
  for (const auto& item : resolved) { 
    resource_names.push_back(item.second); 
  }

  cown_ptr<detersl::types::Resource> resource_cowns[resolved.size()];


  for(int i = 0; i < resource_names.size(); i++){ 
    std::string res_name = resource_names[i];

    if (workflow_resources.find(res_name) != workflow_resources.end()) {
    // Resource is a workflow-scoped resource 
      resource_cowns[i] = workflow_resources[res_name];
      continue; 
      } 
    if (resource_map.find(res_name) == resource_map.end()) { 
        resource_map[res_name] = std::make_pair(make_cown<detersl::types::Resource>(), 0); 
    } 
    resource_cowns[i] = resource_map[res_name].first; 
  }

  cown_array<detersl::types::Resource> cowns{resource_cowns, resource_names.size()};
  
  auto eval_choice = [node, resolved, value_inputs, resource_names](auto local_resources, auto &cown) {
    std::unordered_map<std::string, detersl::types::Resource*> resource_map;

    for (size_t i = 0; i < resource_names.size(); ++i) { 
      resource_map[resource_names[i]] = const_cast<detersl::types::Resource*>(&(*local_resources.array[i]));
    }

    size_t default_index = static_cast<size_t>(-1);
    for (size_t i = 0; i < node->Choices.size(); ++i) {
      const auto& edge = node->Choices[i];
      if (edge.Operand == "default") {
        default_index = i;
        continue;
      }
      bool match = false;

      if(value_inputs.find(edge.Variable) != value_inputs.end()){ 
        json val = value_inputs.at(edge.Variable);
        if (!cmp(val, edge.Operand, edge.Value, &match)) {
          std::cerr << "choice: unsupported operand \"" << edge.Operand << "\"\n";
          cown->decided = true;
          return;
        }
      }
      else {
        if (resource_map.find(resolved.at(edge.Variable)) == resource_map.end()) {
          std::cerr << "choice: resource \"" << resolved.at(edge.Variable) << "\" not found\n";
          continue;
        }
        detersl::types::Resource* resource = resource_map.at(resolved.at(edge.Variable));
        if (!cmpBytes(resource->get_data(), edge.Operand, edge.Value, &match)) {
          std::cerr << "choice: unsupported operand \"" << edge.Operand << "\"\n";
          cown->decided = true;
          return;
        }   
      }
      if (match) {
        cown->selected = i;
        cown->decided = true;
        return;
      }
    }

    if (default_index != static_cast<size_t>(-1)) {
      cown->selected = default_index;
      cown->decided = true;
      return;
    }

    cown->selected = static_cast<size_t>(-1);
    cown->decided = true;
  };

  if (guard) {
    const size_t guard_edge = guard->edge_index;
    when(read(cowns), control, read(guard->control)) << [eval_choice, guard_edge](auto resources, auto control, auto parent) {
      if (!parent->decided || parent->selected != guard_edge) {
        control->decided = true;
        control->selected = static_cast<size_t>(-1);
        return;
      }
      eval_choice(resources, control);
    };
    return true;
  }

  when(read(cowns), control) << [eval_choice](auto resources, auto control) {
    eval_choice(resources, control);
  };
  return true;
}

static bool schedule_graph(Node* node,
                           const nlohmann::json& input,
                           const std::string& request_id,
                           std::string* err)
{
  std::unordered_map<std::string, cown_ptr<detersl::types::Resource>> workflow_resources;

  if (!node) return true;

  struct QueueItem {
    Node* node;
    detersl::types::BranchGuard guard;

    bool operator==(const QueueItem& other) const {
      return node == other.node &&
            guard.control == other.guard.control &&
            guard.edge_index == other.guard.edge_index;
    }
  };

  auto hash_queue_item = [](const QueueItem& qi){return std::hash<const Node*>{}(qi.node);};
  std::unordered_set<QueueItem, decltype(hash_queue_item)> visited(0, hash_queue_item);

  std::deque<QueueItem> queue;
  queue.push_back({node, detersl::types::BranchGuard{}});

  while (!queue.empty()) {
    QueueItem item = queue.front();
    queue.pop_front();

    if (!item.node) {
      continue;
    }

    const detersl::types::BranchGuard* active_guard = item.guard.control ? &item.guard : nullptr;
    std::string type_lower = item.node->Type;
    std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);

    if (type_lower == "task") {
      if(visited.count(item) != 0) {
        if(err) *err = "cannot schedule function " + std::to_string(*item.node->FuncID) + " twice.";
        return false;
      }
      visited.insert(item);
      if (!run_task_node(item.node, input, request_id, workflow_resources, active_guard, err)) {
        return false;
      }
      if (item.node->End) {
        continue;
      }
      if (!item.node->Next) {
        if (err) *err = "node has no Next and End=false";
        return false;
      }
      queue.push_back({item.node->Next, item.guard});
    } else if (type_lower == "choice") {
      cown_ptr<detersl::types::ChoiceControl> control = make_cown<detersl::types::ChoiceControl>();
      if(!schedule_choice_node(item.node, input, request_id, workflow_resources, control, active_guard, err)) {
        return false;
      }
      
      for (size_t i = 0; i < item.node->Choices.size(); ++i) {
        const ChoiceEdge& edge = item.node->Choices[i];
        if (!edge.Next) {
          continue;
        }
        detersl::types::BranchGuard next_guard{control, i};
        queue.push_back({edge.Next, next_guard});
      }
    } else {
      if (err) *err = "unknown node type \"" + item.node->Type + "\"";
      return false;
    }
  }

  return true;
}

int register_wasm_function(const nlohmann::json& j, std::string* err, int* func_id)
{

  static rust::Box<FfiExecutioner> compile_exec = new_executioner(*engine, new_cpp_kv(new KVInterface()));
  
  try {

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
                    
    // TODO: this is not function DSL, so we cannot pass it directly 
    // to the executioner. We have to create this after having
    // our own function DSL.
    compile_exec->executioner_compile_json(j.dump());

    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    
    
    detersl::func::WasmFuncInfo info = detersl::func::WasmFuncInfo::from_json(j);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Function " << info.func_name << " compiled in " << duration << " ms\n";
    
    const int id = next_wasm_func_id++;
    wasm_func_registry[id] = std::move(info);
    if (func_id) *func_id = id;

    return 0;
  } catch (const rust::Error& e) {
    if (err) *err = e.what();
    return -1;
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return -1;
  }
}

int register_workflow(const detersl::types::Workflow& workflow, std::string* err)
{
  if (workflow.ID.empty()) {
    if (err) *err = "workflow id is required";
    return -1;
  }

  if (workflow_registry.find(workflow.ID) != workflow_registry.end()) {
    if (err) *err = "workflow already registered: " + workflow.ID;
    return -1;
  }
  Node* root = BuildFromWorkflow(workflow, err);
  if (!root) return -1;
  workflow_registry.emplace(workflow.ID, root);
  return 0;
}

bool schedule_workflow(const detersl::types::WorkflowRequest& request, std::string* err)
{
  auto it = workflow_registry.find(request.WorkflowID);
  if (it == workflow_registry.end()) {
    if (err) *err = "unknown workflow id: " + request.WorkflowID;
    return false;
  }
  Node* root = it->second;
  return schedule_graph(root, request.Input, request.RequestID, err);
}

bool invoke_workflow(const detersl::types::InvokeDTO& invoke, std::string* err)
{
  detersl::types::Workflow workflow;
  {
    auto it = workflow_registry.find(invoke.WorkflowID);
    if (it == workflow_registry.end()) {
      if (err) *err = "unknown workflow id: " + invoke.WorkflowID;
      return false;
    }
  }

  detersl::types::WorkflowRequest request;
  request.WorkflowID = invoke.WorkflowID;
  request.Input = invoke.Input;
  request.RequestID = invoke.WorkflowID + ":" + std::to_string(next_workflow_request_id++);
  return schedule_workflow(request, err);
}

void cleanup_resources()
{
  std::pair<std::string, uint64_t> res;
  while(!((res = deleted_resources_queue.pop()).first).empty()){
    auto it = resource_map.find(res.first);
    if (it != resource_map.end() && it->second.second == res.second)
    {
      resource_map.erase(it);
    }
  }
}

} // namespace detersl::worker
