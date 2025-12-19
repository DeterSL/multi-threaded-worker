#pragma once

#include "models.hpp"
#include "runner.hpp"
#include <cpp/when.h>
#include <unordered_map>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl::worker {

using FunctionType = int (*)(std::string);

void schedule_function(FunctionState func_state);

int parse_and_load(const std::string& func_name);

void register_and_schedule();

void hardcoded_test();

void register_function(const std::string& name, FunctionType fn);

void clear_state_for_tests();

size_t resource_count_for_tests();

cown_ptr<Resource> get_cown_for_resource(const std::string& name);

} // namespace detersl::worker