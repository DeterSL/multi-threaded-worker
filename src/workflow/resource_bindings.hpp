#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "support/fast_json.hpp"
#include "workflow/graph.hpp"
#include "workflow/model/invocation.hpp"

namespace detersl::utils {

bool get_task_binding(detersl::worker::Node* node,
                std::unordered_map<std::string, std::string> resources,
                std::string* err);
    
bool get_choice_bindings(detersl::worker::Node* node, std::string* err);
    
bool parse_resource_placeholder(const std::string& placeholder,
                                       bool &immediate_val,
                                       std::string& name,
                                       bool &read_only,
                                       bool &is_local,
                                       std::string* err); 

bool resolve_resources(const detersl::worker::Node* node,
                              detersl::types::WorkflowInvocation& invocation,
                              detersl::fastjson::ResourceInputs& resource_inputs,
                              std::vector<std::string>& resource_names,
                              std::unordered_set<std::string>* read_only,
                              detersl::fastjson::ValueInputs& value_inputs,
                              std::string* err,
                              bool allow_variadic); 

}
