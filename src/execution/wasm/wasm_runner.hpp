#pragma once

#include "execution/runner.hpp"
#include "execution/wasm/wasm_executioner.hpp"
#include "execution/wasm/wasm_function.hpp"
#include "execution/wasm/wasm_kv.hpp"

extern const rust::Box<DeterSLEngine> engine;

namespace detersl {
    namespace runner {

        inline thread_local executioner::WasmExecution* worker_executor = nullptr;

        class WasmRunner : public Runner {
            public:
                WasmRunner(verona::cpp::acquired_cown_span<detersl::types::Resource> rw_cown_arr,
                    verona::cpp::acquired_cown_span<const detersl::types::Resource> ro_cown_arr,
                    const detersl::func::WasmFuncInfo& func_info) :
                    Runner(rw_cown_arr, ro_cown_arr, func_info),
                    func_info_(func_info) {
                        if (!worker_executor) {
                            // Threre is no executioner on this thread.
                            // Lets make it
                            worker_executor = new executioner::WasmExecution(*engine, new kv::WasmExecEnvKV());
                        }

                        worker_executor->get_kv()->reinitialize(std::move(storage));
                    }

                WasmRunner(const WasmRunner&) = delete;
                WasmRunner& operator=(const WasmRunner&) = delete;
                WasmRunner(WasmRunner&&) = delete;
                WasmRunner& operator=(WasmRunner&&) = delete;

                bool run() override{
                    bool ok = true;
                    try{                    
                        worker_executor->execution_func(func_info_);
                    } catch (const std::exception& e) {
                        ok = false;
                    } catch (...) {
                        ok = false;
                    }

                    // Bring back storage.
                    // The reason why we did not use shared ptr is that
                    // it is too slow for fast allocation and deallocation in a high dynamic env.
                    storage = std::move(worker_executor->get_kv()->move_resource_storage_out());
                    return ok;
                }
 
                ~WasmRunner() = default;

            protected:
                const detersl::func::WasmFuncInfo& func_info_;
        };
    }
}
