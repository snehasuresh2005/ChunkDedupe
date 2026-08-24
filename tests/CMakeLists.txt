include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
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

# Define Pre-check-in Unit Tests
add_test(NAME ChunkerTest.Determinism COMMAND dedup_tests --gtest_filter=ChunkerTest.Determinism)
set_tests_properties(ChunkerTest.Determinism PROPERTIES LABELS "pre-checkin")

add_test(NAME ChunkerTest.MinMaxLimits COMMAND dedup_tests --gtest_filter=ChunkerTest.MinMaxLimits)
set_tests_properties(ChunkerTest.MinMaxLimits PROPERTIES LABELS "pre-checkin")

add_test(NAME ChunkerTest.EmptyFile COMMAND dedup_tests --gtest_filter=ChunkerTest.EmptyFile)
set_tests_properties(ChunkerTest.EmptyFile PROPERTIES LABELS "pre-checkin")

add_test(NAME DedupIndexTest.RefCounting COMMAND dedup_tests --gtest_filter=DedupIndexTest.RefCounting)
set_tests_properties(DedupIndexTest.RefCounting PROPERTIES LABELS "pre-checkin")

add_test(NAME DedupIndexTest.DuplicateIngestion COMMAND dedup_tests --gtest_filter=DedupIndexTest.DuplicateIngestion)
set_tests_properties(DedupIndexTest.DuplicateIngestion PROPERTIES LABELS "pre-checkin")

# Define Post-check-in Integration Tests
add_test(NAME IntegrationTest.IdenticalFileTwice COMMAND dedup_tests --gtest_filter=IntegrationTest.IdenticalFileTwice)
set_tests_properties(IntegrationTest.IdenticalFileTwice PROPERTIES LABELS "post-checkin")

add_test(NAME IntegrationTest.SingleByteModification COMMAND dedup_tests --gtest_filter=IntegrationTest.SingleByteModification)
set_tests_properties(IntegrationTest.SingleByteModification PROPERTIES LABELS "post-checkin")

add_test(NAME IntegrationTest.MultithreadedCorrectness COMMAND dedup_tests --gtest_filter=IntegrationTest.MultithreadedCorrectness)
set_tests_properties(IntegrationTest.MultithreadedCorrectness PROPERTIES LABELS "post-checkin")

add_test(NAME IntegrationTest.ReconstructionRoundtrip COMMAND dedup_tests --gtest_filter=IntegrationTest.ReconstructionRoundtrip)
set_tests_properties(IntegrationTest.ReconstructionRoundtrip PROPERTIES LABELS "post-checkin")
