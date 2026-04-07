#include "scheduling.hpp"
#include "metrics.hpp"

using json = nlohmann::json;

namespace detersl::worker {

void Scheduling::schedule_function(detersl::func::WasmFuncInfo&& func_info,
                                      detersl::types::WorkflowInvocation& invocation,
                                      const detersl::types::BranchGuard* guard)
{
  const std::shared_ptr<std::atomic<bool>>& failed = invocation.failed;
  const bool can_abort = invocation.request.can_abort;

  std::vector<cown_ptr<detersl::types::Resource>> read_write_resources;
  std::vector<cown_ptr<detersl::types::Resource>> read_only_resources;

  std::unordered_map<std::string, int> local_ref_counts;

  for(auto & res_name : func_info.resources){
    if(invocation.workflow_resources.find(res_name) != invocation.workflow_resources.end()){
      // Resource is a workflow-scoped resource
      if(func_info.read_only_resources.find(res_name) != func_info.read_only_resources.end()){
        read_only_resources.push_back(invocation.workflow_resources[res_name]);
      }
      else{
        read_write_resources.push_back(invocation.workflow_resources[res_name]);
      }
      continue;
    }

    auto it = resource_map.find(res_name);
    if (it == resource_map.end())
    {
      it = resource_map.insert({res_name, std::make_pair(make_cown<detersl::types::Resource>(), 0)}).first;
    }
  
    if(func_info.read_only_resources.find(res_name) != func_info.read_only_resources.end()){
      read_only_resources.push_back(it->second.first);
    }
    else{
      read_write_resources.push_back(it->second.first);
      invocation.workflow_rw_resources.insert(res_name);
    }

    local_ref_counts[res_name] = ++it->second.second; // increment ref count
  }

  cown_array<detersl::types::Resource> rw_cowns{
    read_write_resources.data(),
    read_write_resources.size()
  };
  cown_array<detersl::types::Resource> ro_cowns{
    read_only_resources.data(),
    read_only_resources.size()
  };

  auto run = [func_info = std::move(func_info), local_refs = std::move(local_ref_counts), can_abort, failed, this](auto rw_c, auto ro_c){
    detersl::runner::WasmRunner runner(rw_c, ro_c, std::move(func_info));
    
    bool ok = runner.run();

    if(!can_abort){
      // commit global resources directly since workflow cannot abort
      for(auto& res: local_refs){
        if(func_info.read_only_resources.count(res.first)){
          //is read only resource
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
    when(rw_cowns, read(ro_cowns), read(guard_cown), read(invocation.invocation_cown)) << [guard_edge, run, failed, can_abort](auto rw_c, auto ro_c, auto guard_c, auto ic) {  
      if (!guard_c->decided || guard_c->selected != guard_edge || (failed->load(std::memory_order_acquire) && can_abort) ) {
        return;
      }
      run(rw_c, ro_c);
    };
    return;
  }

  when(rw_cowns, read(ro_cowns), read(invocation.invocation_cown)) << [run, failed, can_abort](auto rw_c, auto ro_c, auto ic) {  
    if(failed->load(std::memory_order_acquire) && can_abort){
      return;
    }
    run(rw_c, ro_c);
  };
}

bool Scheduling::run_task_node(Node* node,
                          detersl::types::WorkflowInvocation& invocation,
                          const detersl::types::BranchGuard* guard,
                          std::string* err) {
  if (!node) {
    if (err) *err = "task missing function";
    return false;
  }
  auto it = wasm_func_registry.find(node->FuncID);
  if (it == wasm_func_registry.end()) {
    if (err) *err = "unknown functions " + node->FuncID;
    return false;
  }

  detersl::func::WasmFuncInfo func_info = it->second;

  std::unordered_map<std::string, nlohmann::json> value_inputs;
  std::unordered_map<std::string, nlohmann::json> resource_inputs;
  if (!detersl::utils::resolve_resources(node, invocation, resource_inputs, func_info.resources, 
    &func_info.read_only_resources, value_inputs, err, true)) {
    return false;
  }
  
  nlohmann::json input_payload = nlohmann::json::object();
  input_payload["resources"] = nlohmann::json::object();
  input_payload["values"] = nlohmann::json::object();

  for (const auto& item : resource_inputs) {
    input_payload["resources"][item.first] = item.second;
  }
  for (const auto& item : value_inputs) {
    input_payload["values"][item.first] = item.second;
  }
  func_info.func_input_event.data = input_payload.dump();
  schedule_function(std::move(func_info), invocation, guard);
  return true;
}

bool Scheduling::schedule_choice_node(const Node* node,
                                 detersl::types::WorkflowInvocation& invocation,
                                 cown_ptr<detersl::types::ChoiceControl> control,
                                 const detersl::types::BranchGuard* guard,
                                 std::string* err)
{  
  const bool can_abort = invocation.request.can_abort;

  std::unordered_map<std::string, json> value_inputs;
  std::unordered_map<std::string, json> resource_inputs;
  std::vector<std::string> resource_names;
  if (!detersl::utils::resolve_resources(node, invocation, resource_inputs, resource_names, nullptr, value_inputs, err, false)) {
    return false;
  }

  std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>& workflow_resources = invocation.workflow_resources;
  const std::shared_ptr<std::atomic<bool>>& failed = invocation.failed;

  std::vector<cown_ptr<detersl::types::Resource>> resource_cowns;

  for(const std::string &res_name: resource_names){
    if (invocation.workflow_resources.find(res_name) != invocation.workflow_resources.end()) {
    // Resource is a workflow-scoped resource 
      resource_cowns.push_back(invocation.workflow_resources[res_name]);
      continue; 
    } 

    auto it = resource_map.find(res_name);
    if (it == resource_map.end()) { 
        it = resource_map.insert({res_name, std::make_pair(make_cown<detersl::types::Resource>(), 0)}).first; 
    } 
    resource_cowns.push_back(it->second.first);
  }

  cown_array<detersl::types::Resource> cowns{
    resource_cowns.data(),
    resource_cowns.size()
  };
  
  auto eval_choice = [node, resource_inputs = std::move(resource_inputs), vals = std::move(value_inputs), res_names = std::move(resource_names), failed](auto local_resources, auto &cown) {
    std::unordered_map<std::string, detersl::types::Resource*> resource_map;

    for (size_t i = 0; i < res_names.size(); ++i) { 
      resource_map[res_names[i]] = const_cast<detersl::types::Resource*>(&(*local_resources.array[i]));
    }

    size_t default_index = static_cast<size_t>(-1);
    for (size_t i = 0; i < node->Choices.size(); ++i) {
      const auto& edge = node->Choices[i];
      if (edge.Operand == "default") {
        default_index = i;
        continue;
      }
      bool match = false;

      if(vals.find(edge.Variable) != vals.end()){ 
        const json &val = vals.at(edge.Variable);
        if (!cmp(val, edge.Operand, edge.Value, &match)) {
          std::cerr << "choice: unsupported operand \"" << edge.Operand << "\"\n";
          cown->decided = true;
          failed->store(true, std::memory_order_release);
          return;
        }
      }
      else {
        auto res_it = resource_inputs.find(edge.Variable);
        if (res_it == resource_inputs.end() || !res_it->second.is_string()) {
          std::cerr << "choice: resource \"" << edge.Variable << "\" not found\n";
          failed->store(true, std::memory_order_release);
          continue;
        }
        const std::string runtime_name = res_it->second.get<std::string>();
        if (resource_map.find(runtime_name) == resource_map.end()) {
          std::cerr << "choice: resource \"" << runtime_name << "\" not found\n";
          failed->store(true, std::memory_order_release);
          continue;
        }
        detersl::types::Resource* resource = resource_map.at(runtime_name);
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
    when(read(cowns), control, read(guard->control), read(invocation.invocation_cown)) << [eval_choice, guard_edge, failed, can_abort](auto res, auto control, auto parent, auto ic) {
      if (!parent->decided || parent->selected != guard_edge || (can_abort && failed->load(std::memory_order_acquire)) ) {
        control->decided = true;
        control->selected = static_cast<size_t>(-1);
        return;
      }
      eval_choice(res, control);
    };
    return true;
  }

  when(read(cowns), control, read(invocation.invocation_cown)) << [eval_choice, failed, can_abort](auto res, auto control, auto ic) {
    if(can_abort && failed->load(std::memory_order_acquire)){
      control->decided = true;
      control->selected = static_cast<size_t>(-1);
      return;
    }
    eval_choice(res, control);
  };
  return true;
}

void Scheduling::schedule_commit_behaviour(detersl::types::WorkflowInvocation& invocation) {
  std::shared_ptr<detersl::metrics::InvocationMetrics> metrics = invocation.metrics;
  const cown_ptr<detersl::types::Resource> invocation_cown = invocation.invocation_cown;
  const std::shared_ptr<std::atomic<bool>>& failed = invocation.failed;
  if(!invocation.request.can_abort){
    when(invocation_cown) << [metrics](auto ic){
       metrics->complete();
    };
    return;
  }

  std::unordered_set<std::string>& workflow_rw_resources = invocation.workflow_rw_resources;
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
 
  cown_array<detersl::types::Resource> cowns{commit_cowns.data(), commit_cowns.size()};
  when(cowns, invocation_cown) << [failed, local_ref_counts, commit_resource_names, metrics, this](auto resources, auto ic) {
    const bool is_failed = failed->load(std::memory_order_acquire);
    for (size_t i = 0; i < resources.length; ++i) {
      auto& res = *resources.array[i];
      if (is_failed) {
        res.abort_uncommitted();
      } else {
        res.commit_uncommitted();
        if(res.is_deleted()){
          deleted_resources_queue.push({commit_resource_names[i], local_ref_counts.at(commit_resource_names[i])});
        }
      }
    }
    metrics->complete();
  };
  
}

bool Scheduling::schedule_graph(Node* node,
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

    if (node->Type == NodeType::Task) {
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
    } else if (node->Type == NodeType::Choice) {
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
      if (err) *err = "unknown node type";
      ok = false;
      break;
    }
  }

  if (!ok) {
    invocation.failed->store(true, std::memory_order_release); 
  }

  schedule_commit_behaviour(invocation);

   if(!invocation.request.can_abort){
    return true;
  }

  return ok;
}

bool Scheduling::register_wasm_function(const nlohmann::json& j, std::string* err)
{

  static rust::Box<FfiExecutioner> compile_exec = new_executioner(*engine, new_cpp_kv(new KVInterface()));

  // convert to WasmFuncInfo first to fill in defaults in missing fields
  detersl::func::WasmFuncInfo info = detersl::func::WasmFuncInfo::from_json(j);
  
  try {

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
                    
    // TODO: this is not function DSL, so we cannot pass it directly 
    // to the executioner. We have to create this after having
    // our own function DSL.
    compile_exec->executioner_compile_json(info.to_json().dump());

    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    
    
    detersl::func::WasmFuncInfo info = detersl::func::WasmFuncInfo::from_json(j);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Function " << info.func_name << " compiled in " << duration << " ms\n";
    
    wasm_func_registry[info.func_name] = std::move(info);

    return true;
  } catch (const rust::Error& e) {
    if (err) *err = e.what();
    return false;
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return false;
  }
}

bool Scheduling::register_workflow(const detersl::types::Workflow& workflow, std::string* err)
{
  if (workflow.ID.empty()) {
    if (err) *err = "workflow id is required";
    return false;
  }

  if (workflow_registry.find(workflow.ID) != workflow_registry.end()) {
    if (err) *err = "workflow already registered: " + workflow.ID;
    return false;
  }
  Node* root = BuildFromWorkflow(workflow, err);
  if (!root) return -1;
  workflow_registry.emplace(workflow.ID, root);
  return true;
}

bool Scheduling::invoke_workflow(const detersl::types::InvokeDTO& request, uint64_t& id, std::string* err)
{
  auto it = workflow_registry.find(request.WorkflowID);
  if (it == workflow_registry.end()) {
    if (err) *err = "unknown workflow id: " + request.WorkflowID;
    return false;
  }
  
  detersl::types::WorkflowInvocation invocation{
    .invocation_id = id,
    .workflow_resources = {},
    .workflow_rw_resources = {},
    .invocation_cown = make_cown<detersl::types::Resource>(),
    .failed = std::make_shared<std::atomic<bool>>(false),
    .metrics = std::make_shared<detersl::metrics::InvocationMetrics>(
        id,
        std::chrono::steady_clock::now(),
        invocation.failed,
        completion_cb_),
    .request = std::move(request)
  };
  
  //detersl::metrics::insert_invocation_metric(request.RequestID, invocation.metrics);
  
  // detersl::metrics::prune_completed_invocation_metrics();
  
  return schedule_graph(it->second, invocation, err);
}

void Scheduling::cleanup_resources()
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

bool Scheduling::get_resource(const std::string &res_name, std::future<rust::Vec<uint8_t>>& res_data) {
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
