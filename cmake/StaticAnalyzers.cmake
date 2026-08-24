option(${PROJECT_NAME}_ENABLE_CLANG_TIDY "Enable clang-tidy" ON)
option(${PROJECT_NAME}_ENABLE_CPPCHECK "Enable cppcheck" ON)
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
    set(CMAKE_CXX_CPPCHECK ${CPPCHECK} --suppress=missingInclude --enable=all
                           --inline-suppr --inconclusive)
  else()
    message(WARNING "cppcheck not found")
  endif()
endif()
