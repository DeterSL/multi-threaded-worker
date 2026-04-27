#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace detersl::func {

struct BasicFuncInfo {
    BasicFuncInfo()
      : func_name("not_specified"),
        func_invocation_id("no id"){}

    std::string func_name;
    std::string func_invocation_id;
    std::vector<std::string> resources;
    std::unordered_set<std::string> read_only_resources;
};

inline void from_json(const nlohmann::json& j, BasicFuncInfo& v) {
    if (!j.is_object()) {
        throw std::runtime_error("BasicFuncInfo::from_json expected a JSON object");
    }

    v.func_name = j.at("func_name").get<std::string>();
    v.func_invocation_id = j.value("func_invocation_id", "no id");
    v.resources = j.value("resources", std::vector<std::string>{});
    v.read_only_resources = j.value("read_only_resources", std::unordered_set<std::string>{});
}

inline void to_json(nlohmann::json& j, const BasicFuncInfo& v) {
    j = nlohmann::json{
        {"func_name", v.func_name},
        {"func_invocation_id", v.func_invocation_id},
        {"resources", v.resources},
        {"read_only_resources", v.read_only_resources}
    };
}

} // namespace detersl::func
