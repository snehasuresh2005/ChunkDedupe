#include <gtest/gtest.h>
#include "dedup_index.h"
#include "hasher.h"

using namespace chunkdedupe;

TEST(DedupIndexTest, RefCounting) {
    DedupIndex index;
    ChunkInfo chunk1;
    chunk1.data = {'A', 'B', 'C', 'D'};
    chunk1.length = 4;
    chunk1.offset = 0;
    chunk1.hash = ComputeSHA256(chunk1.data);

    std::string rel_path1, rel_path2;
    bool is_new1 = index.InsertOrRef(chunk1, rel_path1);
    EXPECT_TRUE(is_new1);

    bool is_new2 = index.InsertOrRef(chunk1, rel_path2);
    EXPECT_FALSE(is_new2);
    EXPECT_EQ(rel_path1, rel_path2);

    StorageStats stats = index.GetStats();
    EXPECT_EQ(stats.total_unique_chunks, 1u);
    EXPECT_EQ(stats.total_unique_bytes, 4u);
    EXPECT_EQ(stats.total_logical_bytes, 8u);
}

TEST(DedupIndexTest, DuplicateIngestion) {
    DedupIndex index;
    ChunkInfo c1{"hash1", 0, 100, {'x'}};
    ChunkInfo c2{"hash2", 100, 200, {'y'}};

    std::string p1, p2;
    EXPECT_TRUE(index.InsertOrRef(c1, p1));
    EXPECT_TRUE(index.InsertOrRef(c2, p2));

    StorageStats stats = index.GetStats();
    EXPECT_EQ(stats.total_unique_chunks, 2u);
    EXPECT_EQ(stats.total_unique_bytes, 300u);

    ChunkMeta meta;
    EXPECT_TRUE(index.GetChunkMeta("hash1", meta));
    EXPECT_EQ(meta.ref_count, 1u);

    EXPECT_FALSE(index.InsertOrRef(c1, p1));
    EXPECT_TRUE(index.GetChunkMeta("hash1", meta));
    EXPECT_EQ(meta.ref_count, 2u);
}
