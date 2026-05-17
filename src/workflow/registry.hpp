#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "execution/wasm/wasm_function.hpp"
#include "workflow/graph.hpp"

namespace detersl::worker {

using FuncHandle = std::shared_ptr<const detersl::func::WasmFuncInfo>;
using WorkflowHandle = std::shared_ptr<const Node>;

class FunctionRegistry {
public:
  void put(FuncHandle fn);
  FuncHandle find(const std::string& name) const;

private:
  mutable std::shared_mutex mu_;
  std::unordered_map<std::string, FuncHandle> by_name_;
};

class WorkflowRegistry {
public:
  bool insert(const std::string& id, WorkflowHandle root, std::string* err);
  WorkflowHandle find(const std::string& id) const;

private:
  mutable std::shared_mutex mu_;
  std::unordered_map<std::string, WorkflowHandle> by_id_;
};

} // namespace detersl::worker
