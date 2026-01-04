#pragma once

#include "func.hpp"
#include "types.hpp"

namespace detersl {
    namespace func {

        struct WasmFuncInfo : public BasicFuncInfo {

            WasmFuncInfo() : func_hash("nothing"),
                                func_clock_init(0),
                                func_random_seed(42) {}

            std::string func_hash;
            int func_clock_init;
            int func_random_seed;
        };
        
        class WasmFunc : public WasmFuncInfo {
            public:

                explicit WasmFunc() {}

                static WasmFunc from_json() {
                    //TODO: return wasmfunc from json
                }
        };
    }
}
