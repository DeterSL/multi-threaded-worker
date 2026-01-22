#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>

#include "bytes.hpp"

namespace detersl {
    namespace types {
        struct Resource {
            public:

                explicit Resource() = default;

                explicit Resource(detersl::types::Bytes& data) : data_(data)
                {}

                explicit Resource(detersl::types::Bytes&& data) : data_(std::move(data))
                {}

                explicit Resource(std::vector<uint8_t>&& data) : data_(std::move(data))
                {}


                detersl::types::Bytes& get_data() {
                    return data_;
                }

                bool is_deleted() const {
                    return deleted_;
                }

                void mark_deleted() {
                    deleted_ = true;
                }

                Resource& operator=(const Resource& t) {
                    data_ = t.data_;
                    deleted_ = t.deleted_;
                    return *this; 
                }

                void set_data(void *data, std::size_t size) {
                    data_ = detersl::types::Bytes((uint8_t*)data, size);
                }

                void set_data(detersl::types::Bytes&& data) {
                    data_ = std::move(data);
                }

                void free_data() {
                    data_ = detersl::types::Bytes();
                    std::cout << "Freeing Resource data via free_data()" << std::endl;
                }

                ~Resource() {
                    std::cout << "Resource destructor called" << std::endl;
                }

            private:
                detersl::types::Bytes data_;
                bool deleted_ = false;
        };
    }
}

