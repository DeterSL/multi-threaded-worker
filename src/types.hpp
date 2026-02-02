#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace detersl {
    namespace types {
        using FunctionOutput = int;
        using FunctionInput = std::string;
        using FunctionType = FunctionOutput (*)(FunctionInput);

        struct IDsSpec {
            std::string From;        // json: "from,omitempty"
            std::string ValueType;   // "int" | "string"
            std::string Collection;  // "single" | "array"
        };

        // NOTE: unique_ptr requires explicit copy to make this type copyable.
        struct DataAccess {
            std::string Table;                       // json:"table"
            std::unique_ptr<IDsSpec> IDs;            // json:"ids,omitempty"

            DataAccess() = default;

            // Deep-copy constructor
            DataAccess(const DataAccess& other)
                : Table(other.Table)
                , IDs(other.IDs ? std::make_unique<IDsSpec>(*other.IDs) : nullptr) {}

            // Deep-copy assignment
            DataAccess& operator=(const DataAccess& other) {
                if (this == &other) return *this;
                Table = other.Table;
                IDs = other.IDs ? std::make_unique<IDsSpec>(*other.IDs) : nullptr;
                return *this;
            }

            // Move OK
            DataAccess(DataAccess&&) noexcept = default;
            DataAccess& operator=(DataAccess&&) noexcept = default;
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
            std::string Next;                          // json:"next"
        };

        struct State {
            std::string Type;                          // json:"type"
            std::string Resource;                      // json:"resource,omitempty"
            std::optional<int> FuncID;                 // json:"func_id,omitempty"
            std::map<std::string, std::string> Resources; // json:"resources,omitempty"

            std::vector<DataAccess> DataAccess;        // json:"data_access,omitempty"
            json Result;                               // json:"result,omitempty"
            std::string Next;                          // json:"next,omitempty"
            bool End{false};                           // json:"end,omitempty"
            std::vector<Choice> Choices;               // json:"choices,omitempty"
            std::string Default;                       // json:"default,omitempty"

            // NEW: Enriched runtime base config JSON (kept alongside resource for compatibility)
            std::string RuntimeBase;                   // json:"runtime_base,omitempty"

            // Match Go semantics: return the same underlying container (no copy)
            const std::vector<Choice>& ChoicesClean() const {
                return Choices;
            }
        };

        struct Workflow {
            std::string ID;                            // json:"id"
            std::string StartAt;                       // json:"start_at"
            std::map<std::string, State> States;       // json:"states"
        };

        struct DeployDTO {
            std::string StartAt;                       // json:"start_at"
            std::map<std::string, State> States;       // json:"states"
        };

        struct InvokeDTO {
            std::string WorkflowID;                    // json:"workflow_id"
            std::string Input;                         // json:"input"
        };

        struct WorkflowRequest {
            Workflow Workflow;                         // json:"workflow"
            std::string Input;                         // json:"input"
            std::string RequestID;                     // json:"request_id"
        };

        // ---------- nlohmann::json (de)serialization ----------

        inline void to_json(json& j, const IDsSpec& v) {
            j = json::object();
            if (!v.From.empty()) j["from"] = v.From;
            j["value_type"] = v.ValueType;
            j["collection"] = v.Collection;
        }

        inline void from_json(const json& j, IDsSpec& v) {
            if (j.contains("from")) v.From = j.at("from").get<std::string>();
            v.ValueType = j.at("value_type").get<std::string>();
            v.Collection = j.at("collection").get<std::string>();
        }

        inline void to_json(json& j, const DataAccess& v) {
            j = json::object();
            j["table"] = v.Table;
            if (v.IDs) j["ids"] = *v.IDs;
        }

        inline void from_json(const json& j, DataAccess& v) {
            v.Table = j.at("table").get<std::string>();
            if (j.contains("ids") && !j.at("ids").is_null()) {
                v.IDs = std::make_unique<IDsSpec>(j.at("ids").get<IDsSpec>());
            } else {
                v.IDs.reset();
            }
        }

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
            j["next"] = v.Next;
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
            v.Next = j.at("next").get<std::string>();
        }

        inline void to_json(json& j, const State& v) {
            j = json::object();
            j["type"] = v.Type;
            if (!v.Resource.empty())     j["resource"] = v.Resource;
            if (v.FuncID)                j["func_id"] = *v.FuncID;
            if (!v.Resources.empty())    j["resources"] = v.Resources;
            if (!v.DataAccess.empty())   j["data_access"] = v.DataAccess;
            if (!v.Result.is_null())     j["result"] = v.Result;
            if (!v.Next.empty())         j["next"] = v.Next;
            if (v.End)                   j["end"] = v.End;
            if (!v.Choices.empty())      j["choices"] = v.Choices;
            if (!v.Default.empty())      j["default"] = v.Default;
            if (!v.RuntimeBase.empty())  j["runtime_base"] = v.RuntimeBase;
        }

        inline void from_json(const json& j, State& v) {
            v.Type = j.at("type").get<std::string>();
            if (j.contains("resource"))      v.Resource = j.at("resource").get<std::string>();
            if (j.contains("func_id"))       v.FuncID = j.at("func_id").get<int>();
            if (j.contains("resources"))     v.Resources = j.at("resources").get<std::map<std::string, std::string>>();
            if (j.contains("data_access"))   v.DataAccess = j.at("data_access").get<std::vector<DataAccess>>();
            if (j.contains("result"))        v.Result = j.at("result");
            if (j.contains("next"))          v.Next = j.at("next").get<std::string>();
            if (j.contains("end"))           v.End = j.at("end").get<bool>();
            if (j.contains("choices"))       v.Choices = j.at("choices").get<std::vector<Choice>>();
            if (j.contains("default"))       v.Default = j.at("default").get<std::string>();
            if (j.contains("runtime_base"))  v.RuntimeBase = j.at("runtime_base").get<std::string>();
        }

        inline void to_json(json& j, const Workflow& v) {
            j = json::object();
            if (!v.ID.empty()) j["id"] = v.ID;
            j["start_at"] = v.StartAt;
            j["states"] = v.States;
        }

        inline void from_json(const json& j, Workflow& v) {
            if (j.contains("id")) v.ID = j.at("id").get<std::string>();
            v.StartAt = j.at("start_at").get<std::string>();
            v.States  = j.at("states").get<std::map<std::string, State>>();
        }

        inline void to_json(json& j, const DeployDTO& v) {
            j = json::object();
            j["start_at"] = v.StartAt;
            j["states"]   = v.States;
        }

        inline void from_json(const json& j, DeployDTO& v) {
            v.StartAt = j.at("start_at").get<std::string>();
            v.States  = j.at("states").get<std::map<std::string, State>>();
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
            j["workflow"]   = v.Workflow;
            j["input"]      = v.Input;
            j["request_id"] = v.RequestID;
        }

        inline void from_json(const json& j, WorkflowRequest& v) {
            v.Workflow  = j.at("workflow").get<Workflow>();
            v.Input     = j.at("input").get<std::string>();
            v.RequestID = j.at("request_id").get<std::string>();
        }
    }
}
