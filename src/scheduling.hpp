#pragma once

#include "func.hpp"
#include "resource.hpp"
#include "runner.hpp"
#include "types.hpp"
#include <cpp/when.h>
#include <unordered_map>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::worker {

void schedule_function(detersl::func::BasicFuncInfo func_state);

int parse_and_load(const std::string& func_name);

void register_and_schedule();

void hardcoded_test();

void register_function(const std::string& name, detersl::types::FunctionType fn);

void cleanup_resources();
} // namespace detersl::worker
