#include "utils.hpp"

using namespace detersl::worker;

namespace detersl::utils {

bool get_task_binding(Node* node, 
                    std::unordered_map<std::string, std::string> resources,
                    std::string* err){
    if (!node) {
        if (err) *err = "missing task node";
        return false;
    }

    for (const auto& entry : resources) {
        std::string parse_err;
        ResourceBinding cur{.local_name = entry.first};
        if (!detersl::utils::parse_resource_placeholder(
            entry.second, cur.immediate, cur.key, cur.read_only, cur.is_local, &parse_err)) {
            if (err) *err = parse_err;
            return false;
        }
        node->resource_bindings.push_back(cur);
    }
    return true;
}

bool get_choice_bindings(Node* node, std::string* err){
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
        std::string parse_err;
        ResourceBinding cur{.local_name = entry.Variable};
        if (!detersl::utils::parse_resource_placeholder(
            entry.Variable, cur.immediate, cur.key, cur.read_only, cur.is_local, &parse_err)) {
            if (err) *err = parse_err;
            return false;
        }
        node->resource_bindings.push_back(cur);
    }
    return true;
}

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
                              std::unordered_map<std::string, nlohmann::json>& resource_inputs,
                              std::vector<std::string>& resource_names,
                              std::unordered_set<std::string>* read_only,
                              std::unordered_map<std::string, nlohmann::json>& value_inputs,
                              std::string* err,
                              bool allow_variadic) {
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

    if (cur.is_local) {
      if (!invocation_resources.contains(cur.key)) {
        if (err) *err = "missing resource \"" + cur.key + "\" in invocation";
        return false;
      }
      const nlohmann::json& input_val = invocation_resources.at(cur.key);
      if (input_val.is_array()) {
        if (!allow_variadic) {
          if (err) *err = "resource \"" + cur.key + "\" must map to a string";
          return false;
        }
        nlohmann::json runtime_vals = nlohmann::json::array();
        for (const auto& entry : input_val) {
          if (!entry.is_string()) {
            if (err) *err = "resource \"" + cur.key + "\" array entries must be strings";
            return false;
          }
          const std::string runtime_name =
              std::to_string(invocation.invocation_id) + ":" + entry.get<std::string>();
          if (workflow_resources.find(runtime_name) == workflow_resources.end()) {
            workflow_resources[runtime_name] = make_cown<detersl::types::Resource>();
          }
          resource_names.push_back(runtime_name);
          runtime_vals.push_back(runtime_name);
          if (read_only && cur.read_only) {
            read_only->insert(runtime_name);
          }
        }
        resource_inputs[cur.local_name] = std::move(runtime_vals);
        continue;
      }
      if (!input_val.is_string()) {
        if (err) *err = "resource \"" + cur.key + "\" must map to a string or array of strings";
        return false;
      }
      const std::string runtime_name =
          std::to_string(invocation.invocation_id) + ":" + input_val.get<std::string>();
      if (workflow_resources.find(runtime_name) == workflow_resources.end()) {
        workflow_resources[runtime_name] = make_cown<detersl::types::Resource>();
      }
      resource_inputs[cur.local_name] = runtime_name;
      resource_names.push_back(runtime_name);
      if (read_only && cur.read_only) {
        read_only->insert(runtime_name);
      }
      continue;
    }

    {
      //global resource
      if (!invocation_resources.contains(cur.key)) {
        if (err) *err = "missing resource \"" + cur.key + "\" in invocation";
        return false;
      }

      const nlohmann::json& input_val = invocation_resources.at(cur.key);
      if (input_val.is_array()) {
        if (!allow_variadic) {
          if (err) *err = "resource \"" + cur.key + "\" must map to a string";
          return false;
        }
        for (const auto& entry : input_val) {
          if (!entry.is_string()) {
            if (err) *err = "resource \"" + cur.key + "\" array entries must be strings";
            return false;
          }
          const std::string runtime_name = entry.get<std::string>();
          resource_names.push_back(runtime_name);
          if (read_only && cur.read_only) {
            read_only->insert(runtime_name);
          }
        }
        resource_inputs[cur.local_name] = input_val;
        continue;
      }

      if (!input_val.is_string()) {
        if (err) *err = "resource \"" + cur.key + "\" must map to a string or array of strings";
        return false;
      }

      const std::string runtime_name = input_val.get<std::string>();
      resource_inputs[cur.local_name] = runtime_name;
      resource_names.push_back(runtime_name);
      if (read_only && cur.read_only) {
        read_only->insert(runtime_name);
      }
    }
  }
  return true;
}

}
