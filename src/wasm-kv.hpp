#pragma once

#include "kv.hpp"
#include "kv_api.h"
#include "ffi.rs.h"
#include "bytes.hpp"
#include "resource.hpp"
#include <memory>

namespace detersl {
    namespace kv {

        class WasmExecEnvKV : public KVInterface {
        public:
            explicit WasmExecEnvKV()
                : resource_storage_(std::make_unique<ResourceStorage>(
                      std::unordered_map<std::string, detersl::types::Resource*>())) {}

            ~WasmExecEnvKV() = default; 

            void reinitialize(std::unique_ptr<ResourceStorage> new_resource_storage) {
                resource_storage_ = std::move(new_resource_storage);
            }

            std::unique_ptr<ResourceStorage> move_resource_storage_out() {
                return std::move(resource_storage_);
            }

            rust::Slice<const uint8_t> get(rust::Str key) override {
                std::string key_str(key.data(), key.size());
                auto* resource = resource_storage_->get_resource(key_str);
                if (!resource) {
                    return rust::Slice<const uint8_t>();
                }

                const auto& bytes = resource->get_data().as_vec();
                return rust::Slice<const uint8_t>(bytes.data(), bytes.size());
            }

            void set(rust::Str key, rust::Vec<uint8_t> data) override {
                std::string key_str(key.data(), key.size());

                resource_storage_->set_resource(key_str, detersl::types::Bytes(std::move(data)));
            }

            bool delete_key(rust::Str key) override {
                std::string key_str(key.data(), key.size());
                resource_storage_->delete_resource(key_str);
                return true;
            }

            void clear() {
                resource_storage_ = std::make_unique<ResourceStorage>(
                    std::unordered_map<std::string, detersl::types::Resource*>());
            }

        private:
            std::unique_ptr<ResourceStorage> resource_storage_;
        };
    }
}
