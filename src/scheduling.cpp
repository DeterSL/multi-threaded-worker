#include "scheduling.hpp"

#include <dlfcn.h>
#include <cctype>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <fstream>
#include "basic-net.hpp"
#include "cpp-func.hpp"
#include "cpp-runner.hpp"
#include "cpp/cown.h"
#include "resource.hpp"
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
std::mutex workflow_registry_mutex;
std::atomic<uint64_t> next_workflow_request_id{1};

struct ChoiceControl {
  bool decided = false;
  size_t selected = 0;

  ~ChoiceControl(){
    std::cout << "ChoiceControl destructor called" << std::endl;
  }
};

struct BranchGuard {
  cown_ptr<ChoiceControl> control;
  size_t edge_index = 0;
};

static void schedule_function_guarded(detersl::func::WasmFuncInfo func_info,
                                      detersl::func::WasmFunc func, 
                                      std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>* workflow_resources,
                                      const std::vector<BranchGuard>& guards);

// namespace detersl::worker
void register_function(const std::string& name, detersl::types::FunctionType fn)
{
  all_functions[name] = fn;
}

static std::string trim_copy(std::string input) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  input.erase(input.begin(), std::find_if(input.begin(), input.end(), not_space));
  input.erase(std::find_if(input.rbegin(), input.rend(), not_space).base(), input.end());
  return input;
}

static bool parse_resource_placeholder(const std::string& placeholder,
                                       char* scope,
                                       std::string* name,
                                       char* mode,
                                       std::string* err) {
  const auto trimmed = trim_copy(placeholder);
  if (trimmed.size() < 3 || trimmed[1] != '.') {
    if (err) *err = "invalid resource placeholder: " + placeholder;
    return false;
  }
  const char scope_char = trimmed[0];
  if (scope_char != '$' && scope_char != '&') {
    if (err) *err = "unsupported placeholder scope: " + placeholder;
    return false;
  }
  const auto colon_pos = trimmed.rfind(':');
  if (colon_pos == std::string::npos || colon_pos + 2 != trimmed.size()) {
    if (err) *err = "missing access mode in placeholder: " + placeholder;
    return false;
  }
  const char mode_char = trimmed[colon_pos + 1];
  if (mode_char != 'r' && mode_char != 'w' && mode_char != 'v') {
    if (err) *err = "unsupported access mode in placeholder: " + placeholder;
    return false;
  }
  const std::string key = trimmed.substr(2, colon_pos - 2);
  if (key.empty()) {
    if (err) *err = "empty resource key in placeholder: " + placeholder;
    return false;
  }
  if (scope) *scope = scope_char;
  if (name) *name = key;
  if (mode) *mode = mode_char;
  return true;
}

static bool resolve_resources(const Node* node,
                              const nlohmann::json& invocation_resources,
                              const std::string &request_id,
                              std::unordered_map<std::string, std::string>* resolved,
                              std::unordered_set<std::string>* read_only,
                              std::unordered_map<std::string, std::string>* value_inputs,
                              std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>* workflow_resources,
                              std::string* err) {
  if (!node) {
    if (err) *err = "missing task node";
    return false;
  }
  if (!resolved || !read_only || !value_inputs || !workflow_resources) {
    if (err) *err = "internal resource resolution error";
    return false;
  }
  resolved->clear();
  read_only->clear();
  value_inputs->clear();

  for (const auto& entry : node->Resources) {
    char scope = 0;
    char mode = 0;
    std::string key;
    std::string parse_err;
    if (!parse_resource_placeholder(entry.second, &scope, &key, &mode, &parse_err)) {
      if (err) *err = parse_err;
      return false;
    }

    if (mode == 'v') {
      if (!invocation_resources.contains(key)) {
        if (err) *err = "missing value resource \"" + key + "\" in invocation";
        return false;
      }
      (*value_inputs)[entry.first] = invocation_resources.at(key).get<std::string>();
      continue;
    }

    std::string runtime_name;
    if (scope == '$') {
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
      runtime_name = request_id + ":" + key;
      if (workflow_resources->find(runtime_name) == workflow_resources->end()) {
        (*workflow_resources)[runtime_name] = make_cown<detersl::types::Resource>();
      }
    }

    (*resolved)[entry.first] = runtime_name;
    if (mode == 'r') {
      read_only->insert(runtime_name);
    } 
  }
  return true;
}

