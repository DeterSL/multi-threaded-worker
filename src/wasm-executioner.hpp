#pragma once

#include "kv_api.h"
#include "ffi.rs.h"
#include "rust/cxx.h"
#include "wasm-func.hpp"
#include "wasm-kv.hpp"
#include <iostream>
#include <string>

namespace detersl {
    namespace executioner {
       
        class WasmExecution {
            public:
                explicit WasmExecution(const DeterSLEngine& engine, detersl::kv::WasmExecEnvKV* kv) :
                    exec_(new_executioner(engine, new_cpp_kv(kv))), kv_(kv) {
                }


                void execution_func(detersl::func::WasmFuncInfo&& func) {
                    const std::string config = func.to_json().dump();
                    
                    auto out = exec_->executioner_run_json(config);
                }

                detersl::kv::WasmExecEnvKV* get_kv() {
                    return kv_;
                }

                ~WasmExecution() {
                        delete kv_;
                    }

            private:
                rust::Box<FfiExecutioner> exec_;
                detersl::kv::WasmExecEnvKV* kv_;
        };
    }
}
