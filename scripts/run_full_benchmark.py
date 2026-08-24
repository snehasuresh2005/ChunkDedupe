import os
import sys
import time
import subprocess

def run_cmd(cmd, cwd=None, env=None, timeout=60):
    print(f"\n[BENCHMARK PIPELINE] Executing: {cmd}")
    start = time.time()
    try:
        res = subprocess.run(cmd, shell=True, cwd=cwd, env=env, timeout=timeout)
        elapsed = time.time() - start
        if res.returncode != 0:
            print(f"[ERROR] Command failed with exit code {res.returncode} ({elapsed:.1f}s)")
            sys.exit(res.returncode)
        else:
            print(f"[SUCCESS] Step finished cleanly in {elapsed:.1f}s")
    except subprocess.TimeoutExpired:
        print(f"[TIMEOUT] Command exceeded timeout of {timeout}s! Terminating run...")
        sys.exit(1)

def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    os.chdir(root)

    env = dict(os.environ)
    if os.name == 'nt':
        env["PATH"] = r"C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + env.get("PATH", "")

    # Locate python executable with matplotlib
    python_bin = "py" if os.name == 'nt' else "python3"

    print("\n========================================================")
    print("      ChunkDedupe Fast Scoped Master Benchmark Pipeline  ")
    print("========================================================\n")

    # Step 1: Build C++ Binaries
    print(">>> Step 1/4: Building C++ binaries...")
    if os.name == 'nt':
        build_cmd = 'g++.exe -std=c++17 -O2 -Iinclude src/hasher.cpp src/chunker.cpp src/dedup_index.cpp src/chunk_store.cpp src/main.cpp -o dedup.exe -lpsapi'
    else:
        build_cmd = 'g++ -std=c++17 -O2 -Iinclude src/hasher.cpp src/chunker.cpp src/dedup_index.cpp src/chunk_store.cpp src/main.cpp -o dedup'
    run_cmd(build_cmd, env=env, timeout=60)

    # Step 2: Generate Scoped Datasets (20MB files)
    print("\n>>> Step 2/4: Generating scoped benchmark corpora (20MB files)...")
    run_cmd(f"{python_bin} scripts/generate_datasets.py --max-file-size-mb 20", env=env, timeout=60)

    # Step 3: Run Benchmark Report (creates results.csv)
    print("\n>>> Step 3/4: Running engine benchmark suite & exporting results.csv...")
    exe = ".\\dedup.exe" if os.name == 'nt' else "./dedup"
    run_cmd(f"{exe} benchmark-report --out-csv benchmarks/results/results.csv", env=env, timeout=120)

    # Step 4: Generate Charts
    print("\n>>> Step 4/4: Generating performance PNG charts...")
    run_cmd(f"{python_bin} scripts/generate_charts.py", env=env, timeout=60)

    print("\n========================================================")
    print(" Scoped benchmark pipeline completed successfully!    ")
    print(" Results exported to:                                 ")
    print("  - Raw CSV metrics  : benchmarks/results/results.csv ")
    print("  - Summary report   : benchmarks/results/summary.md  ")
    print("  - Visualizations   : benchmarks/results/charts/    ")
    print("========================================================\n")

if __name__ == "__main__":
    main()