static bool run_task_node(Node* node,
                          const nlohmann::json& invocation_resources,
                          const std::string &request_id,
                          std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>* workflow_resources,
                          const std::vector<BranchGuard>& guards,
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
  std::unordered_map<std::string, std::string> value_inputs;
  if (!resolve_resources(node, invocation_resources, request_id, &resolved, &read_only, &value_inputs, workflow_resources, err)) {
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
  func_info.resources = resources;
  func_info.read_only_resources = read_only;

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

  detersl::func::WasmFunc func(func_info);
  if (guards.empty()) {
    schedule_function(func_info, func, workflow_resources);
  } else {
    schedule_function_guarded(func_info, func, workflow_resources, guards);
  }
  return true;
}

static void schedule_choice_node(const Node* node,
                                 const nlohmann::json& input,
                                 cown_ptr<ChoiceControl> control)
{
  when(control) << [node, input](auto cown) {
    size_t default_index = static_cast<size_t>(-1);
    for (size_t i = 0; i < node->Choices.size(); ++i) {
      const auto& edge = node->Choices[i];
      if (edge.Operand == "default") {
        default_index = i;
        continue;
      }
      json val;
      if (!readByPath(input, edge.Variable, &val)) {
        continue;
      }
      bool match = false;
      if (!cmp(val, edge.Operand, edge.Value, &match)) {
        std::cerr << "choice: unsupported operand \"" << edge.Operand << "\"\n";
        cown->decided = true;
        return;
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

    std::cerr << "choice: no rule matched and no default\n";
    cown->decided = true;
  };
}

static bool schedule_graph(Node* node,
                           const nlohmann::json& input,
                           const nlohmann::json& resources,
                           const std::string& request_id,
                           std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>* workflow_resources,
                           std::unordered_map<const Node*, cown_ptr<ChoiceControl>>* choice_controls,
                           const std::vector<BranchGuard>& guards,
                           std::unordered_set<const Node*>* visiting,
                           std::string* err)
{
  if (!node) return true;
  if (!workflow_resources || !choice_controls || !visiting) {
    if (err) *err = "internal workflow scheduling error";
    return false;
  }
  if (visiting->count(node) != 0) {
    if (err) *err = "cycle detected in workflow graph";
    return false;
  }

  visiting->insert(node);

  std::string type_lower = node->Type;
  std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);
  if (type_lower == "task") {
    if (!run_task_node(node, resources, request_id, workflow_resources, guards, err)) {
      visiting->erase(node);
      return false;
    }
    if (node->End) {
      visiting->erase(node);
      return true;
    }
    if (!node->Next) {
      if (err) *err = "node has no Next and End=false";
      visiting->erase(node);
      return false;
    }
    if (!schedule_graph(node->Next, input, resources, request_id, workflow_resources, choice_controls, guards, visiting, err)) {
      visiting->erase(node);
      return false;
    }
  } else if (type_lower == "pass") {
    if (node->End) {
      visiting->erase(node);
      return true;
    }
    if (!node->Next) {
      if (err) *err = "node has no Next and End=false";
      visiting->erase(node);
      return false;
    }
    if (!schedule_graph(node->Next, input, resources, request_id, workflow_resources, choice_controls, guards, visiting, err)) {
      visiting->erase(node);
      return false;
    }
  } else if (type_lower == "choice") {
    cown_ptr<ChoiceControl> control;
    auto it = choice_controls->find(node);
    if (it == choice_controls->end()) {
      control = make_cown<ChoiceControl>();
      choice_controls->emplace(node, control);
      schedule_choice_node(node, input, control);
    } else {
      control = it->second;
    }

    for (size_t i = 0; i < node->Choices.size(); ++i) {
      const ChoiceEdge& edge = node->Choices[i];
      std::vector<BranchGuard> next_guards = guards;
      next_guards.push_back(BranchGuard{control, i});
      if (!schedule_graph(edge.Next, input, resources, request_id, workflow_resources, choice_controls, next_guards, visiting, err)) {
        visiting->erase(node);
        return false;
      }
    }
  } else {
    if (err) *err = "unknown node type \"" + node->Type + "\"";
    visiting->erase(node);
    return false;
  }

  visiting->erase(node);
  return true;
}

void schedule_function(detersl::func::WasmFuncInfo func_info,
                       detersl::func::WasmFunc func, 
                       std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>* workflow_resources)
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
    if(workflow_resources && workflow_resources->find(res_name) != workflow_resources->end()){
      // Resource is a workflow-scoped resource
      if(func_info.read_only_resources.find(res_name) != func_info.read_only_resources.end()){
        read_only_resources[ro_index++] = (*workflow_resources)[res_name];
      }
      else{
        read_write_resources[rw_index++] = (*workflow_resources)[res_name];
      }
      continue;
    }

    if (resource_map.find(res_name) == resource_map.end())
    {
      std::cout << "Creating new cown for resource: " << res_name << std::endl;
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

  when(rw_cowns, read(ro_cowns)) << [func_info, func, local_ref_counts](auto rw_c, auto ro_c) {  
    detersl::runner::WasmRunner runner(rw_c, ro_c, func_info, func);

    // TODO: maybe a return code of funciton?
    // Like previous version
    runner.run();
    

    for (const auto& res_name : runner.get_deleted_resources()) {
      deleted_resources_queue.push({res_name, local_ref_counts.at(res_name)});
    }
  };
}

static void schedule_function_guarded(detersl::func::WasmFuncInfo func_info,
                                      detersl::func::WasmFunc func, 
                                      std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>* workflow_resources,
                                      const std::vector<BranchGuard>& guards)
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
    if(workflow_resources && workflow_resources->find(res_name) != workflow_resources->end()){
      // Resource is a workflow-scoped resource
      if(func_info.read_only_resources.find(res_name) != func_info.read_only_resources.end()){
        read_only_resources[ro_index++] = (*workflow_resources)[res_name];
      }
      else{
        read_write_resources[rw_index++] = (*workflow_resources)[res_name];
      }
      continue;
    }

    if (resource_map.find(res_name) == resource_map.end())
    {
      std::cout << "Creating new cown for resource: " << res_name << std::endl;
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

  std::vector<cown_ptr<ChoiceControl>> guard_cowns;
  guard_cowns.reserve(guards.size());
  std::vector<size_t> guard_edges;
  guard_edges.reserve(guards.size());
  for (const auto& guard : guards) {
    guard_cowns.push_back(guard.control);
    guard_edges.push_back(guard.edge_index);
  }

  cown_array<detersl::types::Resource> rw_cowns{read_write_resources, num_rw_res};
  cown_array<detersl::types::Resource> ro_cowns{read_only_resources, num_ro_res};
  cown_array<ChoiceControl> choice_cowns{guard_cowns.data(), guard_cowns.size()};

  when(rw_cowns, read(ro_cowns), read(choice_cowns)) << [func_info, func, local_ref_counts, guard_edges](auto rw_c, auto ro_c, auto guard_c) {  
    for (size_t i = 0; i < guard_edges.size(); ++i) {
      ChoiceControl* state = const_cast<ChoiceControl*>(&(*guard_c.array[i]));
      if (!state->decided || state->selected != guard_edges[i]) {
        std::cout << "Skipping function " << func_info.func_name << " due to guard\n";
        return;
      }
    }

    detersl::runner::WasmRunner runner(rw_c, ro_c, func_info, func);

    // TODO: maybe a return code of funciton?
    // Like previous version
    runner.run();
    

    for (const auto& res_name : runner.get_deleted_resources()) {
      deleted_resources_queue.push({res_name, local_ref_counts.at(res_name)});
    }
  };
}

std::string read_file_as_string(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// int parse_and_load_cpp(const std::string& func_name)
// {
//   void* handle = dlopen(("./" + func_name + ".dylib").c_str(), RTLD_NOW | RTLD_GLOBAL);
//   if (!handle)
//   {
//     std::cerr << "dlopen error: " << dlerror() << "\n";
//     return 1;
//   }

//   dlerror(); // clear any existing error

//   detersl::types::FunctionType fn = (detersl::types::FunctionType)dlsym(handle, "func");
//   const char* err = dlerror();
//   if (err)
//   {
//     std::cerr << "dlsym error: " << err << "\n";
//     return 1;
//   }

//   register_function(func_name, fn);

//   std::ifstream ifs("../functions/" + func_name + ".json");
//   std::string json_config = read_file_as_string("../functions/" + func_name + ".json");
//   detersl::func::CPPFuncInfo f = detersl::func::CPPFuncInfo::from_json(json_config);
//   detersl::func::CPPFunc func(fn, f);

//   std::cout << "Loaded: " << func_name << std::endl;

//   schedule_function(f, func);
//   return 0;
// }

int parse_and_load(const std::string& func_name)
{
  const std::string path = "../functions/" + func_name + ".json";
  std::string json_config;

  try{
    json_config = read_file_as_string(path);
  } catch (const std::runtime_error& e) {
    std::cerr << "Could not find file: " << path << "\n";
    return 1;
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_config);
  } catch (const nlohmann::json::parse_error& e) {
    std::cerr << "JSON parse error in " << path << ": " << e.what() << "\n";
    return 1;
  }

  detersl::func::WasmFuncInfo f = detersl::func::WasmFuncInfo::from_json(j);
  detersl::func::WasmFunc func(f);

  std::cout << "Loaded: " << func_name << std::endl;
  schedule_function(f, func, nullptr);
  return 0;
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

int register_wasm_function(const nlohmann::json& j, std::string* err, int* func_id)
{
  try {
    detersl::func::WasmFuncInfo info = detersl::func::WasmFuncInfo::from_json(j);
    const int id = next_wasm_func_id++;
    wasm_func_registry[id] = std::move(info);
    if (func_id) *func_id = id;
    return 0;
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

  std::lock_guard<std::mutex> lock(workflow_registry_mutex);
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
  std::unordered_map<std::string, cown_ptr<detersl::types::Resource>> workflow_resources;

  nlohmann::json invocation;
  try {
    invocation = nlohmann::json::parse(request.Input);
  } catch (const std::exception& e) {
    if (err) *err = std::string("invalid workflow input: ") + e.what();
    return false;
  }

  if (!invocation.is_object() || !invocation.contains("resources")) {
    if (err) *err = "workflow input must include resources";
    return false;
  }

  const auto& resources = invocation.at("resources");
  if (!resources.is_object()) {
    if (err) *err = "workflow resources must be an object";
    return false;
  }

  auto it = workflow_registry.find(request.WorkflowID);
  if (it == workflow_registry.end()) {
    if (err) *err = "unknown workflow id: " + request.WorkflowID;
    return false;
  }
  Node* root = it->second;
  std::unordered_map<const Node*, cown_ptr<ChoiceControl>> choice_controls;
  std::unordered_set<const Node*> visiting;
  return schedule_graph(root, invocation, resources, request.RequestID, &workflow_resources, &choice_controls, {}, &visiting, err);
}

bool invoke_workflow(const detersl::types::InvokeDTO& invoke, std::string* err)
{
  detersl::types::Workflow workflow;
  {
    std::lock_guard<std::mutex> lock(workflow_registry_mutex);
    auto it = workflow_registry.find(invoke.WorkflowID);
    if (it == workflow_registry.end()) {
      if (err) *err = "unknown workflow id: " + invoke.WorkflowID;
      return false;
    }
  }

  detersl::types::WorkflowRequest request;
  request.WorkflowID = invoke.WorkflowID;
  request.Input = invoke.Input;
  request.RequestID = invoke.WorkflowID + ":" + std::to_string(next_workflow_request_id.fetch_add(1));
  return schedule_workflow(request, err);
}

void register_and_schedule()
{
  int client_fd = listen_and_accept_client_connection(6666);
  const int delete_after_n_func = 3;
  int func_count = 0;

  while (1)
  {
    constexpr size_t BUF_SIZE = 128;
    char buffer[BUF_SIZE];
    ssize_t n = ::read(client_fd, buffer, BUF_SIZE);
    if (n < 0)
    {
      std::perror("read");
      break;
    }
    if (n == 0)
    {
      std::cout << "Client disconnected.\n";
      break;
    }
    buffer[n - 1] = '\0';

    if(func_count >= delete_after_n_func) {
      func_count = 0;
      cleanup_resources();
    }

    if(parse_and_load(buffer) == 0){
      func_count++;
    }

  }
}

} // namespace detersl::worker
