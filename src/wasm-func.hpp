#pragma once

#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "func.hpp"
#include "types.hpp"

namespace detersl::func {

inline void require_object(const nlohmann::json& j, const char* what) {
    if (!j.is_object()) throw std::runtime_error(std::string(what) + " must be a JSON object");
}

template <class T>
inline T require_field(const nlohmann::json& j, const char* key) {
    if (!j.contains(key)) {
        throw std::runtime_error(std::string("Missing required field: ") + key);
    }
    return j.at(key).get<T>();
}

struct FuncBinarySource {
    std::string type;
    std::string path;

    static FuncBinarySource from_json(const nlohmann::json& j) {
        require_object(j, "func_binary_source");
        FuncBinarySource s;
        s.type = require_field<std::string>(j, "type");
        s.path = require_field<std::string>(j, "path");
        return s;
    }

    nlohmann::json to_json() const {
        return nlohmann::json{
            {"type", type},
            {"path", path}
        };
    }
};

struct FuncInputEvent {
    std::string type;
    std::string data;

    static FuncInputEvent from_json(const nlohmann::json& j) {
        require_object(j, "func_input_event");
        FuncInputEvent e;
        e.type = require_field<std::string>(j, "type");
        e.data = require_field<std::string>(j, "data");
        return e;
    }

    nlohmann::json to_json() const {
        return nlohmann::json{
            {"type", type},
            {"data", data}
        };
    }
};

struct FuncOutputEvent {
    std::string type;

    static FuncOutputEvent from_json(const nlohmann::json& j) {
        require_object(j, "func_output_event");
        FuncOutputEvent e;
        e.type = require_field<std::string>(j, "type");
        return e;
    }

    nlohmann::json to_json() const {
        return nlohmann::json{
            {"type", type}
        };
    }
};

struct FuncLinkOpt {
    bool link_clocks = true;
    bool link_filesystem = true;
    bool link_random = true;
    bool link_cli = true;
    bool link_io = true;
    bool link_socket = true;

    static FuncLinkOpt from_json(const nlohmann::json& j) {
        require_object(j, "func_link_opt");
        FuncLinkOpt o;
        o.link_clocks     = j.value("link_clocks", true);
        o.link_filesystem = j.value("link_filesystem", true);
        o.link_random     = j.value("link_random", true);
        o.link_cli        = j.value("link_cli", true);
        o.link_io         = j.value("link_io", true);
        o.link_socket     = j.value("link_socket", true);
        return o;
    }

    nlohmann::json to_json() const {
        return nlohmann::json{
            {"link_clocks", link_clocks},
            {"link_filesystem", link_filesystem},
            {"link_random", link_random},
            {"link_cli", link_cli},
            {"link_io", link_io},
            {"link_socket", link_socket}
        };
    }
};

struct FuncExecutionPolicy {
    bool allow_clocks = true;
    bool allow_filesystem = false;
    bool allow_random = true;
    bool allow_cli = true;
    bool allow_socket = false;

    static FuncExecutionPolicy from_json(const nlohmann::json& j) {
        require_object(j, "func_execution_policy");
        FuncExecutionPolicy p;
        p.allow_clocks     = j.value("allow_clocks", true);
        p.allow_filesystem = j.value("allow_filesystem", false);
        p.allow_random     = j.value("allow_random", true);
        p.allow_cli        = j.value("allow_cli", true);
        p.allow_socket     = j.value("allow_socket", false);
        return p;
    }

    nlohmann::json to_json() const {
        return nlohmann::json{
            {"allow_clocks", allow_clocks},
            {"allow_filesystem", allow_filesystem},
            {"allow_random", allow_random},
            {"allow_cli", allow_cli},
            {"allow_socket", allow_socket}
        };
    }
};

struct FuncInitialValues {
    int init_clock = 0;
    int random_seed = 42;

    static FuncInitialValues from_json(const nlohmann::json& j) {
        require_object(j, "func_initial_values");
        FuncInitialValues v;
        v.init_clock  = j.value("init_clock", 0);
        v.random_seed = j.value("random_seed", 42);
        return v;
    }

    nlohmann::json to_json() const {
        return nlohmann::json{
            {"init_clock", init_clock},
            {"random_seed", random_seed}
        };
    }
};

struct WasmFuncInfo : public BasicFuncInfo {
    WasmFuncInfo() = default;

    explicit WasmFuncInfo(const BasicFuncInfo& base)
      : BasicFuncInfo(base) {}

    // matches config keys:
    std::string func_binary_hash;
    FuncBinarySource func_binary_source;

    FuncInputEvent func_input_event;
    FuncOutputEvent func_output_event;

    FuncLinkOpt func_link_opt;
    FuncExecutionPolicy func_execution_policy;

    FuncInitialValues func_initial_values;

    static WasmFuncInfo from_json(const nlohmann::json& j) {
        BasicFuncInfo base = BasicFuncInfo::from_json(j);
        WasmFuncInfo info(base);

        info.func_binary_hash = require_field<std::string>(j, "func_binary_hash");
        info.func_binary_source    = FuncBinarySource::from_json(j.at("func_binary_source"));
        info.func_input_event      = FuncInputEvent::from_json(j.at("func_input_event"));
        info.func_output_event     = FuncOutputEvent::from_json(j.at("func_output_event"));
        info.func_link_opt         = FuncLinkOpt::from_json(j.at("func_link_opt"));
        info.func_execution_policy = FuncExecutionPolicy::from_json(j.at("func_execution_policy"));
        info.func_initial_values   = FuncInitialValues::from_json(j.at("func_initial_values"));

        return info;
    }

    static WasmFuncInfo from_json(const std::string& json_text) {
        nlohmann::json j = nlohmann::json::parse(json_text);
        if (j.is_string()) {
            j = nlohmann::json::parse(j.get<std::string>());
        }
        return from_json(j);
    }

    nlohmann::json to_json() const {
        nlohmann::json j = to_json_base();

        j["func_binary_hash"] = func_binary_hash;
        j["func_binary_source"] = func_binary_source.to_json();

        j["func_input_event"] = func_input_event.to_json();
        j["func_output_event"] = func_output_event.to_json();

        j["func_link_opt"] = func_link_opt.to_json();
        j["func_execution_policy"] = func_execution_policy.to_json();

        j["func_initial_values"] = func_initial_values.to_json();

        return j;
    }
};

class WasmFunc {
public:
    explicit WasmFunc(WasmFuncInfo wasm_func_info)
      : wasm_func_info_(std::move(wasm_func_info)) {}

    static WasmFunc from_json(const nlohmann::json& j) {
        return WasmFunc(WasmFuncInfo::from_json(j));
    }

    static WasmFunc from_json(const std::string& json_text) {
        return WasmFunc(WasmFuncInfo::from_json(json_text));
    }

    nlohmann::json to_json() const {
        return wasm_func_info_.to_json();
    }

private:
    WasmFuncInfo wasm_func_info_;
};

} // namespace detersl::func

