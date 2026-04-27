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


                void execution_func(const detersl::func::WasmFuncInfo& func) {
                    const std::string config = nlohmann::json(func).dump();

                    //std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
                    auto out = exec_->executioner_run_json(config);
                    // std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
                    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                    // std::cout << "Function " << func.func_name << " executed in " << duration << " microseconds" << std::endl;
                    //std::string res = std::string(out.begin(), out.end());

                    // if(!res.empty() && res != "success"){
                    //     std::cout << func.func_name << " output: " << res << std::endl;
                    // }
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
