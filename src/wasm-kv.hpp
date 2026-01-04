#pragma once

#include "kv_api.h"
#include "ffi.rs.h"
#include "bytes.hpp"
#include "resource.hpp"
#include <unordered_map>
#include <string>
#include <stdexcept>

namespace detersl {
    namespace kv {

        class WasmExecEnvKV : public KVInterface {
        public:
            explicit WasmExecEnvKV() = default;

            ~WasmExecEnvKV() {
                clear();
            }

            rust::Slice<const uint8_t> get(rust::Str key) override {
                auto it = store_.find(std::string(key.data(), key.size()));
                if (it == store_.end() || it->second == nullptr) {
                    return rust::Slice<const uint8_t>(); 
                }

                const auto& bytes = it->second->get_data();
                return rust::Slice<const uint8_t>(bytes.data(), bytes.data().size());
            }

            void set(rust::Str key, rust::Slice<const uint8_t> data) override {
                std::string key_str(key.data(), key.size());
                auto* resource = new detersl::types::Resource(
                    detersl::types::Bytes(data.data(), data.size())); 

                // TODO: this is for preventing leak. is this a corret resolution?
                delete_key(key); 
                store_[std::move(key_str)] = resource;
            }

            bool delete_key(rust::Str key) override {
                auto it = store_.find(std::string(key.data(), key.size()));
                if (it == store_.end()) {
                    return false; 
                }

                delete it->second;
                store_.erase(it); 
                return true;
            }

            void clear() {
                for (auto& pair : store_) {
                    delete pair.second;
                }
                store_.clear();
            }

            void reinitialize(std::unordered_map<std::string, detersl::types::Resource*> new_store) {
                clear(); 
                store_ = std::move(new_store);
            }

            std::unordered_map<std::string, detersl::types::Resource*> move_store_out() {
                auto old_store = std::move(store_); 
                store_.clear(); 
                return old_store;
            }

        private:
            std::unordered_map<std::string, detersl::types::Resource*> store_;
        };
    }
}
