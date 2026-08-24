include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(
    dedup_tests
    test_chunker.cpp
    test_dedup_index.cpp
    test_integration.cpp
)

target_include_directories(dedup_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(dedup_tests PRIVATE chunkdedupe_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(dedup_tests)

# Set labels for test categorization
set_tests_properties(ChunkerTest.Determinism PROPERTIES LABELS "pre-checkin")
set_tests_properties(ChunkerTest.MinMaxLimits PROPERTIES LABELS "pre-checkin")
set_tests_properties(ChunkerTest.EmptyFile PROPERTIES LABELS "pre-checkin")
set_tests_properties(DedupIndexTest.RefCounting PROPERTIES LABELS "pre-checkin")
set_tests_properties(DedupIndexTest.DuplicateIngestion PROPERTIES LABELS "pre-checkin")

set_tests_properties(IntegrationTest.IdenticalFileTwice PROPERTIES LABELS "post-checkin")
set_tests_properties(IntegrationTest.SingleByteModification PROPERTIES LABELS "post-checkin")
set_tests_properties(IntegrationTest.MultithreadedCorrectness PROPERTIES LABELS "post-checkin")
set_tests_properties(IntegrationTest.ReconstructionRoundtrip PROPERTIES LABELS "post-checkin")
