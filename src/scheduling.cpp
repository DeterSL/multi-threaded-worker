#include "scheduling.hpp"

#include <dlfcn.h>
#include <cctype>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <deque>
#include <vector>
#include <fstream>
#include "cpp/cown.h"
#include "resource.hpp"
#include "types.hpp"
#include "wasm-func.hpp"
#include "wasm-runner.hpp"
#include "thread-safe-queue.hpp"
#include "graph.hpp"
#include "utils.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace detersl::worker {

std::unordered_map<std::string, std::pair<cown_ptr<detersl::types::Resource>, uint64_t>> resource_map;
BasicMPSCQueue<std::pair<std::string, uint64_t>> deleted_resources_queue;
std::unordered_map<int, detersl::func::WasmFuncInfo> wasm_func_registry;
int next_wasm_func_id = 1;
std::unordered_map<std::string, Node*> workflow_registry;
int next_workflow_request_id = 1;


static void schedule_function(detersl::func::WasmFuncInfo func_info,
                                      detersl::types::WorkflowInvocation& invocation,
                                      const detersl::types::BranchGuard* guard)
{
  std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>& workflow_resources = invocation.workflow_resources;
  std::unordered_set<std::string>& workflow_rw_resources = invocation.workflow_rw_resources;
  const std::shared_ptr<std::atomic<bool>>& failed = invocation.failed;
  const bool can_abort = invocation.request.can_abort;

  const size_t num_res = func_info.resources.size();
  const size_t num_ro_res = func_info.read_only_resources.size();
  const size_t num_rw_res = num_res - num_ro_res;

  cown_ptr<detersl::types::Resource> read_write_resources[num_rw_res];
  cown_ptr<detersl::types::Resource> read_only_resources[num_ro_res];
  size_t ro_index = 0;
  size_t rw_index = 0;

  std::unordered_map<std::string, int> local_ref_counts;

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
      workflow_rw_resources.insert(res_name);
    }
    local_ref_counts[res_name] = ++resource_map[res_name].second; // increment ref count
  }

  cown_array<detersl::types::Resource> rw_cowns{read_write_resources, num_rw_res};
  cown_array<detersl::types::Resource> ro_cowns{read_only_resources, num_ro_res};

  auto run = [func_info, can_abort, workflow_rw_resources, failed, local_ref_counts](auto rw_c, auto ro_c){
    detersl::runner::WasmRunner runner(rw_c, ro_c, func_info);
    
    bool ok = runner.run();

    if(!can_abort){
      // commit global resources directly since workflow cannot abort
      for(auto& res: local_ref_counts){
        if(workflow_rw_resources.find(res.first) == workflow_rw_resources.end()){
          //is read only
          continue;
        }
        detersl::types::Resource* res_ptr = runner.storage->get_resource(res.first);
        if(res_ptr){
          res_ptr->commit_uncommitted();
          if(res_ptr->is_deleted()){
            // push to deleted resource queue for cleanup
            deleted_resources_queue.push({res.first, res.second});
          }
        }
      }
      return;
    }

    if (!ok) {
      failed->store(true, std::memory_order_release);      
    }
  };

  if (guard) {
    cown_ptr<detersl::types::ChoiceControl> guard_cown = guard->control;
    size_t guard_edge = guard->edge_index;
    when(rw_cowns, read(ro_cowns), read(guard_cown)) << [guard_edge, run, failed](auto rw_c, auto ro_c, auto guard_c) {  
      if (!guard_c->decided || guard_c->selected != guard_edge || failed->load(std::memory_order_acquire)) {
        return;
      }
      run(rw_c, ro_c);
    };
    return;
  }

  when(rw_cowns, read(ro_cowns)) << [run, failed](auto rw_c, auto ro_c) {  
    if(failed->load(std::memory_order_acquire)){
      return;
    }
    run(rw_c, ro_c);
  };
}


