#include "utils.hpp"

using namespace detersl::worker;

namespace detersl::utils {

bool parse_resource_placeholder(const std::string& placeholder,
                                       bool &immediate_val,
                                       std::string& name,
                                       bool &read_only,
                                       bool &is_local,
                                       std::string* err) {
                                        
  auto trim_copy = [](std::string input) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    input.erase(input.begin(), std::find_if(input.begin(), input.end(), not_space));
    input.erase(std::find_if(input.rbegin(), input.rend(), not_space).base(), input.end());
    return input;
  };
  
  const auto trimmed = trim_copy(placeholder);
  if (trimmed.size() < 2) {
    if (err) *err = "invalid resource placeholder: " + placeholder;
    return false;
  }
  const char lead = trimmed[0];
  if (lead != '$' && lead != '&') {
    if (err) *err = "unsupported placeholder scope: " + placeholder;
    return false;
  }
  size_t start = 1;
  if (start >= trimmed.size()) {
    if (err) *err = "empty resource key in placeholder: " + placeholder;
    return false;
  }

  if (lead == '$') {
    const std::string key = trimmed.substr(start);
    if (key.empty()) {
      if (err) *err = "empty value key in placeholder: " + placeholder;
      return false;
    }
    immediate_val = true;
    name = key;
    return true;
  }

  const auto colon_pos = trimmed.rfind(':');
  if (colon_pos == std::string::npos || colon_pos + 2 != trimmed.size()) {
    const std::string key = trimmed.substr(start);
    if (key.empty()) {
      if (err) *err = "empty value key in placeholder: " + placeholder;
      return false;
    }
    immediate_val = false;
    name = key;
    return true;
  }
  const char mode_char = trimmed[colon_pos + 1];
  if (mode_char != 'r' && mode_char != 'w') {
    if (err) *err = "unsupported access mode in placeholder: " + placeholder;
    return false;
  }
  const std::string key = trimmed.substr(start, colon_pos - start);
  if (key.empty()) {
    if (err) *err = "empty resource key in placeholder: " + placeholder;
    return false;
  }
  is_local = key[0] == '_';
  immediate_val = false;
  name = key;
  read_only = (mode_char == 'r');
  return true;
}

bool resolve_resources(const Node* node,
                              detersl::types::WorkflowInvocation& invocation,
                              std::unordered_map<std::string, std::string>& resolved,
                              std::unordered_set<std::string>* read_only,
                              std::unordered_map<std::string, nlohmann::json>& value_inputs,
                              std::string* err) {
  if (!node) {
    if (err) *err = "missing task node";
    return false;
  }
  nlohmann::json &invocation_resources = invocation.request.Input;
  std::unordered_map<std::string, cown_ptr<detersl::types::Resource>> &workflow_resources = invocation.workflow_resources;

  for (const ResourceBinding &cur : node->resource_bindings) {
    if (cur.immediate) {
      if (!invocation_resources.contains(cur.key)) {
        if (err) *err = "missing value resource \"" + cur.key + "\" in invocation";
        return false;
      }
      value_inputs[cur.local_name] = invocation_resources.at(cur.key);
      continue;
    }

    std::string runtime_name;
    if (cur.is_local) {
      //local_resource
      runtime_name = invocation.request.RequestID + ":" + cur.key;
      if (workflow_resources.find(runtime_name) == workflow_resources.end()) {
        workflow_resources[runtime_name] = make_cown<detersl::types::Resource>();
      }
      
    } else {
      //global resource
      if (!invocation_resources.contains(cur.key)) {
        if (err) *err = "missing resource \"" + cur.key + "\" in invocation";
        return false;
      }
      if (!invocation_resources.at(cur.key).is_string()) {
        if (err) *err = "resource \"" + cur.key + "\" must map to a string";
        return false;
      }
      runtime_name = invocation_resources.at(cur.key).get<std::string>();
      
    }

    resolved[cur.local_name] = runtime_name;
    if (read_only && cur.read_only) {
      read_only->insert(runtime_name);
    } 
  }
  return true;
}

}