#pragma once

#include <unordered_map>
#include "cpp-func.hpp"
#include "../src/runner.hpp"
#include <verona.h>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl {
    namespace runner {
        class CPPRunner;

        inline thread_local CPPRunner* cur_runner = nullptr;

        class CPPRunner : public Runner {
            public:
                CPPRunner(acquired_cown_span<detersl::types::Resource> rw_cown_arr,
                        acquired_cown_span<const detersl::types::Resource> ro_cown_arr,
                        detersl::func::CPPFunc func) : 
                    func_(func),
                    Runner(rw_cown_arr, ro_cown_arr, func.info()) {
                        if (cur_runner != nullptr) {
                            // This means that we are changing the current runner ptr
                            // without destroying the previous one
                            assert(false);
                        }
                        cur_runner = this;
                    }

                CPPRunner(const CPPRunner&) = delete;
                CPPRunner& operator=(const CPPRunner&) = delete;
                CPPRunner(CPPRunner&&) = delete;
                CPPRunner& operator=(CPPRunner&&) = delete;

                void set_func(detersl::func::CPPFunc func) {
                    func_ = func;
                }

                bool run() override {
                
                    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

                    bool ok = func_();
                
                    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                    std::cout << "Function executed in " << duration << " ms\n";
                  
                    return ok;
                }

                ~CPPRunner() {
                    cur_runner = nullptr;
                }

            protected:
                detersl::func::CPPFunc func_;
        };

    }
}
