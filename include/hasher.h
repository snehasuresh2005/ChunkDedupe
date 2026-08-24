#ifndef HASHER_H
#define HASHER_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace chunkdedupe {

// Computes SHA-256 hash of a raw memory buffer and returns a 64-character hex string.
std::string ComputeSHA256(const uint8_t* data, size_t length);

// Helper overload for std::vector<uint8_t>
std::string ComputeSHA256(const std::vector<uint8_t>& data);

// Helper overload for std::string
std::string ComputeSHA256(const std::string& str);

// Computes SHA-256 hash of an entire file from disk.
std::string ComputeSHA256File(const std::string& filepath);

} // namespace chunkdedupe

#endif // HASHER_H
