#pragma once

#include <cpp/when.h>
#include <string>
#include <unordered_map>
#include "resource.hpp"

namespace detersl {
    namespace kv {

        class ResourceStorage {
            public:
                ResourceStorage(std::unordered_map<std::string, detersl::types::Resource*>& resources_to_manage) :
                    local_resources_(resources_to_manage) {}

                ResourceStorage(std::unordered_map<std::string, detersl::types::Resource*>&& resources_to_manage) : 
                    local_resources_(std::move(resources_to_manage)) {}

                // TODO: Is it good idea to return the ptr?
                virtual detersl::types::Resource* get_resource(const std::string& key) {
                    auto value = local_resources_.find(key);
                    if (value == local_resources_.end())
                        return nullptr;

                    return value->second;
                }

                virtual void set_resource(const std::string& key, detersl::types::Bytes&& data) {
                    if (local_resources_.find(key) == local_resources_.end()) {
                        // The resource storage only manage the storage that has been given to it
                        // any new value that is gonna be set should be inside the local resource
                        // and it is not permitted to generate new value.
                        assert(false);
                    }
                    local_resources_[key]->set_data(std::move(data));
                }

                virtual void delete_resource(const std::string& key) {
                    if(local_resources_.find(key) == local_resources_.end()) {
                        return;
                    }

                    // TODO: this logic of deleting data by hand is dirty
                    // but is necessary for now since the resource might 
                    // not be deleted right after the current function is executed.
                    local_resources_[key]->free_data();
                    local_resources_.erase(key);
                }

                ~ResourceStorage() {
                    std::cout << "ResourceStorage destructor called" << std::endl;
                }

            private:
                std::unordered_map<std::string, detersl::types::Resource*> local_resources_;
        };
    }
}
