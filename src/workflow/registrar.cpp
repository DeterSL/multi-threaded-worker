#include "workflow/registrar.hpp"

#include <chrono>
#include <iostream>

#include "execution/wasm/wasm_kv.hpp"

extern const rust::Box<DeterSLEngine> engine;

using json = nlohmann::json;

namespace detersl::worker {

FunctionRegistrar::FunctionRegistrar(FunctionRegistry& functions)
    : functions_(functions),
      compile_exec_(new_executioner(*engine, new_cpp_kv(new detersl::kv::WasmExecEnvKV()))) {}

bool FunctionRegistrar::register_wasm_function(const nlohmann::json& j, std::string* err) {
  detersl::func::WasmFuncInfo info;
  j.get_to(info);

  try {
    const auto start = std::chrono::high_resolution_clock::now();
    compile_exec_->executioner_compile_json(json(info).dump());
    const auto end = std::chrono::high_resolution_clock::now();

    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Function " << info.func_name << " compiled in " << duration << " ms"
              << std::endl;

    functions_.put(
        std::make_shared<const detersl::func::WasmFuncInfo>(std::move(info)));
    return true;
  } catch (const rust::Error& e) {
    if (err) {
      *err = e.what();
    }
    return false;
  } catch (const std::exception& e) {
    if (err) {
      *err = e.what();
    }
    return false;
  }
}

WorkflowRegistrar::WorkflowRegistrar(
    const FunctionRegistry& functions, WorkflowRegistry& workflows)
    : functions_(functions), workflows_(workflows) {}

bool WorkflowRegistrar::register_workflow(
    const detersl::types::Workflow& workflow, std::string* err) {
  if (workflow.ID.empty()) {
    if (err) {
      *err = "workflow id is required";
    }
    return false;
  }

  std::unique_ptr<Node> root = BuildFromWorkflow(workflow, err);
  if (!root) {
    return false;
  }

  if (!bind_functions(root.get(), err)) {
    return false;
  }

  WorkflowHandle handle(std::move(root));
  return workflows_.insert(workflow.ID, std::move(handle), err);
}

bool WorkflowRegistrar::bind_functions(Node* node, std::string* err) {
  if (!node) {
    return true;
  }

  if (node->Type == NodeType::Task) {
    auto func = functions_.find(node->FuncID);
    if (!func) {
      if (err) {
        *err = "unknown function: " + node->FuncID;
      }
      return false;
    }
    node->Func = std::move(func);
  }

  if (node->Next && !bind_functions(node->Next.get(), err)) {
    return false;
  }

  for (auto& edge : node->Choices) {
    if (!bind_functions(edge.Next.get(), err)) {
      return false;
    }
  }

  return true;
}

} // namespace detersl::worker
