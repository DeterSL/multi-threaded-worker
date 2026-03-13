// src/worker/graph.cpp
#include "graph.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include "utils.hpp"

namespace detersl::worker {

using detersl::types::Workflow;
using detersl::types::State;
using detersl::types::Choice;

static std::string lower(const std::string& s) {
        std::string t = s;
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        return t;
    }

    static NodeType getNodeType(const std::string& type) {
        const std::string t = lower(type);
        if (t == "task") return NodeType::Task;
        if (t == "choice") return NodeType::Choice;
        return NodeType::Unknown;
    }

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

    std::string Node::ID() const {
        return WorkflowID + ":" + StateID;
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

    static Node* buildSequence(const std::string& workflow_id,
                               const std::vector<State>& tasks,
                               std::string* err) {
        if (tasks.empty()) return nullptr;

        Node* next = nullptr;
        for (size_t idx = tasks.size(); idx-- > 0;) {
            const State& st = tasks[idx];
            const std::string state_id = std::to_string(idx);

            Node* n = new Node{
                .WorkflowID = workflow_id,
                .StateID = state_id,
                .Type = getNodeType(st.Type),
                .FuncID = st.FuncID,
                .End = false,
                .Next = nullptr,
                .Choices = {},
            };

            if (n->Type == NodeType::Task) {
                if(!get_task_binding(n, st.Resources, err)){
                    return nullptr;
                }
                n->Next = next;
                n->End = (next == nullptr);
            } else if (n->Type == NodeType::Choice) {
                if (idx + 1 != tasks.size()) {
                    if (err) *err = "choice at index " + state_id + " must be the last state in the list";
                    return nullptr;
                }
                if (st.Choices.empty()) {
                    if (err) *err = "choice at index " + state_id + " has no choices";
                    return nullptr;
                }
                std::vector<ChoiceEdge> edges;
                edges.reserve(st.Choices.size());
                for (const auto& c : st.Choices) {
                    if (c.Variable.empty()) {
                        if (err) *err = "choice at index " + state_id + " rule missing variable";
                        return nullptr;
                    }
                    std::string op; json val;
                    std::string terr;
                    if (!toOperandValue(c, op, val, terr)) {
                        if (err) *err = "choice at index " + state_id + ": " + terr;
                        return nullptr;
                    }
                    Node* child = buildSequence(workflow_id, c.tasks, err);
                    if (!child && err && !err->empty()) return nullptr;

                    edges.push_back(ChoiceEdge{c.Variable, op, val, child});
                }
                if (!st.Default.empty()) {
                    // Default branch ends with no additional tasks.
                    edges.push_back(ChoiceEdge{"", "default", nullptr, nullptr});
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
                if(!get_choice_bindings(n, err)){
                    return nullptr;
                }
            } else {
                if (err) *err = "unknown node type \"" + st.Type + "\"";
                return nullptr;
            }

            next = n;
        }
        return next;
    }

    Node* BuildFromWorkflow(const Workflow& workflow, std::string* err) {
        std::string local_err;
        std::string* errp = err ? err : &local_err;
        errp->clear();

        if (workflow.tasks.empty()) {
            *errp = "workflow has no tasks";
            return nullptr;
        }

        Node* root = buildSequence(workflow.ID, workflow.tasks, errp);
        if (!root) {
            if (!errp->empty()) {
                if (err) *err = "failed to build graph: " + *errp;
                return nullptr;
            }
            *errp = "workflow has no tasks";
            if (err) *err = "failed to build graph: " + *errp;
            return nullptr;
        }
        if (!errp->empty()) {
            if (err) *err = "failed to build graph: " + *errp;
            return nullptr;
        }
        return root;
    }

    static bool asNumber(const json& v, uint32_t* out) {
        if (v.is_number_float()) { *out = static_cast<uint32_t>(v.get<double>()); return true; }
        if (v.is_number_integer()) { *out = static_cast<uint32_t>(v.get<long long>()); return true; }
        if (v.is_string()) { try { *out = std::stoi(v.get<std::string>()); return true; } catch(...){} }
        return false;
    }

    bool cmp(const json& actual, const std::string& operand, const json& rhs, bool* match) {
        if (operand == "==" || operand == "eq") {
            if (rhs.is_string()) { *match = actual.is_string() && actual.get<std::string>() == rhs.get<std::string>(); return true; }
            if (rhs.is_boolean()) { *match = actual.is_boolean() && actual.get<bool>() == rhs.get<bool>(); return true; }
            uint32_t af, rf;
            bool aok = asNumber(actual, &af), rok = asNumber(rhs, &rf);
            *match = aok && rok && (af == rf);
            return true;
        } else if (operand == "!=") {
            if (rhs.is_string()) { *match = actual.is_string() && actual.get<std::string>() != rhs.get<std::string>(); return true; }
            if (rhs.is_boolean()) { *match = actual.is_boolean() && actual.get<bool>() != rhs.get<bool>(); return true; }
            uint32_t af, rf;
            bool aok = asNumber(actual, &af), rok = asNumber(rhs, &rf);
            *match = aok && rok && (af != rf);
            return true;
        } else if (operand == ">" || operand == ">=" || operand == "<" || operand == "<=") {
            uint32_t af, rf;
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

    bool cmpBytes(const detersl::types::Bytes& actual, const std::string& operand, const json& rhs, bool* match) {
        const auto& vec = actual.as_vec();

        auto bytes_equal_raw = [&vec](const uint8_t* data, size_t size) -> bool {
            //strict equality only: same size and same bytes
            if (vec.size() != size) return false;
            if (size == 0) return true;
            return std::memcmp(vec.data(), data, size) == 0;
        };

        auto bytes_to_bool = [&vec](bool* out) -> bool {
            // interpret the bytes as a boolean: either a single byte of value 0 or 1, or the ASCII string "0" or "1"
            if (vec.empty()) return false;

            const uint8_t* data = vec.data();
            const size_t size = vec.size();

            if (size == 1) {
                if (data[0] == 0 || data[0] == 1) { *out = (data[0] == 1); return true; }
                if (data[0] == '0' || data[0] == '1') { *out = (data[0] == '1'); return true; }
            }
            return false;
        };

        auto bytes_to_number = [&vec](uint32_t* out) -> bool {
            //TODO : fix this to handle integers other than 32 bit unsigned little endian
            if (vec.empty()) return false;
            memcpy(out, vec.data(), 4);
            return true;
        };

        if (operand == "==" || operand == "eq" || operand == "!=") {
            bool eq = false;
            bool comparable = true;

            if (rhs.is_string()) {
                const auto& rhs_str = rhs.get_ref<const std::string&>();
                eq = bytes_equal_raw(reinterpret_cast<const uint8_t*>(rhs_str.data()), rhs_str.size());
            } else if (rhs.is_boolean()) {
                bool bv = false;
                if (!bytes_to_bool(&bv)) {
                    comparable = false;
                } else {
                    eq = (bv == rhs.get<bool>());
                }
            } 
            else {
                uint32_t rf = 0;
                uint32_t af = 0;
                if (!asNumber(rhs, &rf) || !bytes_to_number(&af)) {
                    comparable = false;
                } else {
                    eq = (af == rf);
                }
            }

            if (!comparable) { *match = false; return true; }
            *match = (operand == "!=") ? !eq : eq;
            return true;
        } else if (operand == ">" || operand == ">=" || operand == "<" || operand == "<=") {
            uint32_t rf = 0;
            uint32_t af = 0;
            if (!asNumber(rhs, &rf) || !bytes_to_number(&af)) { *match = false; return true; }
            if (operand == ">")  *match = af > rf;
            if (operand == ">=") *match = af >= rf;
            if (operand == "<")  *match = af < rf;
            if (operand == "<=") *match = af <= rf;
            return true;
        } else if (operand == "bool") {
            if (!rhs.is_boolean()) return false;
            bool bv = false;
            if (!bytes_to_bool(&bv)) { *match = false; return true; }
            *match = bv == rhs.get<bool>();
            return true;
        } else if (operand == "default") {
            *match = true;
            return true;
        }
        return false;
    }
} // namespace detersl::worker