# Both analyzers default OFF and are enforced solely by the dedicated "Static
# Analysis" workflow (.github/workflows/static-analysis.yml), whose jobs opt in
# explicitly with -DCharybdis_ENABLE_<TOOL>=ON.
#
# Defaulting them ON meant every configuration that did not opt out re-ran
# clang-tidy over every translation unit -- the build-test matrix (gcc/clang x
# debug/release) plus the install, asan/tsan, coverage, codeql, integration-tests
# and release builds -- while the dedicated clang-tidy job, gated by the required
# `lint` aggregate, already existed to do exactly that. Same defect class as
# Agamemnon #515/#516, where the duplicate cost pinned every matrix job at its
# 30-minute timeout-minutes cap. See #373.
#
# scripts/test-static-analysis-policy.py fails if either default flips back, or
# if a build configuration other than the Static Analysis workflow opts in.
option(${PROJECT_NAME}_ENABLE_CLANG_TIDY "Run clang-tidy during compilation (opt-in; Static Analysis workflow only)" OFF)
option(${PROJECT_NAME}_ENABLE_CPPCHECK "Run cppcheck during compilation (opt-in; Static Analysis workflow only)" OFF)
option(CHARYBDIS_CLANGTIDY_ALLOW_BROKEN_SYSROOT
       "When ON, downgrade conda sysroot probe failures from FATAL_ERROR to WARNING (see issue #84)"
       OFF)

if(${PROJECT_NAME}_ENABLE_CLANG_TIDY)
  find_program(CLANGTIDY clang-tidy)
  if(CLANGTIDY)

    # The builtin headers (stddef.h, ...) live in the compiler's builtin include
    # dir, not in the libc sysroot. When the compile-toolchain frontend (e.g. a
    # conda/pixi GCC) differs from clang-tidy's LLVM frontend, clang-tidy cannot
    # resolve them on its own, so derive both paths from the live compiler.
    execute_process(
      COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=include
      OUTPUT_VARIABLE GCC_INCLUDE_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _gcc_include_rc)
    execute_process(
      COMMAND ${CMAKE_CXX_COMPILER} -print-sysroot
      OUTPUT_VARIABLE COMPILER_SYSROOT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _sysroot_rc)

    # Defensive check for degenerate compiler output (some compilers print the
    # bare fallback token "include" when the path cannot be resolved).
    if(NOT _gcc_include_rc EQUAL 0 OR NOT GCC_INCLUDE_DIR
       OR GCC_INCLUDE_DIR STREQUAL "include")
      message(WARNING "Could not resolve builtin include dir from "
                      "${CMAKE_CXX_COMPILER}; clang-tidy may fail to find "
                      "compiler-builtin headers such as stddef.h")
    else()
      set(CMAKE_CXX_CLANG_TIDY
        ${CLANGTIDY}
        --extra-arg=-Wno-unknown-warning-option
        --extra-arg=-isystem${GCC_INCLUDE_DIR})
      # Only pass --sysroot when the toolchain actually reports one, so a stock
      # system toolchain is never handed a bare "--sysroot=".
      if(_sysroot_rc EQUAL 0 AND COMPILER_SYSROOT)
        list(APPEND CMAKE_CXX_CLANG_TIDY
          "--extra-arg=--sysroot=${COMPILER_SYSROOT}")
      endif()
    endif()

    if(NOT CMAKE_CXX_CLANG_TIDY)
      set(CMAKE_CXX_CLANG_TIDY ${CLANGTIDY}
        --extra-arg=-Wno-unknown-warning-option)
    endif()
  else()
    message(WARNING "clang-tidy not found")
  endif()
endif()

if(${PROJECT_NAME}_ENABLE_CPPCHECK)
  find_program(CPPCHECK cppcheck)
  if(CPPCHECK)
    # --library=googletest teaches cppcheck the gtest TEST()/TEST_F() macros via the
    # shipped cfg/googletest.cfg (which defines them as function definitions).
    # Without it `<gtest/gtest.h>` is never on cppcheck's include path, `TEST` is an
    # unknown macro, and cppcheck aborts every gtest translation unit at the SECOND
    # TEST() with `error: syntax error [syntaxError]` -- analyzing none of it. That
    # silently skipped 11 of 13 TUs. See #375.
    #
    # --error-exitcode=1 is what makes this a gate. CMake's CMAKE_CXX_CPPCHECK
    # integration never adds it, so without it the build succeeds even on
    # `error:`-severity findings and the required `lint` aggregate passes on a
    # clean-looking job that found nothing.
    #
    # The three --suppress= entries below are the *only* findings suppressed by id;
    # everything else is either fixed or suppressed inline at its site. Each is
    # unavoidable under CMake's per-translation-unit invocation:
    #
    #   missingIncludeSystem  cppcheck is not given any -isystem paths, so every
    #                         `#include <...>` reports this. cppcheck's own message
    #                         says it does not need stdlib headers. Note this is NOT
    #                         `missingInclude` -- the project-header id -- because
    #                         the build does pass -I include and -I build/debug/include,
    #                         so those resolve. The two ids were previously conflated.
    #   unusedFunction        only reliable in whole-program mode; per-TU every
    #                         function used from another TU is a false positive, and
    #                         googletest.cfg defines each TEST body as a function.
    #   unmatchedSuppression  a suppression is "unused" in any TU with no finding of
    #                         that id (e.g. unusedFunction in main.cpp), and reports
    #                         itself as a finding. Without this the gate would fail
    #                         on the suppressions that make it usable. The cost is
    #                         that a *stale* suppression becomes invisible, so
    #                         scripts/test-static-analysis-policy.py pins this exact
    #                         list and fails if it changes.
    set(CMAKE_CXX_CPPCHECK
        ${CPPCHECK}
        --enable=all
        --inline-suppr
        --inconclusive
        --library=googletest
        --error-exitcode=1
        --suppress=missingIncludeSystem
        --suppress=unusedFunction
        --suppress=unmatchedSuppression)
  else()
    message(WARNING "cppcheck not found")
  endif()
endif()
