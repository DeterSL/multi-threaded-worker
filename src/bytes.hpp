#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <vector>
namespace detersl {
    namespace types {

        template<typename Alloc = std::allocator<uint8_t>>
        struct BaseBytes {
            explicit BaseBytes() = default;

            explicit BaseBytes(const std::vector<uint8_t>& in_bytes)
            {
                std::copy(in_bytes.begin(), in_bytes.end(), std::back_inserter(data_));
            }

            explicit BaseBytes(std::vector<uint8_t>&& in_bytes)
            {
                data_ = std::move(in_bytes);
            }

            explicit BaseBytes(const uint8_t* data, std::size_t size)
            {
               data_.insert(data_.begin(), data, data + size); 
            }

            BaseBytes(const BaseBytes& other) 
            {
                std::copy(other.data_.begin(), other.data_.end(), std::back_inserter(data_));
            }

            BaseBytes(BaseBytes&& other)
            {
                data_ = std::move(other.data_);
            }

            BaseBytes operator+(const BaseBytes& rhs)
            {
                std::vector<uint8_t> result = data_;
                std::copy(rhs.data_.begin(), rhs.data_.end(), std::back_inserter(result));
                return BaseBytes(std::move(result));
            }

            BaseBytes& operator=(const BaseBytes& other)
            {
                std::copy(other.data_.begin(), other.data_.end(), std::back_inserter(data_));
                return *this;
            }
            
            BaseBytes& operator=(BaseBytes&& other) noexcept
            {
                data_ = std::move(other.data_);
                return *this;
            }

            ~BaseBytes() {};

            std::vector<uint8_t, Alloc>& as_vec() {
                return data_;
            }

        private:
            std::vector<uint8_t, Alloc> data_;
        };

        using Bytes = BaseBytes<>;
    }
}
