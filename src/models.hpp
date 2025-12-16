#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <iostream>

using json = nlohmann::json;

struct FunctionState {
    std::string id;
    std::string name;
    std::vector<std::string> resources;
    std::string input;
    std::string output;
    bool started = false;
    bool finished = false;
};

struct Resource {
  public:
    Resource(void *data_) : data(data_)
  {}
    template<typename T>
    T& get_data() const {
        if(data == nullptr) {
            throw std::runtime_error("Resource data is null");
        }
        return *reinterpret_cast<T*>(data);
    }

    Resource operator=(const Resource& t)
    {
        return Resource(t.data);
    }

    void set_data(void *data_)
    {
        data = data_;
    }

    template<typename T>
    void free_data()
    {
        std::cout << "Freeing Resource data via free_data()" << std::endl;
        delete reinterpret_cast<T*>(data);
        data = nullptr;
    }

    // void set_delete()
    // {
    //     to_delete = true;
    // }

    ~Resource()
    {
        std::cout << "Resource destructor called" << std::endl;
        if(data != nullptr)
        {
            std::cout << "Freeing Resource data in destructor" << std::endl;
            delete reinterpret_cast<char*>(data);
        }
    }

  private:
    void *data;
    // bool to_delete = false;
};

inline void to_json(json& j, const FunctionState& obj) {
    j = json::object();
    j["id"] = obj.id;
    j["name"] = obj.name;
    if(obj.started)
        j["started"] = obj.started;
    if(obj.finished)
        j["finished"] = obj.finished;
    if(!obj.input.empty())
        j["input"] = obj.input;
    if(!obj.output.empty())
        j["output"] = obj.output;
    if (!obj.resources.empty())
        j["resources"] = obj.resources;
}

inline void from_json(const json& j, FunctionState& obj) {
    obj.id = j.at("id").get<std::string>();
    obj.name = j.at("name").get<std::string>();
    if(j.contains("started"))
        obj.started = j.at("started").get<bool>();
    if(j.contains("finished"))
        obj.finished = j.at("finished").get<bool>();
    if(j.contains("input"))
        obj.input = j.at("input").get<std::string>();
    if(j.contains("output"))    
        obj.output = j.at("output").get<std::string>();
    if(j.contains("resources"))
        obj.resources = j.at("resources").get<std::vector<std::string>>();
}