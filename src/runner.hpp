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
                Runner(acquired_cown_span<detersl::types::Resource> rw_cown_arr,
                    acquired_cown_span<const detersl::types::Resource> ro_cown_arr, detersl::func::BasicFuncInfo basic_func);

                std::vector<std::string> get_deleted_resources();

                virtual bool run() = 0;

                ~Runner();

                Runner(const Runner&) = delete;
                Runner& operator=(const Runner&) = delete;
                Runner(Runner&&) = delete;
                Runner& operator=(Runner&&) = delete;

                std::unique_ptr<detersl::kv::ResourceStorage> storage;

            protected:
                detersl::func::BasicFuncInfo func_info_;
        };
    }
}
