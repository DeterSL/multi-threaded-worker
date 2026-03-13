#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include "graph.hpp"
#include "types.hpp"

using namespace detersl::worker;

namespace detersl::utils {

bool parse_resource_placeholder(const std::string& placeholder,
                                       bool &immediate_val,
                                       std::string& name,
                                       bool &read_only,
                                       bool &is_local,
                                       std::string* err); 

bool resolve_resources(const Node* node,
                              detersl::types::WorkflowInvocation& invocation,
                              std::unordered_map<std::string, std::string>& resolved,
                              std::unordered_set<std::string>* read_only,
                              std::unordered_map<std::string, nlohmann::json>& value_inputs,
                              std::string* err); 

}