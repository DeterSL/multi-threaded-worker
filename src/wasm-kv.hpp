#pragma once

#include "kv.hpp"
#include "kv_api.h"
#include "ffi.rs.h"
#include "bytes.hpp"
#include "resource.hpp"
#include <memory>

// Namespace declaration stays the same
namespace detersl {
    namespace kv {

        class WasmExecEnvKV : public KVInterface {
        public:
            // Default constructor initializes with an empty ResourceStorage
            explicit WasmExecEnvKV()
                : resource_storage_(std::make_unique<ResourceStorage>(
                      std::unordered_map<std::string, detersl::types::Resource*>())) {}

            ~WasmExecEnvKV() = default; // Let unique_ptr manage resource storage cleanup

            // Reinitialize with a new ResourceStorage passed as a unique_ptr
            void reinitialize(std::unique_ptr<ResourceStorage> new_resource_storage) {
                resource_storage_ = std::move(new_resource_storage); // Take ownership
            }

            // Move out the ResourceStorage while resetting internal storage
            std::unique_ptr<ResourceStorage> move_resource_storage_out() {
                return std::move(resource_storage_); // Transfer ownership out
            }

            // Get stored data as a rust::Slice
            rust::Slice<const uint8_t> get(rust::Str key) override {
                std::string key_str(key.data(), key.size());
                auto* resource = resource_storage_->get_resource(key_str);
                if (!resource) {
                    return rust::Slice<const uint8_t>(); // Return empty slice if not found
                }

                const auto& bytes = resource->get_data().as_vec();
                return rust::Slice<const uint8_t>(bytes.data(), bytes.size());
            }

            // Set a new key/value resource
            void set(rust::Str key, rust::Slice<const uint8_t> data) override {
                std::string key_str(key.data(), key.size());
                auto new_resource = detersl::types::Resource(
                    detersl::types::Bytes(data.data(), data.size())); 

                // Delegate to resource storage
                resource_storage_->set_resource(key_str, std::move(new_resource));
            }

            // Delete a key from the storage
            bool delete_key(rust::Str key) override {
                std::string key_str(key.data(), key.size());
                resource_storage_->delete_resource(key_str);
                return true;
            }

            // Clear all resources (reinitialize with an empty `ResourceStorage`)
            void clear() {
                resource_storage_ = std::make_unique<ResourceStorage>(
                    std::unordered_map<std::string, detersl::types::Resource*>());
            }

        private:
            std::unique_ptr<ResourceStorage> resource_storage_;
        };
    }
}
