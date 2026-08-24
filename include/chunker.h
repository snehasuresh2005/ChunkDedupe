#ifndef CHUNKER_H
#define CHUNKER_H

#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

namespace chunkdedupe {

struct ChunkInfo {
    std::string hash;       // SHA-256 hex hash
    uint64_t offset;        // Byte offset within original stream/file
    uint32_t length;        // Byte length of chunk
    std::vector<uint8_t> data; // Raw chunk bytes
};

struct ChunkerOptions {
    size_t target_chunk_size = 4096; // 4KB average chunk size
    size_t min_chunk_size = 1024;    // 1KB min chunk size
    size_t max_chunk_size = 16384;   // 16KB max chunk size
    size_t window_size = 64;         // 64-byte sliding window
};

class RollingHash {
public:
    explicit RollingHash(size_t window_size = 64, uint64_t base = 31);

    void Reset();
    void Eat(uint8_t in_byte);
    void Update(uint8_t out_byte, uint8_t in_byte);
    uint64_t Hash() const { return m_hash; }

private:
    size_t m_window_size;
    uint64_t m_base;
    uint64_t m_base_pow; // B^(W-1)
    uint64_t m_hash;
};

class Chunker {
public:
    explicit Chunker(ChunkerOptions options = ChunkerOptions());

    // Chunks a buffer segment into a list of ChunkInfo objects.
    // If full_file_ptr is provided, rolling hash calculation evaluates exact global stream context.
    std::vector<ChunkInfo> ChunkBuffer(
        const uint8_t* data,
        size_t length,
        uint64_t base_offset = 0,
        const uint8_t* full_file_ptr = nullptr
    ) const;

    // Helper overload
    std::vector<ChunkInfo> ChunkBuffer(const std::vector<uint8_t>& buffer, uint64_t base_offset = 0) const;

    // Finds the next content-defined boundary position at or after start_pos
    size_t FindNextBoundary(const uint8_t* data, size_t length, size_t start_pos) const;

    // Parallel segment chunker
    std::vector<ChunkInfo> ChunkSegment(
        const uint8_t* data,
        size_t segment_offset,
        size_t segment_length,
        const uint8_t* lookbehind_data,
        size_t lookbehind_length,
        uint64_t base_file_offset
    ) const;

    const ChunkerOptions& GetOptions() const { return m_options; }

private:
    ChunkerOptions m_options;
};

} // namespace chunkdedupe

#endif // CHUNKER_H
