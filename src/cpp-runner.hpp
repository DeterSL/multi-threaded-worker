#pragma once

#include <unordered_map>
#include "cpp-func.hpp"
#include "func.hpp"
#include "runner.hpp"
#include <verona.h>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl {
    namespace runner {

        class CPPRunner : public Runner {
            public:
                CPPRunner(acquired_cown_span<detersl::types::Resource> rw_cown_arr,
                    acquired_cown_span<const detersl::types::Resource> ro_cown_arr,
                    detersl::func::CPPFuncInfo func_info) : 
                    func_info_(func_info),
                    func_(func_info),
                    Runner(rw_cown_arr, ro_cown_arr, func_info) {}

                CPPRunner(acquired_cown_span<detersl::types::Resource> rw_cown_arr,
                        acquired_cown_span<const detersl::types::Resource> ro_cown_arr,
                        detersl::func::CPPFuncInfo func_info,
                        detersl::func::CPPFunc func) : 
                    func_info_(func_info),
                    func_(func),
                    Runner(rw_cown_arr, ro_cown_arr, func_info) {}

                CPPRunner(const CPPRunner&) = delete;
                CPPRunner& operator=(const CPPRunner&) = delete;
                CPPRunner(CPPRunner&&) = delete;
                CPPRunner& operator=(CPPRunner&&) = delete;

                void set_func(detersl::func::CPPFunc func) {
                    func_ = func;
                }

                void run() override {
                    func_();
                }

                ~CPPRunner() = default;

            protected:
                detersl::func::CPPFuncInfo func_info_;
                detersl::func::CPPFunc func_;
        };
    }
}
