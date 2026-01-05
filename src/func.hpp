#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace detersl::func {

struct BasicFuncInfo {
    BasicFuncInfo()
      : func_name("not_specified"),
        func_invocation_id("no id"),
        resources(),
        started(false),
        finished(false) {}

    std::string func_name;
    std::string func_invocation_id;
    std::vector<std::string> resources;
    bool started;
    bool finished;

    static BasicFuncInfo from_json(const nlohmann::json& j) {
        if (!j.is_object()) {
            throw std::runtime_error("BasicFuncInfo::from_json expected a JSON object");
        }

        BasicFuncInfo info;

        info.func_name = j.at("func_name").get<std::string>();
        info.func_invocation_id = j.value("func_invocation_id", "no id");
        info.resources = j.value("resources", std::vector<std::string>{});
        info.started   = j.value("started", false);
        info.finished  = j.value("finished", false);

        return info;
    }

    static BasicFuncInfo from_json(const std::string& json_text) {
        nlohmann::json j = nlohmann::json::parse(json_text);
        if (j.is_string()) {
            j = nlohmann::json::parse(j.get<std::string>());
        }
        return from_json(j);
    }

    nlohmann::json to_json_base() const {
        nlohmann::json j;
        j["func_name"] = func_name;
        j["func_invocation_id"] = func_invocation_id;

        return j;
    }
};

} // namespace detersl::func

