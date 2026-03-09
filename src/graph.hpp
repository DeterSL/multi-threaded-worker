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

using json = nlohmann::json;

namespace detersl::worker {

    struct Node;

    struct ChoiceEdge {
        std::string Variable;
        std::string Operand;
        json Value; // nlohmann json to hold any
        Node* Next{nullptr};
    };

    struct Node {
        std::string WorkflowID;
        std::string StateID;
        std::string Type;
        std::string FuncID;
        std::map<std::string, std::string> Resources;
        bool End{false};
        Node* Next{nullptr};
        std::vector<ChoiceEdge> Choices;
        std::string ID() const;
    };

    Node* BuildFromWorkflow(const detersl::types::Workflow& request, std::string* err);

    bool cmp(const json& actual, const std::string& operand, const json& rhs, bool* match);
    bool cmpBytes(const detersl::types::Bytes& actual, const std::string& operand, const json& rhs, bool* match);
} // namespace detersl::worker
