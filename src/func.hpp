#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace detersl {
    namespace func {
        struct BasicFuncInfo {
            BasicFuncInfo() :
                func_name("not_specified"),
                func_invocation_id("no id"),
                input("nothing"),
                output("nothing"),
                resources(),
                started(false),
                finished(false) {}
                                
            std::string func_name;
            std::string func_invocation_id;
            std::string input;
            std::string output;
            std::vector<std::string> resources;
            bool started;
            bool finished;


            static BasicFuncInfo from_json(const std::string& json) {
                auto j = nlohmann::json::parse(json);

                BasicFuncInfo info;
                info.func_name = j.value("func_name", "not_specified");
                info.func_invocation_id = j.value("func_invocation_id", "no id");
                info.input = j.value("input", "nothing");
                info.output = j.value("output", "nothing");
                info.resources = j.value("resources", std::vector<std::string>());
                info.started = j.value("started", false);
                info.finished = j.value("finished", false);

                return info;
            }
            
        };
    }
}
