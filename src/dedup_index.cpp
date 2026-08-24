#include "dedup_index.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <chrono>
#include <algorithm>

namespace chunkdedupe {

bool DedupIndex::InsertOrRef(const ChunkInfo& chunk, std::string& out_relative_path) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_stats.total_logical_bytes += chunk.length;

    auto it = m_index.find(chunk.hash);
    if (it != m_index.end()) {
        it->second.ref_count++;
        out_relative_path = it->second.relative_path;
        return false; // Duplicate chunk
    }

    // New chunk
    std::string prefix = chunk.hash.substr(0, 2);
    std::string rel_path = prefix + "/" + chunk.hash + ".chunk";

    ChunkMeta meta;
    meta.hash = chunk.hash;
    meta.size = chunk.length;
    meta.ref_count = 1;
    meta.relative_path = rel_path;

    m_index[chunk.hash] = meta;
    m_stats.total_unique_chunks++;
    m_stats.total_unique_bytes += chunk.length;

    out_relative_path = rel_path;
    return true; // New chunk
}

void DedupIndex::ProcessBatch(
    const std::vector<ChunkInfo>& chunks,
    std::vector<bool>& is_new_flags,
    std::vector<std::string>& relative_paths
) {
    is_new_flags.reserve(chunks.size());
    relative_paths.reserve(chunks.size());

    for (const auto& chunk : chunks) {
        std::string rel_path;
        bool is_new = InsertOrRef(chunk, rel_path);
        is_new_flags.push_back(is_new);
        relative_paths.push_back(rel_path);
    }
}

void DedupIndex::IncrementFileCount() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.total_files_ingested++;
}

bool DedupIndex::GetChunkMeta(const std::string& hash, ChunkMeta& out_meta) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_index.find(hash);
    if (it != m_index.end()) {
        out_meta = it->second;
        return true;
    }
    return false;
}

IndexLookupStats DedupIndex::MeasureLookupLatency(size_t num_queries) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    IndexLookupStats stats;
    stats.total_entries = m_index.size();
    if (m_index.empty() || num_queries == 0) return stats;

    std::vector<std::string> keys;
    keys.reserve(std::min(num_queries, m_index.size()));
    for (const auto& [hash, meta] : m_index) {
        keys.push_back(hash);
        if (keys.size() >= num_queries) break;
    }

    std::vector<double> latencies_us;
    latencies_us.reserve(keys.size());

    for (const auto& k : keys) {
        auto t1 = std::chrono::high_resolution_clock::now();
        auto it = m_index.find(k);
        (void)it;
        auto t2 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t2 - t1).count();
        latencies_us.push_back(us);
    }

    std::sort(latencies_us.begin(), latencies_us.end());

    double sum = 0.0;
    for (double d : latencies_us) sum += d;
    stats.avg_lookup_us = sum / latencies_us.size();

    size_t p99_idx = static_cast<size_t>(latencies_us.size() * 0.99);
    if (p99_idx >= latencies_us.size()) p99_idx = latencies_us.size() - 1;
    stats.p99_lookup_us = latencies_us[p99_idx];

    return stats;
}

StorageStats DedupIndex::GetStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

void DedupIndex::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_index.clear();
    m_stats = StorageStats();
}

bool DedupIndex::SaveToFile(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    // Header stats
    file << m_stats.total_files_ingested << " "
         << m_stats.total_logical_bytes << " "
         << m_stats.total_unique_chunks << " "
         << m_stats.total_unique_bytes << "\n";

    // Entries
    for (const auto& [hash, meta] : m_index) {
        file << meta.hash << " "
             << meta.size << " "
             << meta.ref_count << " "
             << meta.relative_path << "\n";
    }
    return true;
}

bool DedupIndex::LoadFromFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    m_index.clear();
    if (!(file >> m_stats.total_files_ingested
               >> m_stats.total_logical_bytes
               >> m_stats.total_unique_chunks
               >> m_stats.total_unique_bytes)) {
        return false;
    }

    std::string hash, rel_path;
    uint64_t size, ref_count;
    while (file >> hash >> size >> ref_count >> rel_path) {
        ChunkMeta meta;
        meta.hash = hash;
        meta.size = size;
        meta.ref_count = ref_count;
        meta.relative_path = rel_path;
        m_index[hash] = meta;
    }
    return true;
}

} // namespace chunkdedupe
