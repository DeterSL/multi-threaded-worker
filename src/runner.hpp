#pragma once
#include <memory>
#include "func.hpp"
#include "kv.hpp"
#include <verona.h>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl {
    namespace runner {
        class Runner {
            public:
                Runner(acquired_cown_span<detersl::types::Resource> cown_arr, detersl::func::BasicFuncInfo basic_func);

                std::vector<std::string> get_deleted_resources();

                virtual void run() = 0;

                ~Runner();

                Runner(const Runner&) = delete;
                Runner& operator=(const Runner&) = delete;
                Runner(Runner&&) = delete;
                Runner& operator=(Runner&&) = delete;

                std::unique_ptr<detersl::kv::ResourceStorage> storage;

            protected:
                detersl::func::BasicFuncInfo func_info_;
        };

    inline thread_local Runner* cur_runner = nullptr;
    }
}
