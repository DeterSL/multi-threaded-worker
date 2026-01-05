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
                    exec_(new_executioner(engine, new_cpp_kv(kv))) {
                }


                void execution_func(detersl::func::WasmFunc func) {
                    const std::string config = func.to_json().dump();
                    std::cout << "haha" << std::endl;
                    auto out = exec_->executioner_run_json(config);
                    std::cout << "haha1" << std::endl;

                    // TODO: the function out can be anything
                    //func.set_output(out);
                }

            private:
                rust::Box<FfiExecutioner> exec_;
        };
    }
}
