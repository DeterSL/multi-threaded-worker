#pragma once
#include <unordered_map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include <iostream>
#include <cpp/when.h>
#include <verona.h>
#include "resource.hpp"
#include <unordered_set>
#include "metrics.hpp"

using namespace verona::rt;
using namespace verona::cpp;

using json = nlohmann::json;

namespace detersl::metrics {
    struct InvocationMetrics;
}

namespace detersl {
    namespace types {
        struct Choice;

        struct State {
            std::string Type;                          // json:"type"
            std::string FuncID;                 // json:"func_id,omitempty"
            std::unordered_map<std::string, std::string> Resources; // json:"resources,omitempty"
            std::vector<Choice> Choices;               // json:"choices,omitempty"
            std::string Default;                       // json:"default,omitempty"
            // Match Go semantics: return the same underlying container (no copy)
            const std::vector<Choice>& ChoicesClean() const {
                return Choices;
            }
        };

        struct Choice {
            std::string Variable;                      // json:"variable"
            std::optional<double> NumericEq;           // json:"numeric_eq"
            std::optional<double> NumericGT;           // json:"numeric_gt"
            std::optional<double> NumericGTE;          // json:"numeric_gte"
            std::optional<double> NumericLT;           // json:"numeric_lt"
            std::optional<double> NumericLTE;          // json:"numeric_lte"
            std::optional<std::string> StringEq;       // json:"string_eq"
            std::optional<bool> BoolEq;                // json:"bool_eq"
            std::vector<State> tasks;                 // json:"tasks"
        };

        struct Workflow {
            std::string ID;                            // json:"id"
            std::vector<State> tasks;                 // json:"tasks"
        };

        struct WorkflowRequest {
            std::string WorkflowID;                   // json:"workflow_id"
            nlohmann::json Input;                         // json:"input"
            bool can_abort;                             // json:"can_abort"
            std::string RequestID;                     // json:"request_id"
        };

        struct InvokeDTO {
            std::string WorkflowID;                    // json:"workflow_id"
            bool can_abort = true;                      // json:"can_abort"
            nlohmann::json Input;                         // json:"input"
        };

        struct ChoiceControl {
            bool decided = false;
            size_t selected = 0;
        };

        struct BranchGuard {
            cown_ptr<ChoiceControl> control;
            size_t edge_index;
        };

        struct WorkflowInvocation {
            uint64_t invocation_id;
            std::unordered_map<std::string, cown_ptr<detersl::types::Resource>> workflow_resources;
            std::unordered_set<std::string> workflow_rw_resources;
            cown_ptr<detersl::types::Resource> invocation_cown;
            std::shared_ptr<std::atomic<bool>> failed = std::make_shared<std::atomic<bool>>(false);
            std::shared_ptr<detersl::metrics::InvocationMetrics> metrics;
            InvokeDTO request;
        };

        struct WorkflowStatus {
            bool done = false;
            bool failed = false;
            int64_t latency_ms = -1;
            int64_t completed_at_ms = -1;
        };

        struct MetricEvent {
            uint64_t request_id;
            bool failed{false};
            int64_t latency_ms{-1};
            int64_t completed_at{-1};
        };

        // ---------- nlohmann::json (de)serialization ----------
        inline void to_json(json& j, const State& v);
        inline void from_json(const json& j, State& v);

        inline void to_json(json& j, const Choice& v) {
            j = json::object();
            j["variable"] = v.Variable;
            if (v.NumericEq)  j["numeric_eq"]  = *v.NumericEq;
            if (v.NumericGT)  j["numeric_gt"]  = *v.NumericGT;
            if (v.NumericGTE) j["numeric_gte"] = *v.NumericGTE;
            if (v.NumericLT)  j["numeric_lt"]  = *v.NumericLT;
            if (v.NumericLTE) j["numeric_lte"] = *v.NumericLTE;
            if (v.StringEq)   j["string_eq"]   = *v.StringEq;
            if (v.BoolEq)     j["bool_eq"]     = *v.BoolEq;
            j["tasks"] = v.tasks;
        }

