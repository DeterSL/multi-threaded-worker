#include "workflow/scheduler.hpp"

#include "execution/wasm/wasm_runner.hpp"
#include "workflow/resource_bindings.hpp"

#include <deque>
#include <iostream>
#include <utility>

namespace detersl::worker {

using namespace verona::cpp;

Scheduling::Scheduling(const WorkflowRegistry& workflows) : workflows_(workflows) {}

void Scheduling::schedule_function(const detersl::func::WasmFuncInfo& func_info,
                                      detersl::types::WorkflowInvocation& invocation,
                                      const detersl::types::BranchGuard* guard)
{
  const std::shared_ptr<std::atomic<bool>>& failed = invocation.failed;
  const bool can_abort = invocation.request.can_abort;

  std::vector<cown_ptr<detersl::types::Resource>> read_write_resources;
  std::vector<cown_ptr<detersl::types::Resource>> read_only_resources;

  std::unordered_map<std::string, int> local_ref_counts;

  for(auto & res_name : func_info.resources){
    auto workflow_it = invocation.workflow_resources.find(res_name);
    if(workflow_it != invocation.workflow_resources.end()){
      // Resource is a workflow-scoped resource
      if(func_info.read_only_resources.find(res_name) != func_info.read_only_resources.end()){
        read_only_resources.push_back(workflow_it->second);
      }
      else{
        read_write_resources.push_back(workflow_it->second);
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
    read_write_resources.size() ? read_write_resources.data() : nullptr,
    read_write_resources.size()
  };
  cown_array<detersl::types::Resource> ro_cowns{
    read_only_resources.size() ? read_only_resources.data() : nullptr,
    read_only_resources.size()
  };

  auto run = [func_info = std::move(func_info),
              local_refs = std::move(local_ref_counts),
              can_abort,
              failed,
              globals = invocation.workflow_rw_resources,
              this](auto rw_c, auto ro_c){
    detersl::runner::WasmRunner runner(rw_c, ro_c, func_info);

    bool ok = runner.run();

    if(!can_abort){
      // In non-aborting mode, each function finalizes only its own write-set.
      for(auto& res: func_info.resources){
        if(func_info.read_only_resources.count(res)){
          //is read only resource
          continue;
        }
        detersl::types::Resource* res_ptr = runner.storage->get_resource(res);
        if(res_ptr){
          if(ok){
          res_ptr->commit_uncommitted();
          if(globals.count(res) && res_ptr->is_deleted()){
            // push to deleted resource queue for cleanup 
            deleted_resources_queue.push({res, local_refs.at(res)});
          }
          } else {
            res_ptr->abort_uncommitted();
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
    const int guard_edge = guard->edge_index;
    when(rw_cowns, read(ro_cowns), read(guard_cown), read(invocation.invocation_cown)) << [guard_edge, run, failed, can_abort](auto rw_c, auto ro_c, auto guard_c, auto ic) {  
      if (guard_c->selected != guard_edge || (failed->load(std::memory_order_acquire) && can_abort) ) {
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

bool Scheduling::run_task_node(const Node* node,
                          detersl::types::WorkflowInvocation& invocation,
                          const detersl::types::BranchGuard* guard,
                          std::string* err) {
  if (!node) {
    if (err) *err = "task missing function";
    return false;
  }
  if (!node->Func) {
    if (err) *err = "unbound function " + node->FuncID;
    return false;
  }

  detersl::func::WasmFuncInfo func_info = *node->Func;

  detersl::fastjson::ValueInputs value_inputs;
  detersl::fastjson::ResourceInputs resource_inputs;
  if (!detersl::utils::resolve_resources(node, invocation, resource_inputs, func_info.resources,
                                         &func_info.read_only_resources, value_inputs, err, true)) {
    return false;
  }

  func_info.func_input_event.data = detersl::fastjson::dump_task_input(resource_inputs, value_inputs);
  schedule_function(func_info, invocation, guard);
  return true;
}

bool Scheduling::schedule_choice_node(const Node* node,
                                 detersl::types::WorkflowInvocation& invocation,
                                 cown_ptr<detersl::types::ChoiceControl> control,
                                 const detersl::types::BranchGuard* guard,
                                 std::string* err)
{  
  const bool can_abort = invocation.request.can_abort;

  detersl::fastjson::ValueInputs value_inputs;
  detersl::fastjson::ResourceInputs resource_inputs;
  std::vector<std::string> resource_names;
  if (!detersl::utils::resolve_resources(node, invocation, resource_inputs, resource_names, nullptr, value_inputs, err, false)) {
    return false;
  }

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
  
  auto eval_choice = [node,
                      workflow_root = invocation.workflow_root,
                      resource_inputs = std::move(resource_inputs),
                      vals = std::move(value_inputs),
                      res_names = std::move(resource_names),
                      failed](auto local_resources, auto &cown) {
    std::unordered_map<std::string, detersl::types::Resource*> resource_map;

    for (size_t i = 0; i < res_names.size(); ++i) { 
      resource_map[res_names[i]] = const_cast<detersl::types::Resource*>(&(*local_resources.array[i]));
    }

    int default_index = -1;
    for (size_t i = 0; i < node->Choices.size(); ++i) {
      const auto& edge = node->Choices[i];
      if (edge.Operand == "default") {
        default_index = static_cast<int>(i);
        continue;
      }
      bool match = false;

      if(vals.find(edge.Variable) != vals.end()){ 
        const detersl::fastjson::InputField& val = vals.at(edge.Variable);
        if (!cmp(val, edge.Operand, edge.Value, &match)) {
          std::cerr << "choice: unsupported operand \"" << edge.Operand << "\"\n";
          cown->selected = -1;
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
        const std::string runtime_name = res_it->second.get_string();
        if (resource_map.find(runtime_name) == resource_map.end()) {
          std::cerr << "choice: resource \"" << runtime_name << "\" not found\n";
          failed->store(true, std::memory_order_release);
          continue;
        }
        detersl::types::Resource* resource = resource_map.at(runtime_name);
        if (!cmpBytes(resource->get_data(), edge.Operand, edge.Value, &match)) {
          std::cerr << "choice: unsupported operand \"" << edge.Operand << "\"\n";
          cown->selected = -1;
          failed->store(true, std::memory_order_release);
          return;
        }   
      }
      if (match) {
        cown->selected = static_cast<int>(i);
        return;
      }
    }

    cown->selected = default_index;
  };

  if (guard) {
    const int guard_edge = guard->edge_index;
    when(read(cowns), control, read(guard->control), read(invocation.invocation_cown)) << [eval_choice, guard_edge, failed, can_abort](auto res, auto control, auto parent, auto ic) {
      if (parent->selected != guard_edge || (can_abort && failed->load(std::memory_order_acquire)) ) {
        control->selected = -1;
        return;
      }
      eval_choice(res, control);
    };
    return true;
  }

  when(read(cowns), control, read(invocation.invocation_cown)) << [eval_choice, failed, can_abort](auto res, auto control, auto ic) {
    if(can_abort && failed->load(std::memory_order_acquire)){
      control->selected = -1;
      return;
    }
    eval_choice(res, control);
  };
  return true;
}

void Scheduling::schedule_commit_behaviour(detersl::types::WorkflowInvocation& invocation) {
  std::shared_ptr<detersl::status::InvocationStatus> status = invocation.status;
  const cown_ptr<detersl::types::Resource> invocation_cown = invocation.invocation_cown;
  const std::shared_ptr<std::atomic<bool>>& failed = invocation.failed;
  if(!invocation.request.can_abort){
    when(invocation_cown) << [status](auto ic){
      status->complete();
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

  if (commit_cowns.empty()) {
    when(invocation_cown) << [status](auto ic) {
      status->complete();
    };
    return;
  }

  cown_array<detersl::types::Resource> cowns{commit_cowns.data(), commit_cowns.size()};
  when(cowns, invocation_cown) << [failed, local_ref_counts, commit_resource_names, status, this](auto resources, auto ic) {
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
    status->complete();
  };
}

bool Scheduling::schedule_graph(const Node* node,
                          detersl::types::WorkflowInvocation& invocation,
                          std::string* err)
{
  if (!node) return true;

  struct QueueItem {
    const Node* node;
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

    if (item.node->Type == NodeType::Task) {
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
      queue.push_back({item.node->Next.get(), item.guard});
    } else if (item.node->Type == NodeType::Choice) {
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
        detersl::types::BranchGuard next_guard{control, static_cast<int>(i)};
        queue.push_back({edge.Next.get(), next_guard});
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

bool Scheduling::invoke_workflow(
    detersl::fastjson::InvokeRequest request,
    const uint64_t& id,
    std::string* err)
{
  auto root = workflows_.find(request.WorkflowID);
  if (!root) {
    if (err) *err = "unknown workflow id: " + request.WorkflowID;
    return false;
  }

  auto failed = std::make_shared<std::atomic<bool>>(false);
  auto status = std::make_shared<detersl::status::InvocationStatus>(id, failed);
  
  detersl::types::WorkflowInvocation invocation{
    .invocation_id = id,
    .workflow_resources = {},
    .workflow_rw_resources = {},
    .invocation_cown = make_cown<detersl::types::Resource>(),
    .failed = std::move(failed),
    .status = std::move(status),
    .workflow_root = root,
    .request = std::move(request)
  }; 

  return schedule_graph(root.get(), invocation, err);
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

bool Scheduling::get_resource_async(
    const std::string& res_name,
    std::function<void(const rust::Vec<uint8_t>&)> on_ready) {
  auto it = resource_map.find(res_name);

  if (it == resource_map.end()) {
    return false;
  }
  auto res = it->second.first;

  when(read(res)) << [on_ready = std::move(on_ready)](auto r){
    on_ready(r->get_data().as_vec());
  };

  return true;
}
} // namespace detersl::worker