static bool run_task_node(Node* node,
                          detersl::types::WorkflowInvocation& invocation,
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
  if (!detersl::utils::resolve_task_resources(node, invocation, resolved, read_only, value_inputs, err)) {
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

  schedule_function(func_info, invocation, guard);
  return true;
}

static bool schedule_choice_node(const Node* node,
                                 detersl::types::WorkflowInvocation& invocation,
                                 cown_ptr<detersl::types::ChoiceControl> control,
                                 const detersl::types::BranchGuard* guard,
                                 std::string* err)
{  

  std::unordered_map<std::string, std::string> resolved;
  std::unordered_map<std::string, json> value_inputs;
  if (!detersl::utils::resolve_choice_resources(node, invocation, resolved, value_inputs, err)) {
    return false;
  }

  std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>& workflow_resources = invocation.workflow_resources;
  const std::shared_ptr<std::atomic<bool>>& failed = invocation.failed;

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
  
  auto eval_choice = [node, resolved, value_inputs, resource_names, failed](auto local_resources, auto &cown) {
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
          failed->store(true, std::memory_order_release);
          return;
        }
      }
      else {
        if (resource_map.find(resolved.at(edge.Variable)) == resource_map.end()) {
          std::cerr << "choice: resource \"" << resolved.at(edge.Variable) << "\" not found\n";
          failed->store(true, std::memory_order_release);
          continue;
        }
        detersl::types::Resource* resource = resource_map.at(resolved.at(edge.Variable));
        if (!cmpBytes(resource->get_data(), edge.Operand, edge.Value, &match)) {
          std::cerr << "choice: unsupported operand \"" << edge.Operand << "\"\n";
          cown->decided = true;
          failed->store(true, std::memory_order_release);
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
    when(read(cowns), control, read(guard->control)) << [eval_choice, guard_edge, failed](auto resources, auto control, auto parent) {
      if (!parent->decided || parent->selected != guard_edge || failed->load(std::memory_order_acquire)) {
        control->decided = true;
        control->selected = static_cast<size_t>(-1);
        return;
      }
      eval_choice(resources, control);
    };
    return true;
  }

  when(read(cowns), control) << [eval_choice, failed](auto resources, auto control) {
    if(failed->load(std::memory_order_acquire)){
      control->decided = true;
      control->selected = static_cast<size_t>(-1);
      return;
    }
    eval_choice(resources, control);
  };
  return true;
}

void schedule_commit_behaviour(detersl::types::WorkflowInvocation& invocation) {
  std::unordered_set<std::string>& workflow_rw_resources = invocation.workflow_rw_resources;
  const std::shared_ptr<std::atomic<bool>>& failed = invocation.failed;

  std::unordered_map<std::string, int> local_ref_counts;
  std::vector<std::string> commit_resource_names;

  std::vector<cown_ptr<detersl::types::Resource>> commit_cowns;
  commit_cowns.reserve(workflow_rw_resources.size());
  for (const auto& res_name : workflow_rw_resources) {
    auto it = resource_map.find(res_name);
    if (it != resource_map.end()) {
      commit_cowns.push_back(it->second.first);
      local_ref_counts[res_name] = ++it->second.second; // get ref count and increment
      commit_resource_names.push_back(res_name);
    }
  }

  if (!commit_cowns.empty()) {
    cown_array<detersl::types::Resource> cowns{commit_cowns.data(), commit_cowns.size()};
    when(cowns) << [failed, &local_ref_counts, &commit_resource_names](auto resources) {
      const bool is_failed = failed->load(std::memory_order_acquire);
      for (size_t i = 0; i < resources.length; ++i) {
        auto& res = *resources.array[i];
        if (is_failed) {
          res.abort_uncommitted();
        } else {
          res.commit_uncommitted();
          if(res.is_deleted()){
            deleted_resources_queue.push({commit_resource_names[i], local_ref_counts[commit_resource_names[i]]});
          }
        }
      }
    };
  } 
}

static bool schedule_graph(Node* node,
                          detersl::types::WorkflowInvocation& invocation,
                          std::string* err)
{
  if (!node) return true;

  struct QueueItem {
    Node* node;
    detersl::types::BranchGuard guard;
  };

  std::deque<QueueItem> queue;
  queue.push_back({node, detersl::types::BranchGuard{}});

  bool ok = true;
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
      if (!run_task_node(item.node, invocation, active_guard, err)) {
        ok = false;
        break;
      }
      if (item.node->End) {
        continue;
      }
      if (!item.node->Next) {
        if (err) *err = "node has no Next and End=false";
        ok = false;
        break;
      }
      queue.push_back({item.node->Next, item.guard});
    } else if (type_lower == "choice") {
      cown_ptr<detersl::types::ChoiceControl> control = make_cown<detersl::types::ChoiceControl>();
      if(!schedule_choice_node(item.node, invocation, control, active_guard, err)) {
        ok = false;
        break;
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
      ok = false;
      break;
    }
  }

  if(!invocation.request.can_abort){
    return true;
  }

  if (!ok) {
    invocation.failed->store(true, std::memory_order_release); 
  }

  schedule_commit_behaviour(invocation);

  return ok;
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

bool schedule_workflow(detersl::types::WorkflowInvocation& invocation, std::string* err)
{
  auto it = workflow_registry.find(invocation.request.WorkflowID);
  if (it == workflow_registry.end()) {
    if (err) *err = "unknown workflow id: " + invocation.request.WorkflowID;
    return false;
  }
  Node* root = it->second;
  return schedule_graph(root, invocation, err);
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

  std::string invocation_id = invoke.WorkflowID + ":" + std::to_string(next_workflow_request_id++);
  
  detersl::types::WorkflowRequest request{
    .WorkflowID = invoke.WorkflowID,
    .Input = invoke.Input,
    .RequestID = invocation_id,
    .can_abort = invoke.can_abort
  };
  
  detersl::types::WorkflowInvocation invocation{
    .workflow_resources = {},
    .workflow_rw_resources = {},
    .failed = std::make_shared<std::atomic<bool>>(false),
    .request = std::move(request)
  };
  return schedule_workflow(invocation, err);
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

bool get_resource(const std::string &res_name, std::future<rust::Vec<uint8_t>>& res_data) {
  auto it = resource_map.find(res_name);

  if (it == resource_map.end()) {
    return false;
  }
  auto res = it->second.first;

  auto promise = std::make_shared<std::promise<rust::Vec<uint8_t>>>();
  res_data = promise->get_future();

  when(read(res)) << [promise](auto r) {
    promise->set_value(r->get_data().as_vec());
  };

  return true;
}

} // namespace detersl::worker
