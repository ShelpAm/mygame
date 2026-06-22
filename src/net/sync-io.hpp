#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Type-safe binary I/O for entity sync.
// SyncWriter / SyncReader replace manual write_bytes/read_bytes offset chains.

class SyncWriter {
  public:
    explicit SyncWriter(std::vector<uint8_t> &out) : out_(&out) {}

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void write(T const &val)
    {
        if constexpr (std::is_same_v<T, bool>) {
            out_->push_back(val ? uint8_t{1} : uint8_t{0});
        }
        else {
            auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(val);
            if constexpr (std::endian::native != std::endian::little) {
                std::reverse(bytes.begin(), bytes.end());
            }
            out_->insert(out_->end(), bytes.begin(), bytes.end());
        }
    }

  private:
    std::vector<uint8_t> *out_;
};

class SyncReader {
  public:
    SyncReader(uint8_t const *data, size_t size) : end_(data + size), cursor_(data) {}

    bool done() const { return cursor_ >= end_; }
    std::ptrdiff_t remaining() const { return end_ - cursor_; }

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    T read()
    {
        if constexpr (std::is_same_v<T, bool>) {
            return *cursor_++ != 0;
        }
        else {
            std::array<uint8_t, sizeof(T)> arr;
            std::memcpy(arr.data(), cursor_, sizeof(T));
            cursor_ += sizeof(T);
            if constexpr (std::endian::native != std::endian::little) {
                std::reverse(arr.begin(), arr.end());
            }
            return std::bit_cast<T>(arr);
        }
    }

  private:
    uint8_t const *end_;
    uint8_t const *cursor_;
};
