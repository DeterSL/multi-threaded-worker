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
                explicit WasmExecution(DeterSLEngine& engine, detersl::kv::WasmExecEnvKV* kv) :
                    exec_(new_executioner(engine, new_cpp_kv(kv))), kv_(kv) {
                }


                void execution_func(detersl::func::WasmFuncInfo func) {
                    const std::string config = func.to_json().dump();
                    
                    auto out = exec_->executioner_run_json(config);
                    std::cout << "the output of function " << func.func_name << " is :" <<  std::string(out.begin(), out.end()) << std::endl;

                    // TODO: the function out can be anything
                    //func.set_output(out);
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
