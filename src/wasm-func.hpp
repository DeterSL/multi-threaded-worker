#pragma once

#include "func.hpp"
#include "types.hpp"

namespace detersl {
    namespace func {

        struct WasmFuncInfo : public BasicFuncInfo {

            WasmFuncInfo() : func_hash("nothing"),
                                func_clock_init(0),
                                func_random_seed(42) {}

            WasmFuncInfo(const BasicFuncInfo& basic_info)
                : BasicFuncInfo(basic_info),
                    func_hash("nothing"),
                    func_clock_init(0),
                    func_random_seed(42) {}

            std::string func_hash;
            int func_clock_init;
            int func_random_seed;

            static WasmFuncInfo from_json(const nlohmann::json& j) {
    BasicFuncInfo base = BasicFuncInfo::from_json(j);

    WasmFuncInfo info(base);
    info.func_hash = j.value("func_hash", "nothing");
    info.func_clock_init = j.value("func_clock_init", 0);
    info.func_random_seed = j.value("func_random_seed", 42);
    return info;
}

            nlohmann::json to_json() const {
                nlohmann::json j = {
                    { "func_hash", func_hash },
                    { "func_clock_init", func_clock_init },
                    { "func_random_seed", func_random_seed }
                };

                j.merge_patch({
                    { "func_name", func_name },
                    { "func_invocation_id", func_invocation_id },
                    { "input", input },
                    { "output", output },
                    { "resources", resources },
                    { "started", started },
                    { "finished", finished }
                });

                return j;
            }
        };
        
        class WasmFunc {
            public:

                explicit WasmFunc(WasmFuncInfo wasm_func_info) : wasm_func_info_(wasm_func_info) {}

                // Deserialize a WasmFunc from JSON
                static WasmFunc from_json(const nlohmann::json& j) {
                    WasmFuncInfo wasm_func_info = WasmFuncInfo::from_json(j);
                    return WasmFunc(std::move(wasm_func_info));
                }

                // Serialize a WasmFunc to JSON
                nlohmann::json to_json() const {
                    return wasm_func_info_.to_json();
                }

                void set_output(detersl::types::FunctionOutput out) {           
                    out_ = out;
                }

                detersl::types::FunctionOutput get_output() {           
                    return out_;
                }

            private:
                WasmFuncInfo wasm_func_info_;
                detersl::types::FunctionOutput out_;
        };
    }
}
