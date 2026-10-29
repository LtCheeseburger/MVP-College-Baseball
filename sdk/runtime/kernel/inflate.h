#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kernel {

// Raw deflate (RFC 1951) as zip stores it, for .mvpmod entries. size is the expected output length; false on bad data.
bool Inflate(const uint8_t* in, size_t in_len, std::vector<uint8_t>& out, size_t size);

}  // namespace kernel
