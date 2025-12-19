#pragma once

#include "models.hpp"
#include "runner.hpp"
#include <verona.h>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;
using json = nlohmann::json;

namespace detersl {
    template<typename T>
    T& get_resource(const std::string& name) {
        return worker::cur_runner->get_resource<T>(name);
    }

    template<class T>
    void set_resource(const std::string &name, T& value)
    {
        return worker::cur_runner->set_resource<T>(name, value);
    }

    // Overload for rvalue references
    template<class T>
    void set_resource(const std::string &name, T&& value)
    {
        T temp = std::move(value);
        return worker::cur_runner->set_resource<T>(name, temp);
    }

    template<class T>
    void delete_resource(const std::string &name){
        return worker::cur_runner->delete_resource<T>(name);
    }

}
