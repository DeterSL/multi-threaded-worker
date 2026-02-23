#pragma once

#include <cpp/when.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include "resource.hpp"

namespace detersl {
    namespace kv {

        class ResourceStorage {
            public:
                ResourceStorage(std::unordered_map<std::string, detersl::types::Resource*>& resources_to_manage,
                    std::unordered_set<std::string> read_only_resources = {}) :
                    local_resources_(resources_to_manage),
                    read_only_resources_(std::move(read_only_resources)) {}

                ResourceStorage(std::unordered_map<std::string, detersl::types::Resource*>&& resources_to_manage,
                    std::unordered_set<std::string> read_only_resources = {}) : 
                    local_resources_(std::move(resources_to_manage)),
                    read_only_resources_(std::move(read_only_resources)) {}

                // TODO: Is it good idea to return the ptr?
                virtual detersl::types::Resource* get_resource(const std::string& key) {
                    auto value = local_resources_.find(key);
                    if (value == local_resources_.end())
                        return nullptr;

                    if (!value->second || value->second->is_deleted()) {
                        return nullptr;
                    }

                    return value->second;
                }

                virtual void set_resource(const std::string& key, detersl::types::Bytes&& data) {
                    if (local_resources_.find(key) == local_resources_.end()) {
                        // The resource storage only manage the storage that has been given to it
                        // any new value that is gonna be set should be inside the local resource
                        // and it is not permitted to generate new value.
                        // TODO: manage this error better because assert gets optimized away
                        assert(false);
                        return;
                    }
                    if (is_read_only(key)) {
                        std::cerr << "Trying to write to a read-only resource: " << key << std::endl;
                        return;
                    }
                    local_resources_[key]->set_data(std::move(data));
                }

                virtual void delete_resource(const std::string& key) {
                    if(local_resources_.find(key) == local_resources_.end()) {
                        return;
                    }

                    if (is_read_only(key)) {
                        std::cerr << "Trying to delete a read-only resource: " << key << std::endl;
                        return;
                    }
                    local_resources_[key]->delete_data();
                }

                ~ResourceStorage() {
                }

            private:
                inline bool is_read_only(const std::string& key) const {
                    return read_only_resources_.count(key) != 0;
                }

                std::unordered_map<std::string, detersl::types::Resource*> local_resources_;
                std::unordered_set<std::string> read_only_resources_;
        };
    }
}
