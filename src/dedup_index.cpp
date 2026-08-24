#include "dedup_index.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>

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
