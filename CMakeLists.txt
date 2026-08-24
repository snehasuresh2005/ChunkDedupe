cmake_minimum_required(VERSION 3.14)
project(ChunkDedupe VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Library sources
set(LIB_SOURCES
    src/hasher.cpp
    src/chunker.cpp
    src/dedup_index.cpp
    src/chunk_store.cpp
)

# Build library
add_library(chunkdedupe_lib STATIC ${LIB_SOURCES})
target_include_directories(chunkdedupe_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Build main executable
add_executable(dedup src/main.cpp)
target_link_libraries(dedup PRIVATE chunkdedupe_lib)

# Testing support
enable_testing()
add_subdirectory(tests)
