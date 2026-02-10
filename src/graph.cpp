// src/worker/graph.cpp
#include "graph.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace detersl::worker {

using detersl::types::Workflow;
using detersl::types::WorkflowRequest;
using detersl::types::State;
using detersl::types::Choice;

static std::string lower(const std::string& s) {
        std::string t = s;
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        return t;
    }

    std::string Node::ID() const {
        if (!Resource.empty()) return Type + ":" + Resource;
        return Type;
    }

    static bool toOperandValue(const Choice& c, std::string& op, json& val, std::string& err) {
        if (c.NumericEq) { op = "=="; val = *c.NumericEq; return true; }
        if (c.NumericGT) { op = ">";  val = *c.NumericGT; return true; }
        if (c.NumericGTE){ op = ">="; val = *c.NumericGTE; return true; }
        if (c.NumericLT) { op = "<";  val = *c.NumericLT; return true; }
        if (c.NumericLTE){ op = "<="; val = *c.NumericLTE; return true; }
        if (c.StringEq)  { op = "=="; val = *c.StringEq; return true; }
        if (c.BoolEq)    { op = "bool"; val = *c.BoolEq; return true; }
        err = "no comparator set in choice rule";
        return false;
    }

    static Node* buildGraph(const std::string& key, const Workflow& wf,
                            std::map<std::string, Node*>& cache, std::string* err) {
        if (auto it = cache.find(key); it != cache.end()) return it->second;
        auto it = wf.States.find(key);
        if (it == wf.States.end()) {
            if (err) *err = "dangling reference to state \"" + key + "\"";
            return nullptr;
        }
        const State& st = it->second;
        Node* n = new Node{
            .WorkflowID = wf.ID,
            .Type = st.Type,
            .Resource = st.Resource,
            .FuncID = st.FuncID,
            .Resources = st.Resources,
            .End = st.End,
            .Input = "",
            .Result = st.Result.is_null() ? std::string{} : st.Result.dump(), // <--- FIX
            .Next = nullptr,
            .Choices = {},
            .RuntimeBase = st.RuntimeBase
        };
        cache[key] = n;

        const std::string t = lower(st.Type);
        if (t == "task" || t == "pass") {
            if (!st.Next.empty()) {
                Node* child = buildGraph(st.Next, wf, cache, err);
                if (!child) return nullptr;
                n->Next = child;
            }
        } else if (t == "choice") {
            std::vector<ChoiceEdge> edges;
            edges.reserve(st.Choices.size() + 1);
            for (const auto& c : st.Choices) {
                if (c.Variable.empty()) {
                    if (err) *err = "choice \"" + key + "\" rule missing variable";
                    return nullptr;
                }
                std::string op; json val;
                std::string terr;
                if (!toOperandValue(c, op, val, terr)) {
                    if (err) *err = "choice \"" + key + "\": " + terr;
                    return nullptr;
                }
                Node* child = buildGraph(c.Next, wf, cache, err);
                if (!child) return nullptr;

                edges.push_back(ChoiceEdge{c.Variable, op, val, child});
            }
            if (!st.Default.empty()) {
                Node* child = buildGraph(st.Default, wf, cache, err);
                if (!child) return nullptr;
                edges.push_back(ChoiceEdge{"","default",nullptr, child});
            }

            // Deterministic sorting like Go: by Next.ID(), then Variable, then Operand.
            std::sort(edges.begin(), edges.end(), [](const ChoiceEdge& a, const ChoiceEdge& b){
                const std::string idi = a.Next ? a.Next->ID() : "";
                const std::string idj = b.Next ? b.Next->ID() : "";
                if (idi == idj) {
                    if (a.Variable == b.Variable) return a.Operand < b.Operand;
                    return a.Variable < b.Variable;
                }
                return idi < idj;
            });
            n->Choices = std::move(edges);
        } else {
            if (err) *err = "unknown choice type \"" + st.Type + "\"";
            return nullptr;
        }
        return n;
    }

    Node* BuildFromWorkflow(const Workflow& workflow, std::string* err) {
        std::map<std::string, Node*> cache;
        Node* root = buildGraph(workflow.StartAt, workflow, cache, err);
        if (!root) {
            if (err) *err = "failed to build graph: " + *err;
            return nullptr;
        }
        return root;
    }

    static bool asNumber(const json& v, double* out) {
        if (v.is_number_float()) { *out = v.get<double>(); return true; }
        if (v.is_number_integer()) { *out = static_cast<double>(v.get<long long>()); return true; }
        if (v.is_string()) { try { *out = std::stod(v.get<std::string>()); return true; } catch(...){} }
        return false;
    }

    bool cmp(const json& actual, const std::string& operand, const json& rhs, bool* match) {
        if (operand == "==" || operand == "eq") {
            if (rhs.is_string()) { *match = actual.is_string() && actual.get<std::string>() == rhs.get<std::string>(); return true; }
            if (rhs.is_boolean()) { *match = actual.is_boolean() && actual.get<bool>() == rhs.get<bool>(); return true; }
            double af, rf;
            bool aok = asNumber(actual, &af), rok = asNumber(rhs, &rf);
            *match = aok && rok && (af == rf);
            return true;
        } else if (operand == "!=") {
            if (rhs.is_string()) { *match = actual.is_string() && actual.get<std::string>() != rhs.get<std::string>(); return true; }
            if (rhs.is_boolean()) { *match = actual.is_boolean() && actual.get<bool>() != rhs.get<bool>(); return true; }
            double af, rf;
            bool aok = asNumber(actual, &af), rok = asNumber(rhs, &rf);
            *match = aok && rok && (af != rf);
            return true;
        } else if (operand == ">" || operand == ">=" || operand == "<" || operand == "<=") {
            double af, rf;
            bool aok = asNumber(actual, &af), rok = asNumber(rhs, &rf);
            *match = false;
            if (!aok || !rok) return true; // not comparable => false
            if (operand == ">")  *match = af > rf;
            if (operand == ">=") *match = af >= rf;
            if (operand == "<")  *match = af < rf;
            if (operand == "<=") *match = af <= rf;
            return true;
        } else if (operand == "bool") {
            if (!rhs.is_boolean()) return false;
            *match = actual.is_boolean() && actual.get<bool>() == rhs.get<bool>();
            return true;
        } else if (operand == "default") {
            *match = true;
            return true;
        }
        return false;
    }

    bool readByPath(const json& input, const std::string& path, json* out) {
        std::string p = path;
        // Trim
        p.erase(p.begin(), std::find_if(p.begin(), p.end(), [](unsigned char c){ return !std::isspace(c);} ));
        p.erase(std::find_if(p.rbegin(), p.rend(), [](unsigned char c){ return !std::isspace(c);}).base(), p.end());

        if (p.empty() || p == "$") { *out = input; return true; }
        if (p.rfind("$.", 0) != 0) return false;

        const std::string rest = p.substr(2);
        auto parts = std::vector<std::string>{};
        size_t start = 0, pos;
        while ((pos = rest.find('.', start)) != std::string::npos) {
            parts.emplace_back(rest.substr(start, pos - start));
            start = pos + 1;
        }
        parts.emplace_back(rest.substr(start));

        json cur = input;
        for (const auto& seg : parts) {
            if (!cur.is_object()) return false;
            if (!cur.contains(seg)) return false;
            cur = cur[seg];
        }
        *out = cur;
        return true;
    }

    static bool get_child_count(const Node* node, size_t* count, std::string* err){
        if (!node || !count) {
            if (err) *err = "internal workflow scheduling error";
            return false;
        }

        std::string type_lower = node->Type;
        std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);
        if (type_lower == "task" || type_lower == "pass") {
            *count = node->Next ? 1 : 0;
            return true;
        }
        if (type_lower == "choice") {
            *count = node->Choices.size();
            return true;
        }

        if (err) *err = "unknown node type \"" + node->Type + "\"";
        return false;
    }

    static bool get_child_at(const Node* node, size_t index, const Node** child, std::string* err){
        if (!node || !child) {
            if (err) *err = "internal workflow scheduling error";
            return false;
        }

        std::string type_lower = node->Type;
        std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);
        if (type_lower == "task" || type_lower == "pass") {
            if (index != 0) {
            *child = nullptr;
            return true;
            }
            *child = node->Next;
            return true;
        }
        if (type_lower == "choice") {
            if (index >= node->Choices.size()) {
            *child = nullptr;
            return true;
            }
            *child = node->Choices[index].Next;
            return true;
        }

        if (err) *err = "unknown node type \"" + node->Type + "\"";
        return false;
    }

    bool detect_cycle(Node* root, std::string* err){
        std::unordered_set<const Node*> visiting;
        if (!root) return true;

        visiting.clear();
        std::unordered_set<const Node*> visited;

        struct Frame {
            const Node* node;
            size_t next_index;
        };

        std::vector<Frame> stack;
        stack.push_back({root, 0});
        visiting.insert(root);

        while (!stack.empty()) {
            Frame& frame = stack.back();
            const Node* node = frame.node;
            size_t child_count = 0;
            if (!get_child_count(node, &child_count, err)) return false;

            if (frame.next_index < child_count) {
            const Node* child = nullptr;
            if (!get_child_at(node, frame.next_index, &child, err)) return false;
            frame.next_index++;
            if (!child) continue;
            if (visited.count(child) != 0) continue;
            if (visiting.count(child) != 0) {
                if (err) *err = "cycle detected in workflow graph";
                return false;
            }
            visiting.insert(child);
            stack.push_back({child, 0});
            continue;
            }

            visiting.erase(node);
            visited.insert(node);
            stack.pop_back();
        }
        return true;
    }


} // namespace detersl::worker
