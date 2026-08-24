#include <gtest/gtest.h>
#include "chunker.h"
#include "hasher.h"
#include <random>
#include <vector>

using namespace chunkdedupe;

class ChunkerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Generate deterministic test pattern
        m_sample_data.resize(50 * 1024); // 50 KB
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 255);
        for (size_t i = 0; i < m_sample_data.size(); ++i) {
            m_sample_data[i] = static_cast<uint8_t>(dist(rng));
        }
    }

    std::vector<uint8_t> m_sample_data;
};

TEST_F(ChunkerTest, Determinism) {
    Chunker chunker;
    auto chunks1 = chunker.ChunkBuffer(m_sample_data);
    auto chunks2 = chunker.ChunkBuffer(m_sample_data);

    ASSERT_FALSE(chunks1.empty());
    ASSERT_EQ(chunks1.size(), chunks2.size());

    for (size_t i = 0; i < chunks1.size(); ++i) {
        EXPECT_EQ(chunks1[i].hash, chunks2[i].hash);
        EXPECT_EQ(chunks1[i].offset, chunks2[i].offset);
        EXPECT_EQ(chunks1[i].length, chunks2[i].length);
    }
}

TEST_F(ChunkerTest, MinMaxLimits) {
    ChunkerOptions opts;
    opts.min_chunk_size = 512;
    opts.max_chunk_size = 2048;
    opts.target_chunk_size = 1024;
    Chunker chunker(opts);

    auto chunks = chunker.ChunkBuffer(m_sample_data);
    ASSERT_FALSE(chunks.empty());

    for (size_t i = 0; i < chunks.size(); ++i) {
        if (i < chunks.size() - 1) { // Non-final chunks must satisfy min limit
            EXPECT_GE(chunks[i].length, opts.min_chunk_size);
        }
        EXPECT_LE(chunks[i].length, opts.max_chunk_size);
    }
}

TEST_F(ChunkerTest, EmptyFile) {
    Chunker chunker;
    std::vector<uint8_t> empty_data;
    auto chunks = chunker.ChunkBuffer(empty_data);
    EXPECT_TRUE(chunks.empty());
}
