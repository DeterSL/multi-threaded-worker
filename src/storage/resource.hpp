#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>

#include "storage/bytes.hpp"
#include "ffi.rs.h"

namespace detersl {
    namespace types {
        struct Resource {
            public:

                explicit Resource() = default;

                explicit Resource(detersl::types::Bytes& data) : committed_(data)
                {}

                explicit Resource(detersl::types::Bytes&& data) : committed_(std::move(data))
                {}

                explicit Resource(rust::Vec<uint8_t>&& data) : committed_(std::move(data))
                {}


                detersl::types::Bytes& get_data() {
                    if (uncommitted_.has_value() && !uncommitted_deleted_) {
                        return *uncommitted_;
                    }
                    return committed_;
                }

                const detersl::types::Bytes& get_data() const{
                    if (uncommitted_.has_value() && !uncommitted_deleted_) {
                        return *uncommitted_;
                    }
                    return committed_;
                }

                Resource& operator=(const Resource& t) {
                    committed_ = t.committed_;
                    uncommitted_ = t.uncommitted_;
                    committed_deleted_ = t.committed_deleted_;
                    uncommitted_deleted_ = t.uncommitted_deleted_;
                    return *this; 
                }

                void set_data(detersl::types::Bytes&& data) {
                    uncommitted_ = std::move(data);
                    uncommitted_deleted_ = false;
                }

                void free_data() {
                    committed_.clear();
                    committed_deleted_ = true;
                    uncommitted_.reset();
                    uncommitted_deleted_ = false;
                    std::cout << "Freeing Resource data via free_data()" << std::endl;
                }

                void delete_data() {
                    uncommitted_.reset();
                    uncommitted_deleted_ = true;
                }

                void commit_uncommitted() {
                    if (uncommitted_deleted_) {
                        committed_.clear();
                        committed_deleted_ = true;
                    } else if (uncommitted_.has_value()) {
                        committed_ = std::move(*uncommitted_);
                        committed_deleted_ = false;
                    }
                    uncommitted_.reset();
                    uncommitted_deleted_ = false;
                }

                void abort_uncommitted() {
                    uncommitted_.reset();
                    uncommitted_deleted_ = false;
                }

                bool is_deleted() const {
                    if (uncommitted_deleted_) {
                        return true;
                    }
                    if (uncommitted_.has_value()) {
                        return false;
                    }
                    return committed_deleted_;
                }

                bool readable() const {
                    if (uncommitted_deleted_) {
                        return false;
                    }
                    if (uncommitted_.has_value()) {
                        return true;
                    }
                    return !committed_deleted_;
                }

            private:
                detersl::types::Bytes committed_;
                std::optional<detersl::types::Bytes> uncommitted_;
                bool committed_deleted_ = false;
                bool uncommitted_deleted_ = false;
        };
    }
}
