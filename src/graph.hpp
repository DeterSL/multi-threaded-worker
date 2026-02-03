#pragma once
#include "types.hpp"
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <algorithm>
#include <cmath>
#include <iostream>

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
        std::string Type;
        std::string Resource;
        std::optional<int> FuncID;
        std::map<std::string, std::string> Resources;
        bool End{false};
        std::string Input;   // raw JSON string
        std::string Result;  // raw JSON string
        Node* Next{nullptr};
        std::vector<ChoiceEdge> Choices;

        std::string RuntimeBase;

        std::string ID() const;
    };

    Node* BuildFromWorkflow(const detersl::types::Workflow& request, std::string* err);

    int Advance(Node** root, std::string* err); // 0 ok, -1 error; sets *root possibly null

    // helpers exposed for tests
    bool readByPath(const json& input, const std::string& path, json* out);
    bool cmp(const json& actual, const std::string& operand, const json& rhs, bool* match);
} // namespace detersl::worker
