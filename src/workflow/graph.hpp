#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "storage/bytes.hpp"
#include "support/fast_json.hpp"
#include "workflow/model/workflow.hpp"

namespace detersl::func {
struct WasmFuncInfo;
}

namespace detersl::worker {

    struct Node;

    enum class NodeType : uint8_t {
        Task,
        Choice,
        Unknown
    };

    struct ResourceBinding {
        std::string local_name;
        std::string key;
        bool immediate = false;
        bool read_only = false;
        bool is_local = false;
    };

    struct ChoiceEdge {
        std::string Variable;
        std::string Operand;
        detersl::fastjson::Value Value;
        std::unique_ptr<Node> Next;
    };

    struct Node {
        std::string WorkflowID;
        std::string StateID;
        NodeType Type;
        std::string FuncID;
        std::shared_ptr<const detersl::func::WasmFuncInfo> Func;
        std::vector<ResourceBinding> resource_bindings;
        bool End{false};
        std::unique_ptr<Node> Next;
        std::vector<ChoiceEdge> Choices;
        std::string ID() const;
    };

    std::unique_ptr<Node> BuildFromWorkflow(const detersl::types::Workflow& request, std::string* err);

    bool cmp(const detersl::fastjson::InputField& actual, const std::string& operand, const detersl::fastjson::Value& rhs, bool* match);
    bool cmpBytes(const detersl::types::Bytes& actual, const std::string& operand, const detersl::fastjson::Value& rhs, bool* match);
} // namespace detersl::worker
