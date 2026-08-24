#ifndef CHUNKER_H
#define CHUNKER_H

#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

namespace chunkdedupe {

enum class ChunkingMode {
    CDC,        // Content-Defined Chunking (Rabin-Karp rolling hash)
    FIXED_SIZE  // Fixed-Size Chunking (naive 4KB slicing)
};

struct ChunkInfo {
    std::string hash;          // SHA-256 hex hash
    uint64_t offset;           // Byte offset within original stream/file
    uint32_t length;           // Byte length of chunk
    std::vector<uint8_t> data; // Raw chunk bytes
};

struct ChunkerOptions {
    ChunkingMode mode = ChunkingMode::CDC;
    size_t target_chunk_size = 4096; // 4KB target chunk size
    size_t min_chunk_size = 1024;    // 1KB min chunk size
    size_t max_chunk_size = 16384;   // 16KB max chunk size
    size_t window_size = 64;         // 64-byte sliding window
};

class RollingHash {
public:
    explicit RollingHash(size_t window_size = 64, uint64_t base = 257, uint64_t mod = 1000000007);

    void Reset();
    void Eat(uint8_t in_byte);
    void Update(uint8_t out_byte, uint8_t in_byte);
    uint64_t Hash() const { return m_hash; }

private:
    size_t m_window_size;
    uint64_t m_base;
    uint64_t m_mod;
    uint64_t m_base_pow; // B^(W-1) mod M
    uint64_t m_hash;
};

class Chunker {
public:
    explicit Chunker(ChunkerOptions options = ChunkerOptions());

    std::vector<ChunkInfo> ChunkBuffer(
        const uint8_t* data,
        size_t length,
        uint64_t base_offset = 0,
        const uint8_t* full_file_ptr = nullptr
    ) const;

    std::vector<ChunkInfo> ChunkBuffer(const std::vector<uint8_t>& buffer, uint64_t base_offset = 0) const;

    size_t FindNextBoundary(const uint8_t* data, size_t length, size_t start_pos) const;

    const ChunkerOptions& GetOptions() const { return m_options; }

private:
    ChunkerOptions m_options;
};

} // namespace chunkdedupe

#endif // CHUNKER_H
