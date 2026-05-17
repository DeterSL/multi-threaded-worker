#pragma once

#include <nlohmann/json.hpp>

#include "ffi.rs.h"
#include "workflow/model/workflow.hpp"
#include "workflow/registry.hpp"

namespace detersl::worker {

class FunctionRegistrar {
public:
  explicit FunctionRegistrar(FunctionRegistry& functions);

  bool register_wasm_function(const nlohmann::json& j, std::string* err);

private:
  FunctionRegistry& functions_;
  rust::Box<FfiExecutioner> compile_exec_;
};

class WorkflowRegistrar {
public:
  WorkflowRegistrar(const FunctionRegistry& functions, WorkflowRegistry& workflows);

  bool register_workflow(const detersl::types::Workflow& workflow, std::string* err);

private:
  bool bind_functions(Node* node, std::string* err);

  const FunctionRegistry& functions_;
  WorkflowRegistry& workflows_;
};

} // namespace detersl::worker
