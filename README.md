# ChunkDedupe Engine

ChunkDedupe is a multi-threaded C++ deduplication engine that uses content-defined chunking (rolling hash + SHA-256) to find duplicate data and reclaim storage space, with a benchmarked concurrent ingestion pipeline and an automated pre-/post-check-in test suite.

---

## System Architecture

```
+-----------------------------------------------------------------------------------+
|                                 Input File Stream                                 |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|  Content-Defined Chunker (Rabin-Karp Rolling Hash over Sliding Window, 4KB Target) |
|  - Minimum Chunk Limit: 1 KB | Maximum Chunk Limit: 16 KB                        |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                           SHA-256 Hash Computation                                |
+-----------------------------------------------------------------------------------+
                                          |
                        +-----------------+-----------------+
                        |                                   |
                        v                                   v
+---------------------------------------+ +---------------------------------------+
| Deduplication Index (Thread-Safe Map) | | Content-Addressable Storage Engine    |
| - Hash Lookup & Ref-Counting          | | - Unique Chunks: store/ab/<hash>.chunk|
| - Global Storage Statistics           | | - Manifests: store/manifests/<id>.json|
| - Save/Load Persistence               | | - Reconstruction & Integrity Check    |
+---------------------------------------+ +---------------------------------------+
```

### Pipeline Flow
1. **Rolling Hashing & Boundary Detection**: A Rabin-Karp polynomial rolling hash slides a 64-byte window across the byte stream. When `rolling_hash % TARGET_CHUNK_SIZE == 0` (with 1KB min and 16KB max boundaries), a chunk boundary is declared.
2. **Crypto Hashing (SHA-256)**: Each chunk is hashed using SHA-256 to generate a 64-character hex identifier.
3. **Index Lookup & Storage**:
   - If the chunk SHA-256 exists in the index, reference counts are updated without re-writing bytes.
   - If new, the chunk raw bytes are persisted to `store/<hash_prefix>/<hash>.chunk`.
4. **File Manifest & Reconstruction**: A JSON manifest records the ordered chunk hashes for every file. The `reconstruct` command rebuilds original files byte-for-byte and verifies SHA-256 checksum integrity.

---

## Build & Run Instructions

### Prerequisites
- C++17 compliant compiler (`g++`, `clang++`, or `MSVC`)
- CMake 3.14+
- Git & Ninja / Make

### Building the Project

```bash
# Clone repository
git clone https://github.com/snehasuresh2005/ChunkDedup.git
cd ChunkDedup

# Configure CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build executable and test suite
cmake --build build -j4
```

### Running Tests

```bash
# Run fast unit tests (Pre-check-in)
ctest --test-dir build -C Release -L pre-checkin --output-on-failure

# Run integration tests (Post-check-in)
ctest --test-dir build -C Release -L post-checkin --output-on-failure
```

---

## CLI Usage & Real Output

Generate synthetic test files using the provided helper script:

```bash
./scripts/generate_test_files.sh    # On Linux/macOS
# OR
.\scripts\generate_test_files.ps1   # On Windows PowerShell
```

### 1. Ingest Original File (`dedup store`)

```
$ ./dedup store sample_data/random_5mb.bin --threads 4

Ingested file        : sample_data/random_5mb.bin (ID: random_5mb.bin)
Threads used         : 4
Bytes ingested       : 5242880 bytes
New unique stored    : 5242880 bytes (1054 new chunks)
File dedup ratio     : 0.00 %
Aggregate store ratio: 0.00 %
Time taken           : 594.16 ms (8.42 MB/s)
```

### 2. Ingest Duplicate File (`dedup store`)

```
$ ./dedup store sample_data/random_5mb_dup.bin --threads 4

Ingested file        : sample_data/random_5mb_dup.bin (ID: random_5mb_dup.bin)
Threads used         : 4
Bytes ingested       : 5242880 bytes
New unique stored    : 0 bytes (0 new chunks)
File dedup ratio     : 100.00 %
Aggregate store ratio: 50.00 %
Time taken           : 48.71 ms (102.64 MB/s)
```

### 3. Ingest Near-Duplicate File (Single Byte Modification)

```
$ ./dedup store sample_data/random_5mb_near_dup.bin --threads 4

Ingested file        : sample_data/random_5mb_near_dup.bin (ID: random_5mb_near_dup.bin)
Threads used         : 4
Bytes ingested       : 5242880 bytes
New unique stored    : 7570 bytes (1 new chunks)
File dedup ratio     : 99.86 %
Aggregate store ratio: 66.62 %
Time taken           : 66.76 ms (74.89 MB/s)
```

### 4. Reconstruct File & Verify Checksum (`dedup reconstruct`)

```
$ ./dedup reconstruct random_5mb.bin sample_data/reconstructed_random_5mb.bin

Reconstruction complete for file_id: random_5mb.bin
Output path      : sample_data/reconstructed_random_5mb.bin
SHA-256 Checksum : 62753787bf660a11151e5d9b55c0fb9ff430f83c0986046d76f15a469357158a
Checksum status  : MATCHED (VERIFIED)
Time taken       : 111.41 ms
```

