#pragma once

#include "../src/bytes.hpp"
#include "cpp-runner.hpp"
#include <cstdint>
#include <verona.h>
#include <cpp/when.h>
#include "ffi.rs.h"
#include "rust/cxx.h"

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl {

    // TODO: this method copies data which is not ideal
    std::vector<uint8_t> get_resource(const std::string& name) {
        rust::Vec<uint8_t>& bytes = runner::cur_runner->storage->get_resource(name)->get_data().as_vec();
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    }

    void set_resource(const std::string &name, std::vector<uint8_t>&& value) {
        rust::Vec<uint8_t> rust_vec;
        std::copy(value.begin(), value.end(), std::back_inserter(rust_vec));
        runner::cur_runner->storage->set_resource(name, types::Bytes(std::move(rust_vec)));
    }

    void set_resource(const std::string &name, std::vector<uint8_t>& value) {
        rust::Vec<uint8_t> rust_vec;
        std::copy(value.begin(), value.end(), std::back_inserter(rust_vec));
        runner::cur_runner->storage->set_resource(name, types::Bytes(std::move(rust_vec)));
    }

    void delete_resource(const std::string &name){
        runner::cur_runner->storage->delete_resource(name);
    }
}
