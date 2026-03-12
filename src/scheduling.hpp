#pragma once

#include "func.hpp"
#include "graph.hpp"
#include "resource.hpp"
#include "runner.hpp"
#include "types.hpp"
#include <cpp/when.h>
#include <unordered_map>
#include <wasm-func.hpp>
#include "rust/cxx.h"
#include <future>
#include <cstdint>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::worker {

void cleanup_resources();

int register_wasm_function(const nlohmann::json& j, std::string* err);

int register_workflow(const detersl::types::Workflow& workflow, std::string* err);

bool invoke_workflow(const detersl::types::InvokeDTO& invoke, std::string* err, std::string* request_id = nullptr);

bool get_resource(const std::string& res_name,  std::future<rust::Vec<uint8_t>>& res_data);

bool get_workflow_status(const std::string& request_id, detersl::types::WorkflowStatus* status);

} // namespace detersl::worker
