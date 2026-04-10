#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "graph.hpp"
#include "types.hpp"

using namespace detersl::worker;

namespace detersl::utils {

bool get_task_binding(Node* node, 
                std::unordered_map<std::string, std::string> resources,
                std::string* err);
    
bool get_choice_bindings(Node* node, std::string* err);
    
bool parse_resource_placeholder(const std::string& placeholder,
                                       bool &immediate_val,
                                       std::string& name,
                                       bool &read_only,
                                       bool &is_local,
                                       std::string* err); 

bool resolve_resources(const Node* node,
                              detersl::types::WorkflowInvocation& invocation,
                              std::unordered_map<std::string, nlohmann::json>& resource_inputs,
                              std::vector<std::string>& resource_names,
                              std::unordered_set<std::string>* read_only,
                              std::unordered_map<std::string, nlohmann::json>& value_inputs,
                              std::string* err,
                              bool allow_variadic); 

}
