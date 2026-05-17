#pragma once

#include <memory>

#include <cpp/when.h>
#include <verona.h>

#include "execution/function.hpp"
#include "storage/kv.hpp"

namespace detersl {
    namespace runner {
        class Runner {
            public:
                Runner(verona::cpp::acquired_cown_span<detersl::types::Resource> rw_cown_arr,
                    verona::cpp::acquired_cown_span<const detersl::types::Resource> ro_cown_arr,
                    const detersl::func::BasicFuncInfo& basic_func);

                virtual bool run() = 0;

                ~Runner();

                Runner(const Runner&) = delete;
                Runner& operator=(const Runner&) = delete;
                Runner(Runner&&) = delete;
                Runner& operator=(Runner&&) = delete;

                std::unique_ptr<detersl::kv::ResourceStorage> storage;

        };
    }
}
