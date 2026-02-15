#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include <iostream>
#include <cpp/when.h>
#include <verona.h>

using namespace verona::rt;
using namespace verona::cpp;

using json = nlohmann::json;

namespace detersl {
    namespace types {
        using FunctionOutput = int;
        using FunctionInput = std::string;
        using FunctionType = FunctionOutput (*)(FunctionInput);

        struct Choice;

        struct State {
            std::string Type;                          // json:"type"
            std::optional<int> FuncID;                 // json:"func_id,omitempty"
            std::map<std::string, std::string> Resources; // json:"resources,omitempty"
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
            std::vector<State> States;                 // json:"states"
        };

        struct Workflow {
            std::string ID;                            // json:"id"
            std::vector<State> States;                 // json:"states"
        };

        struct DeployDTO {
            std::string StartAt;                       // json:"start_at"
            std::vector<State> States;                 // json:"states"
        };

        struct InvokeDTO {
            std::string WorkflowID;                    // json:"workflow_id"
            std::string Input;                         // json:"input"
        };

        struct WorkflowRequest {
            std::string WorkflowID;                   // json:"workflow_id"
            std::string Input;                         // json:"input"
            std::string RequestID;                     // json:"request_id"
        };

        struct ChoiceControl {
            bool decided = false;
            size_t selected = 0;

            ~ChoiceControl(){
                std::cout << "ChoiceControl destructor called" << std::endl;
            }
        };

        struct BranchGuard {
            cown_ptr<ChoiceControl> control;
            size_t edge_index;
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
            j["states"] = v.States;
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
            v.States = j.at("states").get<std::vector<State>>();
        }

        inline void to_json(json& j, const State& v) {
            j = json::object();
            j["type"] = v.Type;
            if (v.FuncID)                j["func_id"] = *v.FuncID;
            if (!v.Resources.empty())    j["resources"] = v.Resources;
            if (!v.Choices.empty())      j["choices"] = v.Choices;
            if (!v.Default.empty())      j["default"] = v.Default;
        }

        inline void from_json(const json& j, State& v) {
            v.Type = j.at("type").get<std::string>();
            if (j.contains("func_id"))       v.FuncID = j.at("func_id").get<int>();
            if (j.contains("resources"))     v.Resources = j.at("resources").get<std::map<std::string, std::string>>();
            if (j.contains("choices"))       v.Choices = j.at("choices").get<std::vector<Choice>>();
            if (j.contains("default"))       v.Default = j.at("default").get<std::string>();
        }

        inline void to_json(json& j, const Workflow& v) {
            j = json::object();
            if (!v.ID.empty()) j["id"] = v.ID;
            j["states"] = v.States;
        }

        inline void from_json(const json& j, Workflow& v) {
            if (j.contains("id")) v.ID = j.at("id").get<std::string>();
            v.States  = j.at("states").get<std::vector<State>>();
        }

        inline void to_json(json& j, const DeployDTO& v) {
            j = json::object();
            j["start_at"] = v.StartAt;
            j["states"]   = v.States;
        }

        inline void from_json(const json& j, DeployDTO& v) {
            v.StartAt = j.at("start_at").get<std::string>();
            v.States  = j.at("states").get<std::vector<State>>();
        }

        inline void to_json(json& j, const InvokeDTO& v) {
            j = json::object();
            j["workflow_id"] = v.WorkflowID;
            j["input"]       = v.Input;
        }

        inline void from_json(const json& j, InvokeDTO& v) {
            v.WorkflowID = j.at("workflow_id").get<std::string>();
            v.Input      = j.at("input").get<std::string>();
        }

        inline void to_json(json& j, const WorkflowRequest& v) {
            j = json::object();
            j["workflow_id"]   = v.WorkflowID;
            j["input"]      = v.Input;
            j["request_id"] = v.RequestID;
        }

        inline void from_json(const json& j, WorkflowRequest& v) {
            v.WorkflowID  = j.at("workflow_id").get<std::string>();
            v.Input     = j.at("input").get<std::string>();
            v.RequestID = j.at("request_id").get<std::string>();
        }
    }
}
