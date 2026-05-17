#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace detersl::types {

using json = nlohmann::json;

struct Choice;

struct State {
  std::string Type;
  std::string FuncID;
  std::unordered_map<std::string, std::string> Resources;
  std::vector<Choice> Choices;
  std::string Default;

  const std::vector<Choice>& ChoicesClean() const {
    return Choices;
  }
};

struct Choice {
  std::string Variable;
  std::optional<double> NumericEq;
  std::optional<double> NumericGT;
  std::optional<double> NumericGTE;
  std::optional<double> NumericLT;
  std::optional<double> NumericLTE;
  std::optional<std::string> StringEq;
  std::optional<bool> BoolEq;
  std::vector<State> tasks;
};

struct Workflow {
  std::string ID;
  std::vector<State> tasks;
};

struct WorkflowRequest {
  std::string WorkflowID;
  nlohmann::json Input;
  bool can_abort;
  std::string RequestID;
};

struct InvokeDTO {
  std::string WorkflowID;
  bool can_abort = true;
  nlohmann::json Input;
};

inline void to_json(json& j, const State& v);
inline void from_json(const json& j, State& v);

inline void to_json(json& j, const Choice& v) {
  j = json::object();
  j["variable"] = v.Variable;
  if (v.NumericEq) {
    j["numeric_eq"] = *v.NumericEq;
  }
  if (v.NumericGT) {
    j["numeric_gt"] = *v.NumericGT;
  }
  if (v.NumericGTE) {
    j["numeric_gte"] = *v.NumericGTE;
  }
  if (v.NumericLT) {
    j["numeric_lt"] = *v.NumericLT;
  }
  if (v.NumericLTE) {
    j["numeric_lte"] = *v.NumericLTE;
  }
  if (v.StringEq) {
    j["string_eq"] = *v.StringEq;
  }
  if (v.BoolEq) {
    j["bool_eq"] = *v.BoolEq;
  }
  j["tasks"] = v.tasks;
}

inline void from_json(const json& j, Choice& v) {
  v.Variable = j.at("variable").get<std::string>();
  if (j.contains("numeric_eq")) {
    v.NumericEq = j.at("numeric_eq").get<double>();
  }
  if (j.contains("numeric_gt")) {
    v.NumericGT = j.at("numeric_gt").get<double>();
  }
  if (j.contains("numeric_gte")) {
    v.NumericGTE = j.at("numeric_gte").get<double>();
  }
  if (j.contains("numeric_lt")) {
    v.NumericLT = j.at("numeric_lt").get<double>();
  }
  if (j.contains("numeric_lte")) {
    v.NumericLTE = j.at("numeric_lte").get<double>();
  }
  if (j.contains("string_eq")) {
    v.StringEq = j.at("string_eq").get<std::string>();
  }
  if (j.contains("bool_eq")) {
    v.BoolEq = j.at("bool_eq").get<bool>();
  }
  v.tasks = j.at("tasks").get<std::vector<State>>();
}

inline void to_json(json& j, const State& v) {
  j = json::object();
  j["type"] = v.Type;
  if (!v.FuncID.empty()) {
    j["func_id"] = v.FuncID;
  }
  if (!v.Resources.empty()) {
    j["resources"] = v.Resources;
  }
  if (!v.Choices.empty()) {
    j["choices"] = v.Choices;
  }
  if (!v.Default.empty()) {
    j["default"] = v.Default;
  }
}

inline void from_json(const json& j, State& v) {
  v.Type = j.at("type").get<std::string>();
  if (j.contains("func_id")) {
    v.FuncID = j.at("func_id").get<std::string>();
  }
  if (j.contains("resources")) {
    v.Resources = j.at("resources").get<std::unordered_map<std::string, std::string>>();
  }
  if (j.contains("choices")) {
    v.Choices = j.at("choices").get<std::vector<Choice>>();
  }
  if (j.contains("default")) {
    v.Default = j.at("default").get<std::string>();
  }
}

inline void to_json(json& j, const Workflow& v) {
  j = json::object();
  if (!v.ID.empty()) {
    j["id"] = v.ID;
  }
  j["tasks"] = v.tasks;
}

inline void from_json(const json& j, Workflow& v) {
  if (j.contains("id")) {
    v.ID = j.at("id").get<std::string>();
  }
  v.tasks = j.at("tasks").get<std::vector<State>>();
}

inline void to_json(json& j, const InvokeDTO& v) {
  j = json::object();
  j["workflow_id"] = v.WorkflowID;
  j["can_abort"] = v.can_abort;
  j["input"] = v.Input;
}

inline void from_json(const json& j, InvokeDTO& v) {
  v.WorkflowID = j.at("workflow_id").get<std::string>();
  v.Input = j.at("input").get<nlohmann::json>();
  if (j.contains("can_abort")) {
    v.can_abort = j.at("can_abort").get<bool>();
  }
}

inline void to_json(json& j, const WorkflowRequest& v) {
  j = json::object();
  j["workflow_id"] = v.WorkflowID;
  j["input"] = v.Input;
  j["request_id"] = v.RequestID;
  j["can_abort"] = v.can_abort;
}

inline void from_json(const json& j, WorkflowRequest& v) {
  v.WorkflowID = j.at("workflow_id").get<std::string>();
  v.Input = j.at("input").get<nlohmann::json>();
  v.RequestID = j.at("request_id").get<std::string>();
  if (j.contains("can_abort")) {
    v.can_abort = j.at("can_abort").get<bool>();
  }
}

}  // namespace detersl::types
