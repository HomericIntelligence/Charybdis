cmake_minimum_required(VERSION 3.20)

find_package(GTest REQUIRED)
find_package(httplib REQUIRED)
find_package(nlohmann_json REQUIRED)

# ── Version tests (always runnable) ──────────────────────────────────────────
add_executable(${PROJECT_NAME}_tests
    src/test_http_client_unit.cpp
    src/test_http_client_retry_unit.cpp
    src/test_helpers_unit.cpp
    src/test_chaos_audit_unit.cpp
    src/test_http_client_timeout_unit.cpp)

target_compile_features(${PROJECT_NAME}_tests PRIVATE cxx_std_20)
set_target_properties(${PROJECT_NAME}_tests PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(${PROJECT_NAME}_tests
  PRIVATE
    ${PROJECT_NAME}::${PROJECT_NAME}
    GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}_tests)

# ── CMake module tests (shell-driven, no binary target) ─────────────────────
# Guards cmake/Sanitizers.cmake's compiler version check (issue #76) by
# configuring throwaway projects against fake compiler identities.
add_test(
  NAME sanitizers_version_guard
  COMMAND bash ${PROJECT_SOURCE_DIR}/test/cmake/test_sanitizers_version_guard.sh)

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

# Discover integration tests, partitioning by capability requirements.
# gtest_discover_tests runs POST_BUILD, so set_tests_properties cannot be used
# after the call — tests don't exist at configure time. Instead, use separate
# discovery calls with gtest_filter to partition the test suite by labels.
#
# Partition:
#   * .nats       — needs Agamemnon + NATS only
#   * .supervisor — additionally needs an external restart supervisor (R03)
#   * .myrmidon   — additionally needs a live myrmidon pull consumer (R04)
#   * .latency    — additionally needs Agamemnon's latency fault applied to
#                   CHAOS_LATENCY_PROBE_POST_PATH (R02, see #95)
set(_charybdis_supervisor_filter "ChaosResilienceTest.R03KillServiceHealthDegrades")
set(_charybdis_myrmidon_filter "ChaosResilienceTest.R04QueueStarveConsumerStalls")
set(_charybdis_latency_filter "ChaosResilienceTest.R02LatencyInjectionSlowResponse")

# Configure-time guard: every ChaosResilience test name listed below must be
# known, and each partition filter must match one of them exactly. A typo in a
# filter (or a renamed test) trips this and stops configuration, instead of
# silently leaking the test into the default .nats slice.
set(_charybdis_all_chaos_tests
    "ChaosResilienceTest.R01NetworkPartitionDegradedHealth"
    "ChaosResilienceTest.R02LatencyInjectionSlowResponse"
    "ChaosResilienceTest.R03KillServiceHealthDegrades"
    "ChaosResilienceTest.R04QueueStarveConsumerStalls"
    "ChaosResilienceTest.R05MultiFaultStackedAndClearAll")
set(_charybdis_partitioned_tests
    "${_charybdis_supervisor_filter}"
    "${_charybdis_myrmidon_filter}"
    "${_charybdis_latency_filter}")
foreach(_t IN LISTS _charybdis_partitioned_tests)
  if(NOT _t IN_LIST _charybdis_all_chaos_tests)
    message(FATAL_ERROR
        "Partition filter '${_t}' does not match any known ChaosResilience "
        "test; update _charybdis_all_chaos_tests or fix the typo.")
  endif()
endforeach()

gtest_discover_tests(${PROJECT_NAME}_integration_tests
    TEST_SUFFIX .nats
    EXTRA_ARGS "--gtest_filter=-${_charybdis_supervisor_filter}:${_charybdis_myrmidon_filter}:${_charybdis_latency_filter}"
    PROPERTIES LABELS "REQUIRES_NATS")

# R03KillServiceHealthDegrades additionally requires an external restart supervisor
# (systemd, Kubernetes, Docker --restart=always) to bring Agamemnon back after the
# kill fault. Without one, the process stays dead and the recovery assertion will
# always time out. CI environments without a supervisor can skip with:
#   ctest --label-exclude REQUIRES_RESTART_SUPERVISOR
# Recovery timeout is tunable via CHAOS_RECOVERY_TIMEOUT_S (seconds).
gtest_discover_tests(${PROJECT_NAME}_integration_tests
    TEST_SUFFIX .supervisor
    EXTRA_ARGS "--gtest_filter=${_charybdis_supervisor_filter}"
    PROPERTIES LABELS "REQUIRES_NATS;REQUIRES_RESTART_SUPERVISOR")

# R04QueueStarveConsumerStalls additionally requires a live myrmidon pull consumer.
# Without one, the task will never advance to 'completed' and the recovery assertion
# will always time out. CI environments without a myrmidon agent can skip with:
#   ctest --label-exclude REQUIRES_MYRMIDON
gtest_discover_tests(${PROJECT_NAME}_integration_tests
    TEST_SUFFIX .myrmidon
    EXTRA_ARGS "--gtest_filter=${_charybdis_myrmidon_filter}"
    PROPERTIES LABELS "REQUIRES_NATS;REQUIRES_MYRMIDON")

# R02LatencyInjectionSlowResponse additionally requires Agamemnon's latency
# fault to apply delay on CHAOS_LATENCY_PROBE_POST_PATH (default POST /v1/teams).
# Environments where latency targets only NATS-internal paths cannot observe
# the effect and should skip with:
#   ctest --label-exclude REQUIRES_LATENCY_PROBE
gtest_discover_tests(${PROJECT_NAME}_integration_tests
    TEST_SUFFIX .latency
    EXTRA_ARGS "--gtest_filter=${_charybdis_latency_filter}"
    PROPERTIES LABELS "REQUIRES_NATS;REQUIRES_LATENCY_PROBE")
