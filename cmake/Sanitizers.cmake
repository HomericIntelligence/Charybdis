if(${PROJECT_NAME}_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|.*Clang")
    message(WARNING "Sanitizers are only supported with GCC or Clang.")
    return()
  endif()

  set(sanitizer_value "${${PROJECT_NAME}_SANITIZER}")
  if(sanitizer_value STREQUAL "tsan")
    set(_san_flags "-fsanitize=thread")
  else()
    set(_san_flags "-fsanitize=address,undefined")
  endif()

  add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:${_san_flags}>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>)
  add_link_options(${_san_flags})
endif()
