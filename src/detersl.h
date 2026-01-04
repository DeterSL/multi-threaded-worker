#pragma once

#include "models.hpp"
#include "runner.hpp"
#include <verona.h>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;
using json = nlohmann::json;

namespace detersl {

    std::vector<uint8_t> get_resource(std::string key){
        return worker::cur_runner->get_resource(key);
    }

    void set_resource(std::string key, std::vector<uint8_t> &&data){
        return worker::cur_runner->set_resource(key, std::move(data));
    }   

    void delete_resource(std::string key){
        return worker::cur_runner->delete_resource(key);
    }
}
