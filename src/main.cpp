#include "chunker.h"
#include "hasher.h"
#include "dedup_index.h"
#include "chunk_store.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <set>

namespace fs = std::filesystem;
using namespace chunkdedupe;

// Helper to sanitize filename into file_id
std::string GetFileId(const std::string& filepath) {
    fs::path p(filepath);
    std::string stem = p.filename().string();
    std::replace(stem.begin(), stem.end(), ' ', '_');
    return stem;
}

struct IngestResult {
    uint64_t total_bytes = 0;
    uint64_t new_unique_bytes = 0;
    uint64_t new_unique_chunks = 0;
    double time_ms = 0.0;
    double throughput_mbps = 0.0;
    std::vector<ChunkInfo> all_chunks;
};

IngestResult IngestBuffer(
    const std::vector<uint8_t>& file_bytes,
    const std::string& file_id,
    const std::string& original_filename,
    DedupIndex& index,
    ChunkStore& store,
    size_t num_threads
) {
    auto start_time = std::chrono::high_resolution_clock::now();

    Chunker chunker;
    size_t total_size = file_bytes.size();
    std::vector<ChunkInfo> ordered_chunks;

    if (total_size == 0) {
        ordered_chunks = chunker.ChunkBuffer(file_bytes);
    } else if (num_threads <= 1 || total_size < 4096 * 4) {
        ordered_chunks = chunker.ChunkBuffer(file_bytes);
    } else {
        // Multi-threaded chunking with CDC boundary alignment
        size_t num_segments = num_threads;
        std::vector<size_t> boundaries(num_segments + 1);
        boundaries[0] = 0;
        boundaries[num_segments] = total_size;

        for (size_t t = 1; t < num_segments; ++t) {
            size_t nominal_split = (t * total_size) / num_segments;
            boundaries[t] = chunker.FindNextBoundary(file_bytes.data(), total_size, nominal_split);
        }

        for (size_t t = 1; t <= num_segments; ++t) {
            if (boundaries[t] < boundaries[t - 1]) {
                boundaries[t] = boundaries[t - 1];
            }
        }

        std::vector<std::vector<ChunkInfo>> segment_chunks(num_segments);
        std::vector<std::thread> workers;
        workers.reserve(num_segments);

        for (size_t t = 0; t < num_segments; ++t) {
            size_t seg_start = boundaries[t];
            size_t seg_len = boundaries[t + 1] - boundaries[t];

            if (seg_len == 0) continue;

            workers.emplace_back([&, t, seg_start, seg_len]() {
                segment_chunks[t] = chunker.ChunkBuffer(
                    file_bytes.data() + seg_start,
                    seg_len,
                    seg_start,
                    file_bytes.data()
                );
            });
        }

        for (auto& worker : workers) {
            if (worker.joinable()) worker.join();
        }

        for (size_t t = 0; t < num_segments; ++t) {
            for (auto& chk : segment_chunks[t]) {
                ordered_chunks.push_back(std::move(chk));
            }
        }
    }

    uint64_t new_unique_bytes = 0;
    uint64_t new_unique_chunks = 0;
    FileManifest manifest;
    manifest.file_id = file_id;
    manifest.original_filename = original_filename;
    manifest.total_size = total_size;
    manifest.original_sha256 = ComputeSHA256(file_bytes);

    for (const auto& chunk : ordered_chunks) {
        manifest.chunk_hashes.push_back(chunk.hash);
        manifest.chunk_lengths.push_back(chunk.length);

        std::string rel_path;
        bool is_new = index.InsertOrRef(chunk, rel_path);
        if (is_new) {
            new_unique_bytes += chunk.length;
            new_unique_chunks++;
            store.WriteChunk(chunk);
        }
    }

    index.IncrementFileCount();
    store.SaveManifest(manifest);

    auto end_time = std::chrono::high_resolution_clock::now();
    double time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    double throughput = (total_size / (1024.0 * 1024.0)) / (time_ms / 1000.0);
    if (time_ms == 0.0) throughput = 0.0;

    IngestResult res;
    res.total_bytes = total_size;
    res.new_unique_bytes = new_unique_bytes;
    res.new_unique_chunks = new_unique_chunks;
    res.time_ms = time_ms;
    res.throughput_mbps = throughput;
    res.all_chunks = std::move(ordered_chunks);
    return res;
}

