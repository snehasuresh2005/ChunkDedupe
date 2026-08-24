import os
import sys
import random
import argparse

def main():
    parser = argparse.ArgumentParser(description="Generate benchmark corpora for ChunkDedupe")
    parser.add_argument("--max-file-size-mb", type=int, default=20, help="Max file size in MB per corpus version")
    args = parser.parse_args()

    file_size_bytes = args.max_file_size_mb * 1024 * 1024
    base_dir = "benchmarks/data"
    os.makedirs(f"{base_dir}/incremental_backup", exist_ok=True)
    os.makedirs(f"{base_dir}/random_baseline", exist_ok=True)

    print(f"Generating scoped benchmark datasets ({args.max_file_size_mb}MB per file)...")
    rng = random.Random(42)

    # 1. Incremental Backup Corpus (5 versions of 20 MB each = 100 MB logical)
    print(f"  - Generating 5 incremental backup versions ({args.max_file_size_mb} MB each)...")
    base_bytes = bytearray(rng.getrandbits(8) for _ in range(file_size_bytes))
    with open(f"{base_dir}/incremental_backup/backup_v0.bin", "wb") as f:
        f.write(base_bytes)

    curr_bytes = base_bytes
    for v in range(1, 5):
        mod_bytes = bytearray(curr_bytes)
        # Apply 2 unaligned insertions/deletions (causes boundary shifts)
        for _ in range(2):
            pos = rng.randint(2048, len(mod_bytes) - 32 * 1024)
            edit_type = rng.choice(["insert", "delete"])
            if edit_type == "insert":
                ins_data = bytearray(rng.getrandbits(8) for _ in range(713))
                mod_bytes = mod_bytes[:pos] + ins_data + mod_bytes[pos:]
            elif edit_type == "delete":
                mod_bytes = mod_bytes[:pos] + mod_bytes[pos + 511:]

        if len(mod_bytes) > file_size_bytes:
            mod_bytes = mod_bytes[:file_size_bytes]
        elif len(mod_bytes) < file_size_bytes:
            mod_bytes.extend(bytearray(file_size_bytes - len(mod_bytes)))

        with open(f"{base_dir}/incremental_backup/backup_v{v}.bin", "wb") as f:
            f.write(mod_bytes)
        curr_bytes = mod_bytes

    # 2. Random Baseline (1 version of 20 MB)
    print(f"  - Generating random incompressible baseline ({args.max_file_size_mb} MB)...")
    rand_bytes = bytearray(rng.getrandbits(8) for _ in range(file_size_bytes))
    with open(f"{base_dir}/random_baseline/random_20mb.bin", "wb") as f:
        f.write(rand_bytes)

    print("Datasets generated successfully under benchmarks/data/!")

if __name__ == "__main__":
    main()
