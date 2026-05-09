cmake_minimum_required(VERSION 3.20)

find_package(GTest REQUIRED)
find_package(httplib REQUIRED)
find_package(nlohmann_json REQUIRED)

# ── Version tests (always runnable) ──────────────────────────────────────────
add_executable(${PROJECT_NAME}_tests
    src/test_http_client_unit.cpp
    src/test_helpers_unit.cpp)

target_compile_features(${PROJECT_NAME}_tests PRIVATE cxx_std_20)
set_target_properties(${PROJECT_NAME}_tests PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(${PROJECT_NAME}_tests
  PRIVATE
    ${PROJECT_NAME}::${PROJECT_NAME}
    GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}_tests)

# ── Integration tests (require Agamemnon + NATS running) ─────────────────────
add_executable(${PROJECT_NAME}_integration_tests
    src/test_chaos_api.cpp
    src/test_payload_fuzzing.cpp
    src/test_malformed_messages.cpp
    src/test_protocol_correctness.cpp
    src/test_chaos_resilience.cpp)

target_compile_features(${PROJECT_NAME}_integration_tests PRIVATE cxx_std_20)
set_target_properties(${PROJECT_NAME}_integration_tests PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(${PROJECT_NAME}_integration_tests
  PRIVATE
    ${PROJECT_NAME}::${PROJECT_NAME}
    httplib::httplib
    nlohmann_json::nlohmann_json
    GTest::gtest_main)

gtest_discover_tests(${PROJECT_NAME}_integration_tests
    PROPERTIES LABELS "REQUIRES_NATS")

# R04_QueueStarve_ConsumerStalls additionally requires a live myrmidon pull consumer.
# Without one, the task will never advance to 'completed' and the recovery assertion
# will always time out. Apply the REQUIRES_MYRMIDON label after test discovery so that
# CI environments without a myrmidon agent can skip this test explicitly:
#   ctest --label-exclude REQUIRES_MYRMIDON
set_tests_properties(
    "${PROJECT_NAME}_integration_tests.ChaosResilienceTest/R04_QueueStarve_ConsumerStalls"
    PROPERTIES LABELS "REQUIRES_NATS;REQUIRES_MYRMIDON")
