#pragma once

#include "bytes.hpp"
#include "runner.hpp"
#include <cstdint>
#include <verona.h>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;

namespace detersl {

    // TODO: this method copies data which is not ideal
    std::string get_resource(const std::string& name) {
        auto bytes = runner::cur_runner->storage->get_resource(name)->get_data().as_vec();
        return std::string((char*)bytes.data(), bytes.size());
    }

    void set_resource(const std::string &name, std::string& value) {
        auto bytes = types::Bytes((uint8_t*)value.data(), value.size());
        runner::cur_runner->storage->set_resource(name, types::Resource(std::move(bytes)));
    }

    void set_resource(const std::string &name, std::string&& value) {
        auto bytes = types::Bytes((uint8_t*)value.data(), value.size());
        runner::cur_runner->storage->set_resource(name, types::Resource(std::move(bytes)));
    }

    void delete_resource(const std::string &name){
        runner::cur_runner->storage->delete_resource(name);
    }
}
