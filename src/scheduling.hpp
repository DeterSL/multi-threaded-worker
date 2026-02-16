#pragma once

#include "func.hpp"
#include "graph.hpp"
#include "resource.hpp"
#include "runner.hpp"
#include "types.hpp"
#include <cpp/when.h>
#include <unordered_map>
#include <wasm-func.hpp>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::worker {

void cleanup_resources();

int register_wasm_function(const nlohmann::json& j, std::string* err, int* func_id);

int register_workflow(const detersl::types::Workflow& workflow, std::string* err);

bool invoke_workflow(const detersl::types::InvokeDTO& invoke, std::string* err);
} // namespace detersl::worker
