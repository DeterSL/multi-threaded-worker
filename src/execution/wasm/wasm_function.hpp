#pragma once

#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "execution/function.hpp"

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
};

inline void from_json(const nlohmann::json& j, FuncBinarySource& v) {
    require_object(j, "func_binary_source");
    v.type = require_field<std::string>(j, "type");
    v.path = require_field<std::string>(j, "path");
}

inline void to_json(nlohmann::json& j, const FuncBinarySource& v) {
    j = nlohmann::json{
        {"type", v.type},
        {"path", v.path}
    };
}

struct FuncInputEvent {
    std::string type;
    std::string data;
};

inline void from_json(const nlohmann::json& j, FuncInputEvent& v) {
    v.type = j.value("type", "data");
    if (j.contains("data")) v.data = j.at("data").get<std::string>();
}

inline void to_json(nlohmann::json& j, const FuncInputEvent& v) {
    j = nlohmann::json{
        {"type", v.type},
        {"data", v.data}
    };
}

struct FuncOutputEvent {
    std::string type;
};

inline void from_json(const nlohmann::json& j, FuncOutputEvent& v) {
    v.type = j.value("type", "default");
}

inline void to_json(nlohmann::json& j, const FuncOutputEvent& v) {
    j = nlohmann::json{
        {"type", v.type}
    };
}

struct FuncLinkOpt {
    bool link_clocks = true;
    bool link_filesystem = true;
    bool link_random = true;
    bool link_cli = true;
    bool link_io = true;
    bool link_socket = true;
};

inline void from_json(const nlohmann::json& j, FuncLinkOpt& v) {
    v.link_clocks     = j.value("link_clocks", true);
    v.link_filesystem = j.value("link_filesystem", true);
    v.link_random     = j.value("link_random", true);
    v.link_cli        = j.value("link_cli", true);
    v.link_io         = j.value("link_io", true);
    v.link_socket     = j.value("link_socket", true);
}

inline void to_json(nlohmann::json& j, const FuncLinkOpt& v) {
    j = nlohmann::json{
        {"link_clocks", v.link_clocks},
        {"link_filesystem", v.link_filesystem},
        {"link_random", v.link_random},
        {"link_cli", v.link_cli},
        {"link_io", v.link_io},
        {"link_socket", v.link_socket}
    };
}

struct FuncExecutionPolicy {
    bool allow_clocks = true;
    bool allow_filesystem = false;
    bool allow_random = true;
    bool allow_cli = true;
    bool allow_socket = false;
};

inline void from_json(const nlohmann::json& j, FuncExecutionPolicy& v) {
    v.allow_clocks     = j.value("allow_clocks", true);
    v.allow_filesystem = j.value("allow_filesystem", false);
    v.allow_random     = j.value("allow_random", true);
    v.allow_cli        = j.value("allow_cli", true);
    v.allow_socket     = j.value("allow_socket", false);
}

inline void to_json(nlohmann::json& j, const FuncExecutionPolicy& v) {
    j = nlohmann::json{
        {"allow_clocks", v.allow_clocks},
        {"allow_filesystem", v.allow_filesystem},
        {"allow_random", v.allow_random},
        {"allow_cli", v.allow_cli},
        {"allow_socket", v.allow_socket}
    };
}

struct FuncInitialValues {
    int init_clock = 0;
    int random_seed = 42;
};

inline void from_json(const nlohmann::json& j, FuncInitialValues& v) {
    v.init_clock  = j.value("init_clock", 0);
    v.random_seed = j.value("random_seed", 42);
}

inline void to_json(nlohmann::json& j, const FuncInitialValues& v) {
    j = nlohmann::json{
        {"init_clock", v.init_clock},
        {"random_seed", v.random_seed}
    };
}

struct WasmFuncInfo : public BasicFuncInfo {
    WasmFuncInfo() = default;
   
    explicit WasmFuncInfo(const BasicFuncInfo& base)
      : BasicFuncInfo(base) {}

    // matches config keys:
    std::string func_binary_hash;
    bool fast_execution = false;
    FuncBinarySource func_binary_source;

    FuncInputEvent func_input_event;
    FuncOutputEvent func_output_event;

    FuncLinkOpt func_link_opt;
    FuncExecutionPolicy func_execution_policy;

    FuncInitialValues func_initial_values;
};

inline void from_json(const nlohmann::json& j, WasmFuncInfo& v) {
    j.get_to(static_cast<BasicFuncInfo&>(v));

    v.func_binary_hash      = require_field<std::string>(j, "func_binary_hash");
    v.fast_execution        = j.value("fast_execution", true);
    v.func_binary_source    = j.contains("func_binary_source") ? j.at("func_binary_source").get<FuncBinarySource>() : FuncBinarySource();
    v.func_input_event      = j.contains("func_input_event") ? j.at("func_input_event").get<FuncInputEvent>() : FuncInputEvent();
    v.func_output_event     = j.contains("func_output_event") ? j.at("func_output_event").get<FuncOutputEvent>() : FuncOutputEvent();
    v.func_link_opt         = j.contains("func_link_opt") ? j.at("func_link_opt").get<FuncLinkOpt>() : FuncLinkOpt();
    v.func_execution_policy = j.contains("func_execution_policy") ? j.at("func_execution_policy").get<FuncExecutionPolicy>() : FuncExecutionPolicy();
    v.func_initial_values   = j.contains("func_initial_values") ? j.at("func_initial_values").get<FuncInitialValues>() : FuncInitialValues();
}

inline void to_json(nlohmann::json& j, const WasmFuncInfo& v) {
    j = nlohmann::json(static_cast<const BasicFuncInfo&>(v));

    j["func_binary_hash"] = v.func_binary_hash;
    j["fast_execution"] = v.fast_execution;
    j["func_binary_source"] = v.func_binary_source;

    j["func_input_event"] = v.func_input_event;
    j["func_output_event"] = v.func_output_event;

    j["func_link_opt"] = v.func_link_opt;
    j["func_execution_policy"] = v.func_execution_policy;

    j["func_initial_values"] = v.func_initial_values;
}
} // namespace detersl::func
