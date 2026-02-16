#pragma once

#include "func.hpp"
#include "runner.hpp"
#include "wasm-executioner.hpp"
#include "wasm-func.hpp"
#include "wasm-kv.hpp"
#include <verona.h>
#include <cpp/when.h>
#include <chrono>

using namespace verona::rt;
using namespace verona::cpp;

extern rust::Box<DeterSLEngine> engine;

namespace detersl {
    namespace runner {

        inline thread_local executioner::WasmExecution* worker_excutioner = nullptr;

        class WasmRunner : public Runner {
            public:
                WasmRunner(acquired_cown_span<detersl::types::Resource> rw_cown_arr,
                    acquired_cown_span<const detersl::types::Resource> ro_cown_arr,
                    detersl::func::WasmFuncInfo func_info) : 
                    func_info_(func_info),
                    Runner(rw_cown_arr, ro_cown_arr, func_info) {
                        if (!worker_excutioner) {
                            // Threre is no executioner on this thread.
                            // Lets make it
                            worker_excutioner = new executioner::WasmExecution(*engine, new kv::WasmExecEnvKV());
                        }

                        worker_excutioner->get_kv()->reinitialize(std::move(storage));
                    }

                WasmRunner(const WasmRunner&) = delete;
                WasmRunner& operator=(const WasmRunner&) = delete;
                WasmRunner(WasmRunner&&) = delete;
                WasmRunner& operator=(WasmRunner&&) = delete;

                void set_func(detersl::func::WasmFuncInfo func) {
                    func_info_ = func;
                }

                void run() override {
                    try{
                        std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
                    
                        worker_excutioner->execution_func(func_info_);

                        std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                        std::cout << "Function " << func_info_.func_name << " executed in " << duration << " ms\n";
                    } catch (const std::exception& e) {
                        std::cerr << "Exception during WasmRunner run: " << e.what() << std::endl;
                    }

                    // Bring back storage.
                    // The reason why we did not use shared ptr is that
                    // it is too slow for fast allocation and deallocation in a high dynamic env.
                    storage = std::move(worker_excutioner->get_kv()->move_resource_storage_out());
                }

                ~WasmRunner() = default;

            protected:
                detersl::func::WasmFuncInfo func_info_;
        };
    }
}
