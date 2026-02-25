#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include "graph.hpp"
#include "types.hpp"

using namespace detersl::worker;

namespace detersl::utils {

static bool parse_resource_placeholder(const std::string& placeholder,
                                       bool &immediate_val,
                                       std::string& name,
                                       bool &read_only,
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
  immediate_val = false;
  name = key;
  read_only = (mode_char == 'r');
  return true;
}


bool resolve_choice_resources(const Node* node,
                              detersl::types::WorkflowInvocation& invocation,
                              std::unordered_map<std::string, std::string>& resolved,
                              std::unordered_map<std::string, json>& value_inputs,
                              std::string* err) {
  const nlohmann::json& invocation_resources = invocation.request.Input;
  const std::string &request_id = invocation.request.RequestID;
  std::unordered_map<std::string, cown_ptr<detersl::types::Resource>>& workflow_resources = invocation.workflow_resources;

  if (!node) {
    if (err) *err = "missing task node";
    return false;
  }

  std::unordered_set<std::string> seen;

  for(const auto& entry : node->Choices){
    if(seen.count(entry.Variable) > 0){
      continue;
    }

    seen.insert(entry.Variable);
    bool immediate_val = false;
    bool is_read_only = false; //not used since everything here is read_only anyways
    std::string key;
    std::string parse_err;
    if (!parse_resource_placeholder(entry.Variable, immediate_val, key, is_read_only, &parse_err)) {
      if (err) *err = parse_err;
      return false;
    }
    if (immediate_val) {
      if (!invocation_resources.contains(key)) {
        if (err) *err = "missing value resource \"" + key + "\" in invocation";
        return false;
      }
      value_inputs[entry.Variable] = invocation_resources[key];
      continue;
    }

    std::string runtime_name;
    if (key[0] != '_') {
      //global resource
      if (!invocation_resources.contains(key)) {
        if (err) *err = "missing resource \"" + key + "\" in invocation";
        return false;
      }
      if (!invocation_resources.at(key).is_string()) {
        if (err) *err = "resource \"" + key + "\" must map to a string";
        return false;
      }
      runtime_name = invocation_resources.at(key).get<std::string>();
    } else {
      //local_resource
      runtime_name = request_id + key;
      if (workflow_resources.find(runtime_name) == workflow_resources.end()) {
        workflow_resources[runtime_name] = make_cown<detersl::types::Resource>();
      }
    }

    resolved[entry.Variable] = runtime_name;  
  }
  
  return true;  
}


bool resolve_task_resources(const Node* node,
                              detersl::types::WorkflowInvocation& invocation,
                              std::unordered_map<std::string, std::string>& resolved,
                              std::unordered_set<std::string>& read_only,
                              std::unordered_map<std::string, nlohmann::json>& value_inputs,
                              std::string* err) {
  if (!node) {
    if (err) *err = "missing task node";
    return false;
  }
  nlohmann::json &invocation_resources = invocation.request.Input;
  std::unordered_map<std::string, cown_ptr<detersl::types::Resource>> &workflow_resources = invocation.workflow_resources;
  std::string &request_id = invocation.request.RequestID;

  for (const auto& entry : node->Resources) {
    bool immediate_val = false;
    bool is_read_only = false;
    std::string key;
    std::string parse_err;
    if (!parse_resource_placeholder(entry.second, immediate_val, key, is_read_only, &parse_err)) {
      if (err) *err = parse_err;
      return false;
    }

    if (immediate_val) {
      if (!invocation_resources.contains(key)) {
        if (err) *err = "missing value resource \"" + key + "\" in invocation";
        return false;
      }
      value_inputs[entry.first] = invocation_resources.at(key);
      continue;
    }

    std::string runtime_name;
    if (key[0] != '_') {
      //global resource
      if (!invocation_resources.contains(key)) {
        if (err) *err = "missing resource \"" + key + "\" in invocation";
        return false;
      }
      if (!invocation_resources.at(key).is_string()) {
        if (err) *err = "resource \"" + key + "\" must map to a string";
        return false;
      }
      runtime_name = invocation_resources.at(key).get<std::string>();
    } else {
      //local_resource
      runtime_name = request_id + key;
      if (workflow_resources.find(runtime_name) == workflow_resources.end()) {
        workflow_resources[runtime_name] = make_cown<detersl::types::Resource>();
      }
      
    }

    resolved[entry.first] = runtime_name;
    if (is_read_only) {
      read_only.insert(runtime_name);
    } 
  }
  return true;
}

}