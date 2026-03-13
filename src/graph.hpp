#pragma once
#include "types.hpp"
#include "bytes.hpp"
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include "wasm-func.hpp"

using json = nlohmann::json;

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
        json Value; // nlohmann json to hold any
        Node* Next{nullptr};
    };

    struct Node {
        std::string WorkflowID;
        std::string StateID;
        NodeType Type;
        std::string FuncID;
        std::vector<ResourceBinding> resource_bindings;
        bool End{false};
        Node* Next{nullptr};
        std::vector<ChoiceEdge> Choices;
        std::string ID() const;
    };

    Node* BuildFromWorkflow(const detersl::types::Workflow& request, std::string* err);

    bool cmp(const json& actual, const std::string& operand, const json& rhs, bool* match);
    bool cmpBytes(const detersl::types::Bytes& actual, const std::string& operand, const json& rhs, bool* match);
} // namespace detersl::worker
