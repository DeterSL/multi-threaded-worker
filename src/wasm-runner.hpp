#pragma once

#include "func.hpp"
#include "runner.hpp"
#include "wasm-executioner.hpp"
#include "wasm-func.hpp"
#include "wasm-kv.hpp"
#include <verona.h>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;

extern rust::Box<DeterSLEngine> engine;

namespace detersl {
    namespace runner {

        inline thread_local executioner::WasmExecution* worker_excutioner = nullptr;

        class WasmRunner : public Runner {
            public:
                WasmRunner(acquired_cown_span<detersl::types::Resource> cown_arr, detersl::func::WasmFuncInfo func_info) : 
                    func_info_(func_info),
                    func_(func_info),
                    Runner(cown_arr, func_info) {
                        if (!worker_excutioner) {
                            // Threre is no executioner on this thread.
                            // Lets make it
                            worker_excutioner = new executioner::WasmExecution(*engine, new kv::WasmExecEnvKV());
                        }

                        worker_excutioner->get_kv()->reinitialize(std::move(storage));
                    }

                WasmRunner(acquired_cown_span<detersl::types::Resource> cown_arr,
                        detersl::func::WasmFuncInfo func_info,
                        detersl::func::WasmFunc func) : 
                    func_info_(func_info),
                    func_(func),
                    Runner(cown_arr, func_info) {
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

                void set_func(detersl::func::WasmFunc func) {
                    func_ = func;
                }

                void run() override {
                    try{
                        worker_excutioner->execution_func(func_);
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
                detersl::func::WasmFunc func_;
        };
    }
}