### 5. Display Storage Statistics (`dedup stats`)

```
$ ./dedup stats

========================================
        ChunkDedupe Store Stats         
========================================
Total files ingested   : 3
Total logical bytes    : 15728640 bytes
Total unique chunks    : 1055
Total unique bytes     : 5250450 bytes
Aggregate dedup ratio  : 66.62 %
Compression factor     : 3.00 x
========================================
```

---

## Multithreaded Benchmark Results

Benchmark execution on an 8.54 MB synthetic repeating pattern file (`sample_data/repeated_pattern_8mb.bin`):

```
$ ./dedup benchmark sample_data/repeated_pattern_8mb.bin --threads-list 1,2,4,8

+--------------+-----------------+-------------------+-----------------+
| Thread Count | Wall Time (ms)  | Throughput (MB/s) | Chunks Produced |
+--------------+-----------------+-------------------+-----------------+
|            1 |           91.33 |             93.56 |            7987 |
|            2 |           72.62 |            117.66 |            7987 |
|            4 |           71.56 |            119.40 |            7987 |
|            8 |           82.28 |            103.85 |            7987 |
+--------------+-----------------+-------------------+-----------------+
```

> **Note on Benchmark Performance**:
> The benchmark results demonstrate multithreaded speedup up to 4 threads (scaling throughput from 93.56 MB/s to 119.40 MB/s). At 8 threads on laptop-grade hardware, memory bandwith saturation and core scheduling overhead slightly flatten scaling. On high-core server hardware (16+ dedicated physical cores), scaling continues across higher thread counts.

---

## Design Decisions & Trade-Offs

### 1. Fixed-Size vs Content-Defined Chunking (CDC)
- **Fixed-Size Chunking**: Fast and trivial to implement, but vulnerable to the "boundary shift problem". Inserting a single byte at the beginning of a 1GB file changes the offset of every downstream chunk, dropping the deduplication ratio to 0%.
- **Content-Defined Chunking (CDC)**: Computes boundaries based on local content (rolling hash pattern matches). A byte insertion only modifies the immediate chunk containing the edit; all subsequent chunks align to identical boundaries, preserving high deduplication ratios (>99% on near-duplicates).

### 2. Chunk Size Selection
- **Target Size (4KB)**: Provides an optimal balance between deduplication granular efficiency and metadata storage overhead. Smaller chunks (e.g. 512B) yield slightly higher dedup ratios but dramatically increase index memory footprint.
- **Min Size (1KB) & Max Size (16KB)**: Prevents degenerate small chunks (reducing IOPS overhead) and caps maximum chunk size to prevent memory spikes.

### 3. Concurrency & Lock Contention
- **Segment-Aligned CDC**: To guarantee that $N$-thread chunking produces the **exact same chunk boundaries** as 1-thread chunking, segment boundaries are pre-aligned to true content boundaries (`FindNextBoundary`).
- **Mutex Protection**: Currently, index updates are protected by a single `std::mutex`. At extreme core counts (64+ threads), mutex contention could become a bottleneck. This can be improved by **sharded locking** (e.g. 256 bucket locks based on hash prefix) or **lock-free concurrent hash maps**.

### 4. Persistence & Crash Safety
- **Current Model**: Metadata persists to disk via JSON index (`store/index.json`) and per-file manifest files.
- **Crash Safety Requirements**: In a production enterprise storage engine, sudden power loss during ingestion could leave orphan chunks or half-written manifests. Production systems require **Write-Ahead Logging (WAL)**, atomic directory renames, and checksummed block allocations.

---

## What I'd Do With More Time

1. **Write-Ahead Logging (WAL) & Crash Recovery**: Implement atomic WAL logs for all index operations so the storage store can recover seamlessly from mid-operation crashes.
2. **On-Disk Persistent Index**: Replace the in-memory `std::unordered_map` with an embedded LSM-Tree / B-Tree key-value database (e.g., RocksDB or LMDB) to handle billions of unique chunks without consuming host RAM.
3. **Delta Encoding**: Compress similar (near-duplicate) chunks using similarity hashing (e.g. MinHash / SSDE) and store diff updates rather than full raw chunks.
4. **Distributed Replication & Tiering**: Extend the chunk store to replicate chunk blocks across storage nodes over gRPC/NVMe-oF with S3 cold storage tiering.

---

## Repository Structure

```
ChunkDedupe/
  ├── CMakeLists.txt
  ├── include/
  │   ├── chunker.h
  │   ├── hasher.h
  │   ├── dedup_index.h
  │   └── chunk_store.h
  ├── src/
  │   ├── chunker.cpp
  │   ├── hasher.cpp
  │   ├── dedup_index.cpp
  │   ├── chunk_store.cpp
  │   └── main.cpp
  ├── tests/
  │   ├── CMakeLists.txt
  │   ├── test_chunker.cpp
  │   ├── test_dedup_index.cpp
  │   └── test_integration.cpp
  ├── .github/workflows/
  │   └── ci.yml
  ├── scripts/
  │   ├── generate_test_files.sh
  │   └── generate_test_files.ps1
  └── README.md
```