void PrintUsage() {
    std::cout << "ChunkDedupe - Content-Defined Chunking Deduplication Engine\n"
              << "Usage:\n"
              << "  dedup store <file> [--threads N]\n"
              << "  dedup reconstruct <file_id> <output_path>\n"
              << "  dedup stats\n"
              << "  dedup benchmark <file> [--threads-list 1,2,4,8]\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string subcommand = argv[1];

    std::string store_dir = "store";
    DedupIndex index;
    ChunkStore store(store_dir);
    index.LoadFromFile(store_dir + "/index.json");

    if (subcommand == "store") {
        if (argc < 3) {
            std::cerr << "Error: Missing file argument for 'store'\n";
            return 1;
        }
        std::string filepath = argv[2];
        size_t threads = 1;

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--threads" && i + 1 < argc) {
                threads = std::stoul(argv[++i]);
            }
        }

        std::ifstream infile(filepath, std::ios::binary);
        if (!infile.is_open()) {
            std::cerr << "Error: Cannot open file: " << filepath << "\n";
            return 1;
        }

        std::vector<uint8_t> file_bytes((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
        infile.close();

        std::string file_id = GetFileId(filepath);
        IngestResult res = IngestBuffer(file_bytes, file_id, filepath, index, store, threads);
        index.SaveToFile(store_dir + "/index.json");

        StorageStats stats = index.GetStats();
        double chunk_dedup_ratio = (res.total_bytes == 0) ? 0.0 : (1.0 - static_cast<double>(res.new_unique_bytes) / res.total_bytes) * 100.0;

        std::cout << "Ingested file        : " << filepath << " (ID: " << file_id << ")\n"
                  << "Threads used         : " << threads << "\n"
                  << "Bytes ingested       : " << res.total_bytes << " bytes\n"
                  << "New unique stored    : " << res.new_unique_bytes << " bytes (" << res.new_unique_chunks << " new chunks)\n"
                  << "File dedup ratio     : " << std::fixed << std::setprecision(2) << chunk_dedup_ratio << " %\n"
                  << "Aggregate store ratio: " << std::fixed << std::setprecision(2) << stats.GetDedupRatioPercent() << " %\n"
                  << "Time taken           : " << std::fixed << std::setprecision(2) << res.time_ms << " ms ("
                  << std::fixed << std::setprecision(2) << res.throughput_mbps << " MB/s)\n";

    } else if (subcommand == "reconstruct") {
        if (argc < 4) {
            std::cerr << "Error: Usage: dedup reconstruct <file_id> <output_path>\n";
            return 1;
        }
        std::string file_id = argv[2];
        std::string output_path = argv[3];

        auto start = std::chrono::high_resolution_clock::now();
        std::string reconstructed_sha256;
        bool matched = false;

        bool ok = store.ReconstructFile(file_id, output_path, reconstructed_sha256, &matched);
        auto end = std::chrono::high_resolution_clock::now();
        double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!ok) {
            std::cerr << "Error: Reconstruction failed for file_id: " << file_id << "\n";
            return 1;
        }

        std::cout << "Reconstruction complete for file_id: " << file_id << "\n"
                  << "Output path      : " << output_path << "\n"
                  << "SHA-256 Checksum : " << reconstructed_sha256 << "\n"
                  << "Checksum status  : " << (matched ? "MATCHED (VERIFIED)" : "MISMATCHED (CORRUPT)") << "\n"
                  << "Time taken       : " << std::fixed << std::setprecision(2) << time_ms << " ms\n";

    } else if (subcommand == "stats") {
        StorageStats stats = index.GetStats();
        std::cout << "========================================\n"
                  << "        ChunkDedupe Store Stats         \n"
                  << "========================================\n"
                  << "Total files ingested   : " << stats.total_files_ingested << "\n"
                  << "Total logical bytes    : " << stats.total_logical_bytes << " bytes\n"
                  << "Total unique chunks    : " << stats.total_unique_chunks << "\n"
                  << "Total unique bytes     : " << stats.total_unique_bytes << " bytes\n"
                  << "Aggregate dedup ratio  : " << std::fixed << std::setprecision(2) << stats.GetDedupRatioPercent() << " %\n"
                  << "Compression factor     : " << std::fixed << std::setprecision(2) << stats.GetCompressionFactor() << " x\n"
                  << "========================================\n";

    } else if (subcommand == "benchmark") {
        if (argc < 3) {
            std::cerr << "Error: Missing file argument for 'benchmark'\n";
            return 1;
        }
        std::string filepath = argv[2];
        std::vector<size_t> thread_list = {1, 2, 4, 8};
        size_t hw_conc = std::thread::hardware_concurrency();
        if (hw_conc > 0 && std::find(thread_list.begin(), thread_list.end(), hw_conc) == thread_list.end()) {
            thread_list.push_back(hw_conc);
        }

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--threads-list" && i + 1 < argc) {
                thread_list.clear();
                std::stringstream ss(argv[++i]);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    if (!item.empty()) thread_list.push_back(std::stoul(item));
                }
            }
        }

        std::ifstream infile(filepath, std::ios::binary);
        if (!infile.is_open()) {
            std::cerr << "Error: Cannot open file for benchmark: " << filepath << "\n";
            return 1;
        }
        std::vector<uint8_t> file_bytes((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
        infile.close();

        std::cout << "\nRunning Multithreaded Ingestion Benchmark\n"
                  << "Target File : " << filepath << " (" << std::fixed << std::setprecision(2)
                  << (file_bytes.size() / (1024.0 * 1024.0)) << " MB)\n\n";

        std::cout << "+--------------+-----------------+-------------------+-----------------+\n"
                  << "| Thread Count | Wall Time (ms)  | Throughput (MB/s) | Chunks Produced |\n"
                  << "+--------------+-----------------+-------------------+-----------------+\n";

        for (size_t t : thread_list) {
            DedupIndex bench_index;
            ChunkStore bench_store("store_bench_tmp");
            std::string file_id = "bench_" + std::to_string(t);

            IngestResult res = IngestBuffer(file_bytes, file_id, filepath, bench_index, bench_store, t);

            std::cout << "| " << std::setw(12) << t
                      << " | " << std::setw(15) << std::fixed << std::setprecision(2) << res.time_ms
                      << " | " << std::setw(17) << std::fixed << std::setprecision(2) << res.throughput_mbps
                      << " | " << std::setw(15) << res.all_chunks.size()
                      << " |\n";

            fs::remove_all("store_bench_tmp");
        }
        std::cout << "+--------------+-----------------+-------------------+-----------------+\n\n";

    } else {
        std::cerr << "Unknown subcommand: " << subcommand << "\n";
        PrintUsage();
        return 1;
    }

    return 0;
}
