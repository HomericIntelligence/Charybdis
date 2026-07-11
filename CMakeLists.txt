cmake_minimum_required(VERSION 3.20)

project(
  Charybdis
  VERSION 0.1.0
  DESCRIPTION "Chaos and resilience testing for HomericIntelligence"
  LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory and run CMake from there."
  )
endif()

include(cmake/StandardSettings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Coverage.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/StaticAnalyzers.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Doxygen.cmake)
include(cmake/SourcesAndHeaders.cmake)

configure_file(
  include/charybdis/version.hpp.in
  ${PROJECT_BINARY_DIR}/include/charybdis/version.hpp
  @ONLY)

find_package(httplib REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(${PROJECT_NAME} ${headers} ${sources})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    httplib::httplib
    nlohmann_json::nlohmann_json)

set_project_warnings(${PROJECT_NAME})

if(${PROJECT_NAME}_ENABLE_SANITIZERS)
  target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
endif()

# ── Standalone CLI (version banner) ──────────────────────────────────────────
add_executable(${PROJECT_NAME}_cli src/main.cpp)
target_compile_features(${PROJECT_NAME}_cli PRIVATE cxx_std_20)
set_target_properties(${PROJECT_NAME}_cli PROPERTIES CXX_EXTENSIONS OFF OUTPUT_NAME ${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME}_cli PRIVATE ${PROJECT_NAME}::${PROJECT_NAME})

if(${PROJECT_NAME}_BUILD_TESTING)
  enable_testing()
  add_subdirectory(test)
  if(${PROJECT_NAME}_ENABLE_SANITIZERS)
    foreach(_test_target ${PROJECT_NAME}_tests ${PROJECT_NAME}_integration_tests)
      target_compile_options(${_test_target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${_test_target} PRIVATE -fsanitize=address,undefined)
    endforeach()
  endif()
endif()

if(${PROJECT_NAME}_ENABLE_DOXYGEN)
  enable_doxygen()
endif()

include(GNUInstallDirs)
install(
  TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}Targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
  TARGETS ${PROJECT_NAME}_cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  EXPORT ${PROJECT_NAME}Targets
  FILE ${PROJECT_NAME}Targets.cmake
  NAMESPACE ${PROJECT_NAME}::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
