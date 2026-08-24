if(${PROJECT_NAME}_ENABLE_SANITIZERS AND ${PROJECT_NAME}_ENABLE_TSAN)
  message(FATAL_ERROR "ENABLE_SANITIZERS (ASAN+UBSAN) and ENABLE_TSAN are mutually exclusive.")
endif()

# Minimum compiler versions known to produce correct sanitizer binaries.
# GCC < 9 has incomplete UBSan vptr/object-size coverage; Clang < 6 has known
# linker failures with -fsanitize=address,undefined. See issue #76.
set(_CHARYBDIS_MIN_GCC_FOR_SANITIZERS 9)
set(_CHARYBDIS_MIN_CLANG_FOR_SANITIZERS 6)

if(${PROJECT_NAME}_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|.*Clang")
    message(WARNING "Sanitizers only supported with GCC or Clang.")
    return()
  endif()
  if(NOT CMAKE_CXX_COMPILER_VERSION)
    message(
      WARNING
        "ASan+UBSan: CMAKE_CXX_COMPILER_VERSION could not be detected for "
        "${CMAKE_CXX_COMPILER_ID}; skipping the minimum-version check. Verify "
        "your toolchain ships GCC >= ${_CHARYBDIS_MIN_GCC_FOR_SANITIZERS} or "
        "Clang >= ${_CHARYBDIS_MIN_CLANG_FOR_SANITIZERS}."
    )
  elseif(
    CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${_CHARYBDIS_MIN_GCC_FOR_SANITIZERS}
  )
    message(
      FATAL_ERROR
        "ASan+UBSan requires GCC >= ${_CHARYBDIS_MIN_GCC_FOR_SANITIZERS} for "
        "complete sanitizer coverage; detected GCC "
        "${CMAKE_CXX_COMPILER_VERSION}. Older GCC has incomplete UBSan coverage "
        "and known linker issues with -fsanitize=address,undefined. Upgrade the "
        "compiler or disable the sanitizer preset."
    )
  elseif(
    CMAKE_CXX_COMPILER_ID MATCHES ".*Clang"
    AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${_CHARYBDIS_MIN_CLANG_FOR_SANITIZERS}
  )
    message(
      FATAL_ERROR
        "ASan+UBSan requires Clang >= ${_CHARYBDIS_MIN_CLANG_FOR_SANITIZERS} "
        "for correct sanitizer linking; detected Clang "
        "${CMAKE_CXX_COMPILER_VERSION}. Upgrade the compiler or disable the "
        "sanitizer preset."
    )
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

if(${PROJECT_NAME}_ENABLE_TSAN)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|.*Clang")
    message(WARNING "TSan only supported with GCC or Clang.")
    return()
  endif()
  if(NOT CMAKE_CXX_COMPILER_VERSION)
    message(
      WARNING
        "ThreadSanitizer: CMAKE_CXX_COMPILER_VERSION could not be detected for "
        "${CMAKE_CXX_COMPILER_ID}; skipping the minimum-version check. Verify "
        "your toolchain ships GCC >= ${_CHARYBDIS_MIN_GCC_FOR_SANITIZERS} or "
        "Clang >= ${_CHARYBDIS_MIN_CLANG_FOR_SANITIZERS}."
    )
  elseif(
    CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${_CHARYBDIS_MIN_GCC_FOR_SANITIZERS}
  )
    message(
      FATAL_ERROR
        "ThreadSanitizer requires GCC >= ${_CHARYBDIS_MIN_GCC_FOR_SANITIZERS}; "
        "detected GCC ${CMAKE_CXX_COMPILER_VERSION}. Upgrade the compiler or "
        "disable the TSan preset."
    )
  elseif(
    CMAKE_CXX_COMPILER_ID MATCHES ".*Clang"
    AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${_CHARYBDIS_MIN_CLANG_FOR_SANITIZERS}
  )
    message(
      FATAL_ERROR
        "ThreadSanitizer requires Clang >= ${_CHARYBDIS_MIN_CLANG_FOR_SANITIZERS}; "
        "detected Clang ${CMAKE_CXX_COMPILER_VERSION}. Upgrade the compiler or "
        "disable the TSan preset."
    )
  endif()
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)
endif()
