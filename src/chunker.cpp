#include "chunker.h"
#include "hasher.h"
#include <algorithm>
#include <iostream>

namespace chunkdedupe {

RollingHash::RollingHash(size_t window_size, uint64_t base, uint64_t mod)
    : m_window_size(window_size), m_base(base), m_mod(mod), m_base_pow(1), m_hash(0) {
    for (size_t i = 0; i < window_size - 1; ++i) {
        m_base_pow = (m_base_pow * m_base) % m_mod;
    }
}

void RollingHash::Reset() {
    m_hash = 0;
}

void RollingHash::Eat(uint8_t in_byte) {
    m_hash = (m_hash * m_base + in_byte) % m_mod;
}

void RollingHash::Update(uint8_t out_byte, uint8_t in_byte) {
    uint64_t remove = (out_byte * m_base_pow) % m_mod;
    uint64_t h = (m_hash >= remove) ? (m_hash - remove) : (m_hash + m_mod - remove);
    m_hash = (h * m_base + in_byte) % m_mod;
}

Chunker::Chunker(ChunkerOptions options)
    : m_options(options) {
}

std::vector<ChunkInfo> Chunker::ChunkBuffer(
    const uint8_t* data,
    size_t length,
    uint64_t base_offset,
    const uint8_t* full_file_ptr
) const {
    std::vector<ChunkInfo> chunks;
    if (length == 0) {
        return chunks;
    }

    if (m_options.mode == ChunkingMode::FIXED_SIZE) {
        size_t chunk_start = 0;
        while (chunk_start < length) {
            size_t chunk_len = std::min(m_options.target_chunk_size, length - chunk_start);
            ChunkInfo info;
            info.offset = base_offset + chunk_start;
            info.length = static_cast<uint32_t>(chunk_len);
            info.data.assign(data + chunk_start, data + chunk_start + chunk_len);
            info.hash = ComputeSHA256(info.data);
            chunks.push_back(std::move(info));
            chunk_start += chunk_len;
        }
        return chunks;
    }

    const uint8_t* file_ctx = (full_file_ptr != nullptr) ? full_file_ptr : (data - base_offset);

    RollingHash hasher(m_options.window_size);
    size_t chunk_start = 0;

    for (size_t i = 0; i < length; ++i) {
        size_t global_idx = static_cast<size_t>(base_offset) + i;

        if (global_idx < m_options.window_size) {
            hasher.Eat(data[i]);
        } else {
            uint8_t out_byte = file_ctx[global_idx - m_options.window_size];
            hasher.Update(out_byte, data[i]);
        }

        size_t current_chunk_len = i - chunk_start + 1;
        bool is_last_byte = (i == length - 1);
        bool reached_max = (current_chunk_len >= m_options.max_chunk_size);
        bool boundary_hit = (current_chunk_len >= m_options.min_chunk_size) &&
                            ((hasher.Hash() % m_options.target_chunk_size) == 0);

        if (boundary_hit || reached_max || is_last_byte) {
            ChunkInfo info;
            info.offset = base_offset + chunk_start;
            info.length = static_cast<uint32_t>(current_chunk_len);
            info.data.assign(data + chunk_start, data + chunk_start + current_chunk_len);
            info.hash = ComputeSHA256(info.data);

            chunks.push_back(std::move(info));
            chunk_start = i + 1;
            hasher.Reset();
        }
    }

    return chunks;
}

std::vector<ChunkInfo> Chunker::ChunkBuffer(const std::vector<uint8_t>& buffer, uint64_t base_offset) const {
    return ChunkBuffer(buffer.data(), buffer.size(), base_offset, buffer.data());
}

size_t Chunker::FindNextBoundary(const uint8_t* data, size_t length, size_t start_pos) const {
    if (start_pos >= length) return length;

    if (m_options.mode == ChunkingMode::FIXED_SIZE) {
        size_t rem = start_pos % m_options.target_chunk_size;
        if (rem == 0) return start_pos;
        return std::min(length, start_pos + (m_options.target_chunk_size - rem));
    }

    RollingHash hasher(m_options.window_size);
    size_t chunk_start = 0;

    for (size_t i = 0; i < length; ++i) {
        if (i < m_options.window_size) {
            hasher.Eat(data[i]);
        } else {
            hasher.Update(data[i - m_options.window_size], data[i]);
        }

        size_t current_chunk_len = i - chunk_start + 1;
        bool is_last_byte = (i == length - 1);
        bool reached_max = (current_chunk_len >= m_options.max_chunk_size);
        bool boundary_hit = (current_chunk_len >= m_options.min_chunk_size) &&
                            ((hasher.Hash() % m_options.target_chunk_size) == 0);

        if (boundary_hit || reached_max || is_last_byte) {
            size_t boundary_idx = i + 1;
            if (boundary_idx >= start_pos) {
                return boundary_idx;
            }
            chunk_start = i + 1;
            hasher.Reset();
        }
    }

    return length;
}

} // namespace chunkdedupe
