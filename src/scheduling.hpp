#pragma once

#include "func.hpp"
#include "resource.hpp"
#include "runner.hpp"
#include "types.hpp"
#include <cpp/when.h>
#include <unordered_map>
#include <wasm-func.hpp>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::worker {

void schedule_function(detersl::func::BasicFuncInfo func_state);

void schedule_function(detersl::func::WasmFuncInfo func_info, detersl::func::WasmFunc func);

int parse_and_load(const std::string& func_name);

void register_and_schedule();

void hardcoded_test();

void register_function(const std::string& name, detersl::types::FunctionType fn);

void cleanup_resources();
} // namespace detersl::worker