        inline void from_json(const json& j, Choice& v) {
            v.Variable   = j.at("variable").get<std::string>();
            if (j.contains("numeric_eq"))  v.NumericEq  = j.at("numeric_eq").get<double>();
            if (j.contains("numeric_gt"))  v.NumericGT  = j.at("numeric_gt").get<double>();
            if (j.contains("numeric_gte")) v.NumericGTE = j.at("numeric_gte").get<double>();
            if (j.contains("numeric_lt"))  v.NumericLT  = j.at("numeric_lt").get<double>();
            if (j.contains("numeric_lte")) v.NumericLTE = j.at("numeric_lte").get<double>();
            if (j.contains("string_eq"))   v.StringEq   = j.at("string_eq").get<std::string>();
            if (j.contains("bool_eq"))     v.BoolEq     = j.at("bool_eq").get<bool>();
            v.tasks = j.at("tasks").get<std::vector<State>>();
        }

        inline void to_json(json& j, const State& v) {
            j = json::object();
            j["type"] = v.Type;
            if (!v.FuncID.empty())       j["func_id"] = v.FuncID;
            if (!v.Resources.empty())    j["resources"] = v.Resources;
            if (!v.Choices.empty())      j["choices"] = v.Choices;
            if (!v.Default.empty())      j["default"] = v.Default;
        }

        inline void from_json(const json& j, State& v) {
            v.Type = j.at("type").get<std::string>();
            if (j.contains("func_id"))       v.FuncID = j.at("func_id").get<std::string>();
            if (j.contains("resources"))     v.Resources = j.at("resources").get<std::unordered_map<std::string, std::string>>();
            if (j.contains("choices"))       v.Choices = j.at("choices").get<std::vector<Choice>>();
            if (j.contains("default"))       v.Default = j.at("default").get<std::string>();
        }

        inline void to_json(json& j, const Workflow& v) {
            j = json::object();
            if (!v.ID.empty()) j["id"] = v.ID;
            j["tasks"] = v.tasks;
        }

        inline void from_json(const json& j, Workflow& v) {
            if (j.contains("id")) v.ID = j.at("id").get<std::string>();
            v.tasks  = j.at("tasks").get<std::vector<State>>();
        }

        inline void to_json(json& j, const InvokeDTO& v) {
            j = json::object();
            j["workflow_id"] = v.WorkflowID;
            j["can_abort"]   = v.can_abort;
            j["input"]       = v.Input;
        }

        inline void from_json(const json& j, InvokeDTO& v) {
            v.WorkflowID = j.at("workflow_id").get<std::string>();
            v.Input      = j.at("input").get<nlohmann::json>();
            if(j.contains("can_abort")) v.can_abort = j.at("can_abort").get<bool>();
        }

        inline void to_json(json& j, const WorkflowRequest& v) {
            j = json::object();
            j["workflow_id"]   = v.WorkflowID;
            j["input"]      = v.Input;
            j["request_id"] = v.RequestID;
            j["can_abort"]   = v.can_abort;
        }

        inline void from_json(const json& j, WorkflowRequest& v) {
            v.WorkflowID  = j.at("workflow_id").get<std::string>();
            v.Input     = j.at("input").get<nlohmann::json>();
            v.RequestID = j.at("request_id").get<std::string>();
            if(j.contains("can_abort")) v.can_abort = j.at("can_abort").get<bool>();
        }

        inline void to_json(json& j, const MetricEvent& v) {
            j = json::object();
            j["request_id"]   = v.request_id;
            j["failed"]      = v.failed;
            j["latency_ms"] = v.latency_ms;
            j["completed_at"]   = v.completed_at;
        }

        inline void from_json(const json& j, MetricEvent& v) {
            v.request_id  = j.at("request_id").get<uint64_t>();
            v.failed     = j.at("failed").get<bool>();
            v.latency_ms = j.at("latency_ms").get<int64_t>();
            v.completed_at = j.at("completed_at").get<int64_t>();
        }
    }
}
