#include "chunk_store.h"
#include "hasher.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace chunkdedupe {

ChunkStore::ChunkStore(std::string store_dir)
    : m_store_dir(std::move(store_dir)) {
    fs::create_directories(m_store_dir);
    fs::create_directories(fs::path(m_store_dir) / "manifests");
}

std::string ChunkStore::GetChunkPath(const std::string& hash) const {
    std::string prefix = hash.substr(0, 2);
    fs::path dir = fs::path(m_store_dir) / prefix;
    return (dir / (hash + ".chunk")).string();
}

std::string ChunkStore::GetManifestPath(const std::string& file_id) const {
    fs::path dir = fs::path(m_store_dir) / "manifests";
    return (dir / (file_id + ".json")).string();
}

bool ChunkStore::WriteChunk(const ChunkInfo& chunk) {
    std::string path = GetChunkPath(chunk.hash);
    fs::create_directories(fs::path(path).parent_path());

    // Skip if already exists on disk
    if (fs::exists(path)) {
        return true;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(chunk.data.data()), chunk.data.size());
    return file.good();
}

bool ChunkStore::ReadChunk(const std::string& hash, std::vector<uint8_t>& out_bytes) const {
    std::string path = GetChunkPath(hash);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    out_bytes.resize(static_cast<size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(out_bytes.data()), size);
    }
    return file.good() || size == 0;
}

bool ChunkStore::SaveManifest(const FileManifest& manifest) const {
    std::string path = GetManifestPath(manifest.file_id);
    fs::create_directories(fs::path(path).parent_path());

    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"file_id\": \"" << manifest.file_id << "\",\n";
    file << "  \"original_filename\": \"" << manifest.original_filename << "\",\n";
    file << "  \"total_size\": " << manifest.total_size << ",\n";
    file << "  \"original_sha256\": \"" << manifest.original_sha256 << "\",\n";
    file << "  \"chunks\": [\n";

    for (size_t i = 0; i < manifest.chunk_hashes.size(); ++i) {
        file << "    {\"hash\": \"" << manifest.chunk_hashes[i]
             << "\", \"length\": " << manifest.chunk_lengths[i] << "}"
             << (i + 1 < manifest.chunk_hashes.size() ? "," : "") << "\n";
    }

    file << "  ]\n";
    file << "}\n";
    return true;
}

bool ChunkStore::LoadManifest(const std::string& file_id, FileManifest& out_manifest) const {
    std::string path = GetManifestPath(file_id);
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    out_manifest.file_id = file_id;
    out_manifest.chunk_hashes.clear();
    out_manifest.chunk_lengths.clear();

    while (std::getline(file, line)) {
        auto find_value = [&](const std::string& key) -> std::string {
            size_t kpos = line.find("\"" + key + "\":");
            if (kpos == std::string::npos) return "";
            size_t start = line.find(':', kpos) + 1;
            while (start < line.size() && (line[start] == ' ' || line[start] == '"')) start++;
            size_t end = start;
            while (end < line.size() && line[end] != '"' && line[end] != ',' && line[end] != '\n' && line[end] != '\r') end++;
            return line.substr(start, end - start);
        };

        if (line.find("\"original_filename\":") != std::string::npos) {
            out_manifest.original_filename = find_value("original_filename");
        } else if (line.find("\"total_size\":") != std::string::npos) {
            std::string val = find_value("total_size");
            if (!val.empty()) out_manifest.total_size = std::stoull(val);
        } else if (line.find("\"original_sha256\":") != std::string::npos) {
            out_manifest.original_sha256 = find_value("original_sha256");
        } else if (line.find("\"hash\":") != std::string::npos) {
            std::string h = find_value("hash");
            std::string l = find_value("length");
            if (!h.empty()) {
                out_manifest.chunk_hashes.push_back(h);
                out_manifest.chunk_lengths.push_back(l.empty() ? 0 : std::stoul(l));
            }
        }
    }

    return !out_manifest.chunk_hashes.empty() || out_manifest.total_size == 0;
}

bool ChunkStore::ReconstructFile(
    const std::string& file_id,
    const std::string& output_path,
    std::string& out_reconstructed_sha256,
    bool* out_checksum_matched
) const {
    FileManifest manifest;
    if (!LoadManifest(file_id, manifest)) {
        std::cerr << "Failed to load manifest for file_id: " << file_id << "\n";
        return false;
    }

    fs::create_directories(fs::path(output_path).parent_path());
    std::ofstream outfile(output_path, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Failed to open output path for reconstruction: " << output_path << "\n";
        return false;
    }

    std::vector<uint8_t> chunk_bytes;
    for (size_t i = 0; i < manifest.chunk_hashes.size(); ++i) {
        const std::string& hash = manifest.chunk_hashes[i];
        if (!ReadChunk(hash, chunk_bytes)) {
            std::cerr << "Failed to read chunk: " << hash << " from store\n";
            return false;
        }
        outfile.write(reinterpret_cast<const char*>(chunk_bytes.data()), chunk_bytes.size());
    }

    outfile.close();

    // Compute reconstructed file SHA-256 and compare with original
    out_reconstructed_sha256 = ComputeSHA256File(output_path);
    bool matched = (out_reconstructed_sha256 == manifest.original_sha256);

    if (out_checksum_matched) {
        *out_checksum_matched = matched;
    }

    return matched;
}

} // namespace chunkdedupe
