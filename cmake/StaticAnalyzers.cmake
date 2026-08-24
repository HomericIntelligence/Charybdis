option(${PROJECT_NAME}_ENABLE_CLANG_TIDY "Enable clang-tidy" ON)
option(${PROJECT_NAME}_ENABLE_CPPCHECK "Enable cppcheck" ON)
option(CHARYBDIS_CLANGTIDY_ALLOW_BROKEN_SYSROOT
       "When ON, downgrade conda sysroot probe failures from FATAL_ERROR to WARNING (see issue #84)"
       OFF)

if(${PROJECT_NAME}_ENABLE_CLANG_TIDY)
  find_program(CLANGTIDY clang-tidy)
  if(CLANGTIDY)
    set(_clangtidy_extra_args --extra-arg=-Wno-unknown-warning-option)

    # Conda/pixi sysroot fix (issue #84). Conda's clang-tidy does not pick up
    # the matching sysroot or resource directory by default, so libc headers
    # like <stddef.h> fail to resolve. CI (apt clang-tidy on ubuntu-24.04) has
    # no CONDA_PREFIX and is unaffected.
    if(DEFINED ENV{CONDA_PREFIX})
      set(_conda_prefix "$ENV{CONDA_PREFIX}")
      set(_conda_sysroot "${_conda_prefix}/x86_64-conda-linux-gnu/sysroot")

      # Verify clang-tidy supports --extra-arg-before; the whole fix depends on it.
      execute_process(
        COMMAND "${CLANGTIDY}" --help
        OUTPUT_VARIABLE _ct_help
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _ct_help_rc)
      string(FIND "${_ct_help}" "--extra-arg-before" _ct_has_extra_arg_before)

      # Resolve the conda C++ compiler that ships in the same env as clang-tidy
      # and ask *it* for the resource directory, so we never pick a stray clang.
      find_program(_conda_cxx
        NAMES x86_64-conda-linux-gnu-clang++ x86_64-conda-linux-gnu-c++ clang++
        HINTS "${_conda_prefix}/bin"
        NO_DEFAULT_PATH)

      set(_conda_clang_resource_dir "")
      set(_conda_cxx_rc 1)
      if(_conda_cxx)
        execute_process(
          COMMAND "${_conda_cxx}" -print-resource-dir
          OUTPUT_VARIABLE _conda_clang_resource_dir
          ERROR_QUIET
          OUTPUT_STRIP_TRAILING_WHITESPACE
          RESULT_VARIABLE _conda_cxx_rc)
      endif()

      set(_conda_ok TRUE)
      set(_conda_fail_reason "")
      if(NOT _ct_help_rc EQUAL 0 OR _ct_has_extra_arg_before EQUAL -1)
        set(_conda_ok FALSE)
        set(_conda_fail_reason
            "clang-tidy at ${CLANGTIDY} does not advertise --extra-arg-before (rc=${_ct_help_rc})")
      elseif(NOT _conda_cxx)
        set(_conda_ok FALSE)
        set(_conda_fail_reason
            "no conda C++ compiler found under ${_conda_prefix}/bin (looked for x86_64-conda-linux-gnu-clang++, x86_64-conda-linux-gnu-c++, clang++)")
      elseif(NOT _conda_cxx_rc EQUAL 0)
        set(_conda_ok FALSE)
        set(_conda_fail_reason
            "${_conda_cxx} -print-resource-dir failed (rc=${_conda_cxx_rc})")
      elseif(NOT IS_DIRECTORY "${_conda_sysroot}")
        set(_conda_ok FALSE)
        set(_conda_fail_reason "conda sysroot not found at ${_conda_sysroot}")
      elseif(NOT IS_DIRECTORY "${_conda_clang_resource_dir}")
        set(_conda_ok FALSE)
        set(_conda_fail_reason
            "clang resource dir from ${_conda_cxx} not a directory: '${_conda_clang_resource_dir}'")
      endif()

      if(_conda_ok)
        list(APPEND _clangtidy_extra_args
          "--extra-arg-before=--sysroot=${_conda_sysroot}"
          "--extra-arg-before=-resource-dir=${_conda_clang_resource_dir}")
        message(STATUS
          "clang-tidy: conda sysroot=${_conda_sysroot} "
          "resource-dir=${_conda_clang_resource_dir} (issue #84 fix active)")
      elseif(CHARYBDIS_CLANGTIDY_ALLOW_BROKEN_SYSROOT)
        message(WARNING
          "clang-tidy conda sysroot probe failed: ${_conda_fail_reason}. "
          "Continuing without sysroot injection; expect '<stddef.h>' errors. "
          "Set -DCHARYBDIS_CLANGTIDY_ALLOW_BROKEN_SYSROOT=OFF to make this fatal.")
      else()
        message(FATAL_ERROR
          "clang-tidy conda sysroot probe failed: ${_conda_fail_reason}. "
          "Either fix the conda env, disable clang-tidy "
          "(-D${PROJECT_NAME}_ENABLE_CLANG_TIDY=OFF), or downgrade to a "
          "warning (-DCHARYBDIS_CLANGTIDY_ALLOW_BROKEN_SYSROOT=ON). See issue #84.")
      endif()
    endif()

    set(CMAKE_CXX_CLANG_TIDY ${CLANGTIDY} ${_clangtidy_extra_args})
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
