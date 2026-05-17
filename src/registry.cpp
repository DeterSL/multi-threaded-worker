#include "registry.hpp"

namespace detersl::worker {

void FunctionRegistry::put(FuncHandle fn) {
  if (!fn) {
    return;
  }

  std::unique_lock lock(mu_);
  by_name_[fn->func_name] = std::move(fn);
}

FuncHandle FunctionRegistry::find(const std::string& name) const {
  std::shared_lock lock(mu_);
  auto it = by_name_.find(name);
  if (it == by_name_.end()) {
    return {};
  }
  return it->second;
}

bool WorkflowRegistry::insert(const std::string& id, WorkflowHandle root, std::string* err) {
  std::unique_lock lock(mu_);
  auto [it, inserted] = by_id_.emplace(id, std::move(root));
  if (!inserted) {
    if (err) {
      *err = "workflow already registered: " + id;
    }
    return false;
  }
  return true;
}

WorkflowHandle WorkflowRegistry::find(const std::string& id) const {
  std::shared_lock lock(mu_);
  auto it = by_id_.find(id);
  if (it == by_id_.end()) {
    return {};
  }
  return it->second;
}

} // namespace detersl::worker
