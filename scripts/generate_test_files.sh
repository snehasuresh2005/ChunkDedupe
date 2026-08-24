#!/usr/bin/env bash
set -e

mkdir -p sample_data

echo "Generating synthetic test files in ./sample_data/ ..."

# 1. Uniform random file (5 MB)
dd if=/dev/urandom of=sample_data/random_5mb.bin bs=1M count=5 2>/dev/null || \
  head -c 5242880 /dev/urandom > sample_data/random_5mb.bin

# 2. Duplicate file (copy of random_5mb.bin)
cp sample_data/random_5mb.bin sample_data/random_5mb_dup.bin

# 3. Near-duplicate file (random_5mb.bin with 10 bytes modified near offset 2MB)
cp sample_data/random_5mb.bin sample_data/random_5mb_near_dup.bin
printf '\xFF\xFE\xFD\xFC\xFB\xFA\xF9\xF8\xF7\xF6' | dd of=sample_data/random_5mb_near_dup.bin bs=1 seek=2097152 conv=notrunc 2>/dev/null

# 4. Pattern repeated file (8 MB)
perl -e 'print "ChunkDedupe test pattern repeating data line 1234567890\n" x 160000' > sample_data/repeated_pattern_8mb.bin

# 5. Small test file (100 KB)
dd if=/dev/urandom of=sample_data/small_100kb.bin bs=1K count=100 2>/dev/null || \
  head -c 102400 /dev/urandom > sample_data/small_100kb.bin

echo "Generated test files:"
ls -lh sample_data/
