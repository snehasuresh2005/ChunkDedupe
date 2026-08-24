#!/usr/bin/env bash
export PATH=/ucrt64/bin:/usr/bin:$PATH
export MSYSTEM=UCRT64

echo "=== Configuring CMake ==="
cmake -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=/ucrt64/bin/g++.exe -DCMAKE_BUILD_TYPE=Release 2>&1

echo "=== Building Project ==="
cmake --build build 2>&1

echo "=== Running Pre-check-in Tests ==="
ctest --test-dir build -C Release -L pre-checkin --output-on-failure 2>&1

echo "=== Running Post-check-in Integration Tests ==="
ctest --test-dir build -C Release -L post-checkin --output-on-failure 2>&1
