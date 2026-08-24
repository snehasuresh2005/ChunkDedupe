#include <gtest/gtest.h>
#include "chunker.h"
#include "hasher.h"
#include "dedup_index.h"
#include "chunk_store.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <algorithm>

namespace fs = std::filesystem;
using namespace chunkdedupe;

struct TestIngestResult {
    uint64_t new_bytes;
    std::vector<std::string> chunk_hashes;
};

static TestIngestResult IngestTestBuffer(
    const std::vector<uint8_t>& bytes,
    const std::string& file_id,
    DedupIndex& index,
    ChunkStore& store,
    size_t num_threads
) {
    Chunker chunker;
    size_t total_size = bytes.size();
    std::vector<ChunkInfo> ordered_chunks;

    if (total_size == 0) {
        ordered_chunks = chunker.ChunkBuffer(bytes);
    } else if (num_threads <= 1 || total_size < 4096 * 4) {
        ordered_chunks = chunker.ChunkBuffer(bytes);
    } else {
        size_t num_segments = num_threads;
        std::vector<size_t> boundaries(num_segments + 1);
        boundaries[0] = 0;
        boundaries[num_segments] = total_size;

        for (size_t t = 1; t < num_segments; ++t) {
            size_t nominal_split = (t * total_size) / num_segments;
            boundaries[t] = chunker.FindNextBoundary(bytes.data(), total_size, nominal_split);
        }

        for (size_t t = 1; t <= num_segments; ++t) {
            if (boundaries[t] < boundaries[t - 1]) {
                boundaries[t] = boundaries[t - 1];
            }
        }

        std::vector<std::vector<ChunkInfo>> segment_chunks(num_segments);
        std::vector<std::thread> workers;

        for (size_t t = 0; t < num_segments; ++t) {
            size_t seg_start = boundaries[t];
            size_t seg_len = boundaries[t + 1] - boundaries[t];

            if (seg_len == 0) continue;

            workers.emplace_back([&, t, seg_start, seg_len]() {
                segment_chunks[t] = chunker.ChunkBuffer(
                    bytes.data() + seg_start,
                    seg_len,
                    seg_start
                );
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        for (size_t t = 0; t < num_segments; ++t) {
            for (auto& chk : segment_chunks[t]) {
                ordered_chunks.push_back(std::move(chk));
            }
        }
    }

    uint64_t new_unique_bytes = 0;
    FileManifest manifest;
    manifest.file_id = file_id;
    manifest.original_filename = file_id;
    manifest.total_size = total_size;
    manifest.original_sha256 = ComputeSHA256(bytes);

    TestIngestResult res;
    for (const auto& chunk : ordered_chunks) {
        manifest.chunk_hashes.push_back(chunk.hash);
        manifest.chunk_lengths.push_back(chunk.length);
        res.chunk_hashes.push_back(chunk.hash);

        std::string rel_path;
        bool is_new = index.InsertOrRef(chunk, rel_path);
        if (is_new) {
            new_unique_bytes += chunk.length;
            store.WriteChunk(chunk);
        }
    }

    index.IncrementFileCount();
    store.SaveManifest(manifest);
    res.new_bytes = new_unique_bytes;
    return res;
}

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_test_dir = "test_store_tmp";
        fs::remove_all(m_test_dir);
    }

    void TearDown() override {
        fs::remove_all(m_test_dir);
    }

    std::string m_test_dir;
};

TEST_F(IntegrationTest, IdenticalFileTwice) {
    DedupIndex index;
    ChunkStore store(m_test_dir);

    std::vector<uint8_t> data(100 * 1024); // 100 KB
    std::mt19937 rng(12345);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(rng() % 256);

    auto res1 = IngestTestBuffer(data, "file1", index, store, 1);
    EXPECT_GT(res1.new_bytes, 0u);

    auto res2 = IngestTestBuffer(data, "file2", index, store, 1);
    EXPECT_EQ(res2.new_bytes, 0u); // Second ingestion adds 0 new unique bytes
}

TEST_F(IntegrationTest, SingleByteModification) {
    DedupIndex index;
    ChunkStore store(m_test_dir);

    std::vector<uint8_t> original(200 * 1024); // 200 KB
    std::mt19937 rng(999);
    for (size_t i = 0; i < original.size(); ++i) original[i] = static_cast<uint8_t>(rng() % 256);

    std::vector<uint8_t> modified = original;
    size_t mid = modified.size() / 2;
    modified[mid] ^= 0xFF; // Modify single byte in middle

    auto res_orig = IngestTestBuffer(original, "orig", index, store, 1);
    auto res_mod = IngestTestBuffer(modified, "mod", index, store, 1);

    // Most chunks should match original
    size_t matching_chunks = 0;
    for (const auto& h : res_mod.chunk_hashes) {
        if (std::find(res_orig.chunk_hashes.begin(), res_orig.chunk_hashes.end(), h) != res_orig.chunk_hashes.end()) {
            matching_chunks++;
        }
    }

    double match_ratio = static_cast<double>(matching_chunks) / res_mod.chunk_hashes.size();
    EXPECT_GT(match_ratio, 0.70); // At least 70% of chunks match original
}

TEST_F(IntegrationTest, MultithreadedCorrectness) {
    std::vector<uint8_t> data(500 * 1024); // 500 KB
    std::mt19937 rng(777);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(rng() % 256);

    DedupIndex index1, index4, index8;
    ChunkStore store1(m_test_dir + "_1"), store4(m_test_dir + "_4"), store8(m_test_dir + "_8");

    auto res1 = IngestTestBuffer(data, "f1", index1, store1, 1);
    auto res4 = IngestTestBuffer(data, "f4", index4, store4, 4);
    auto res8 = IngestTestBuffer(data, "f8", index8, store8, 8);

    EXPECT_EQ(res1.chunk_hashes.size(), res4.chunk_hashes.size());
    EXPECT_EQ(res1.chunk_hashes.size(), res8.chunk_hashes.size());
    EXPECT_EQ(res1.chunk_hashes, res4.chunk_hashes);
    EXPECT_EQ(res1.chunk_hashes, res8.chunk_hashes);

    fs::remove_all(m_test_dir + "_1");
    fs::remove_all(m_test_dir + "_4");
    fs::remove_all(m_test_dir + "_8");
}

TEST_F(IntegrationTest, ReconstructionRoundtrip) {
    DedupIndex index;
    ChunkStore store(m_test_dir);

    // Small file (15 KB) and multi-MB file (2.5 MB)
    std::vector<size_t> test_sizes = {15 * 1024, 2500 * 1024};

    for (size_t sz : test_sizes) {
        std::vector<uint8_t> data(sz);
        std::mt19937 rng(static_cast<uint32_t>(sz));
        for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(rng() % 256);

        std::string original_sha256 = ComputeSHA256(data);
        std::string file_id = "test_roundtrip_" + std::to_string(sz);

        IngestTestBuffer(data, file_id, index, store, 2);

        std::string output_path = m_test_dir + "/reconstructed_" + std::to_string(sz) + ".bin";
        std::string reconstructed_sha256;
        bool checksum_matched = false;

        bool ok = store.ReconstructFile(file_id, output_path, reconstructed_sha256, &checksum_matched);

        EXPECT_TRUE(ok);
        EXPECT_TRUE(checksum_matched);
        EXPECT_EQ(original_sha256, reconstructed_sha256);

        // Byte-for-byte check
        std::ifstream rec_file(output_path, std::ios::binary);
        std::vector<uint8_t> rec_data((std::istreambuf_iterator<char>(rec_file)), std::istreambuf_iterator<char>());
        EXPECT_EQ(data.size(), rec_data.size());
        EXPECT_EQ(data, rec_data);
    }
}
