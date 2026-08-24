#ifndef DEDUP_INDEX_H
#define DEDUP_INDEX_H

#include "chunker.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <vector>

namespace chunkdedupe {

struct ChunkMeta {
    std::string hash;
    uint64_t size = 0;
    uint64_t ref_count = 0;
    std::string relative_path;
};

struct StorageStats {
    uint64_t total_files_ingested = 0;
    uint64_t total_logical_bytes = 0;
    uint64_t total_unique_chunks = 0;
    uint64_t total_unique_bytes = 0;

    double GetDedupRatioPercent() const {
        if (total_logical_bytes == 0) return 0.0;
        if (total_unique_bytes == 0) return 100.0;
        return (1.0 - static_cast<double>(total_unique_bytes) / static_cast<double>(total_logical_bytes)) * 100.0;
    }

    double GetCompressionFactor() const {
        if (total_unique_bytes == 0) return 1.0;
        return static_cast<double>(total_logical_bytes) / static_cast<double>(total_unique_bytes);
    }
};

struct IndexLookupStats {
    size_t total_entries = 0;
    double avg_lookup_us = 0.0;
    double p99_lookup_us = 0.0;
};

class DedupIndex {
public:
    DedupIndex() = default;

    // Checks if a chunk is present.
    // If present: increments ref_count, adds chunk.length to logical bytes, sets is_new = false.
    // If new: inserts metadata, increments unique & logical counts, sets is_new = true.
    // Returns true if chunk is NEW, false if DUPLICATE.
    bool InsertOrRef(const ChunkInfo& chunk, std::string& out_relative_path);

    // Batch insert for multi-threaded chunk results
    void ProcessBatch(const std::vector<ChunkInfo>& chunks, std::vector<bool>& is_new_flags, std::vector<std::string>& relative_paths);

    // Increments total ingested file counter
    void IncrementFileCount();

    // Retrieves metadata for a chunk hash
    bool GetChunkMeta(const std::string& hash, ChunkMeta& out_meta) const;

    // Measures average and p99 hash lookup latency across num_queries lookups
    IndexLookupStats MeasureLookupLatency(size_t num_queries = 10000) const;

    // Returns a snapshot of the current storage statistics
    StorageStats GetStats() const;

    // Clears the entire index
    void Clear();

    // Persists index state to disk (simple text format)
    bool SaveToFile(const std::string& filepath) const;

    // Loads index state from disk
    bool LoadFromFile(const std::string& filepath);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, ChunkMeta> m_index;
    StorageStats m_stats;
};

} // namespace chunkdedupe

#endif // DEDUP_INDEX_H
