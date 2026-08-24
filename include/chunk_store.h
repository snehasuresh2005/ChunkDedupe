#ifndef CHUNK_STORE_H
#define CHUNK_STORE_H

#include "chunker.h"
#include <string>
#include <vector>
#include <cstdint>

namespace chunkdedupe {

struct FileManifest {
    std::string file_id;
    std::string original_filename;
    uint64_t total_size = 0;
    std::string original_sha256;
    std::vector<std::string> chunk_hashes;
    std::vector<uint32_t> chunk_lengths;
};

class ChunkStore {
public:
    explicit ChunkStore(std::string store_dir = "store");

    // Returns absolute or relative path for a chunk file
    std::string GetChunkPath(const std::string& hash) const;

    // Returns path for a file manifest
    std::string GetManifestPath(const std::string& file_id) const;

    // Writes raw chunk bytes to disk
    bool WriteChunk(const ChunkInfo& chunk);

    // Reads raw chunk bytes from disk
    bool ReadChunk(const std::string& hash, std::vector<uint8_t>& out_bytes) const;

    // Saves a file manifest
    bool SaveManifest(const FileManifest& manifest) const;

    // Loads a file manifest
    bool LoadManifest(const std::string& file_id, FileManifest& out_manifest) const;

    // Reconstructs the original file byte-for-byte from manifest and stored chunks.
    // Calculates SHA-256 of reconstructed file and returns true if matches manifest original_sha256.
    bool ReconstructFile(
        const std::string& file_id,
        const std::string& output_path,
        std::string& out_reconstructed_sha256,
        bool* out_checksum_matched = nullptr
    ) const;

    const std::string& GetStoreDir() const { return m_store_dir; }

private:
    std::string m_store_dir;
};

} // namespace chunkdedupe

#endif // CHUNK_STORE_H
