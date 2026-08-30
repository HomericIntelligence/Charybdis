option(${PROJECT_NAME}_ENABLE_CLANG_TIDY "Enable clang-tidy" ON)
option(${PROJECT_NAME}_ENABLE_CPPCHECK "Enable cppcheck" ON)
option(CHARYBDIS_CLANGTIDY_ALLOW_BROKEN_SYSROOT
       "When ON, downgrade conda sysroot probe failures from FATAL_ERROR to WARNING (see issue #84)"
       OFF)

if(${PROJECT_NAME}_ENABLE_CLANG_TIDY)
  find_program(CLANGTIDY clang-tidy)
  if(CLANGTIDY)
    set(_clangtidy_extra_args --extra-arg=-Wno-unknown-warning-option)

    # Conda/pixi sysroot fix (issue #84). A conda toolchain resolves headers
    # against two locations: the conda cross sysroot (libc headers such as
    # wchar.h) and the GCC builtin include dir (compiler builtins such as
    # stddef.h). clang-tidy inherits neither automatically, so BOTH paths are
    # derived from the live compiler and passed to clang-tidy as a pair:
    #   --extra-arg=-isystem<$CXX -print-file-name=include>  (builtins)
    #   --extra-arg=--sysroot=<$CXX -print-sysroot>         (libc, optional)
    # --sysroot alone re-breaks builtin resolution; -isystem alone does not
    # redirect libc resolution. See Mnemosyne skill
    # 'clang-tidy-conda-sysroot-isystem-pairing'. CI (apt clang-tidy on
    # ubuntu-24.04) has no CONDA_PREFIX and is unaffected.
    if(DEFINED ENV{CONDA_PREFIX})
      set(_conda_prefix "$ENV{CONDA_PREFIX}")

      # Verify clang-tidy supports --extra-arg; the whole fix depends on it.
      execute_process(
        COMMAND "${CLANGTIDY}" --help
        OUTPUT_VARIABLE _ct_help
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _ct_help_rc)
      string(FIND "${_ct_help}" "--extra-arg" _ct_has_extra_arg)

      # Resolve the live compiler of the conda env and ask *it* for both the
      # GCC builtin include dir and the sysroot, so we never pick a stray
      # toolchain. Prefer $CXX from the activated env when it points into it;
      # conda-forge's cxx-compiler ships GCC, hence the g++-flavored names.
      set(_conda_cxx "")
      if(DEFINED ENV{CXX} AND EXISTS "$ENV{CXX}")
        get_filename_component(_cxx_real "$ENV{CXX}" REALPATH)
        get_filename_component(_conda_prefix_real "${_conda_prefix}" REALPATH)
        string(FIND "${_cxx_real}" "${_conda_prefix_real}" _cxx_in_prefix)
        if(NOT _cxx_in_prefix EQUAL -1)
          set(_conda_cxx "$ENV{CXX}")
        endif()
      endif()
      if(NOT _conda_cxx)
        find_program(_conda_cxx_prog
          NAMES x86_64-conda-linux-gnu-c++ x86_64-conda-linux-gnu-g++
                x86_64-conda-linux-gnu-clang++ c++ g++
          HINTS "${_conda_prefix}/bin"
          NO_DEFAULT_PATH)
        if(_conda_cxx_prog)
          set(_conda_cxx "${_conda_cxx_prog}")
        endif()
      endif()

      set(_gcc_include_dir "")
      set(_gcc_include_rc 1)
      set(_reported_sysroot "")
      set(_sysroot_rc 1)
      if(_conda_cxx)
        # GCC builtin include dir (stddef.h, stdarg.h, ...).
        execute_process(
          COMMAND "${_conda_cxx}" -print-file-name=include
          OUTPUT_VARIABLE _gcc_include_dir
          ERROR_QUIET
          OUTPUT_STRIP_TRAILING_WHITESPACE
          RESULT_VARIABLE _gcc_include_rc)
        # Conda cross sysroot (wchar.h, ...); empty on stock/system toolchains.
        execute_process(
          COMMAND "${_conda_cxx}" -print-sysroot
          OUTPUT_VARIABLE _reported_sysroot
          ERROR_QUIET
          OUTPUT_STRIP_TRAILING_WHITESPACE
          RESULT_VARIABLE _sysroot_rc)
      endif()

      set(_conda_ok TRUE)
      set(_conda_fail_reason "")
      if(NOT _ct_help_rc EQUAL 0 OR _ct_has_extra_arg EQUAL -1)
        set(_conda_ok FALSE)
        set(_conda_fail_reason
            "clang-tidy at ${CLANGTIDY} does not advertise --extra-arg (rc=${_ct_help_rc})")
      elseif(NOT _conda_cxx)
        set(_conda_ok FALSE)
        set(_conda_fail_reason
            "no conda C++ compiler found under ${_conda_prefix}/bin (looked for x86_64-conda-linux-gnu-c++, x86_64-conda-linux-gnu-g++, x86_64-conda-linux-gnu-clang++, c++, g++)")
      elseif(NOT _gcc_include_rc EQUAL 0)
        set(_conda_ok FALSE)
        set(_conda_fail_reason
            "${_conda_cxx} -print-file-name=include failed (rc=${_gcc_include_rc})")
      elseif(NOT IS_DIRECTORY "${_gcc_include_dir}")
        set(_conda_ok FALSE)
        set(_conda_fail_reason
            "GCC builtin include dir from ${_conda_cxx} not a directory: '${_gcc_include_dir}'")
      elseif(_sysroot_rc EQUAL 0 AND _reported_sysroot
             AND NOT IS_DIRECTORY "${_reported_sysroot}")
        set(_conda_ok FALSE)
        set(_conda_fail_reason
            "sysroot reported by ${_conda_cxx} not a directory: '${_reported_sysroot}'")
      endif()

      if(_conda_ok)
        list(APPEND _clangtidy_extra_args "--extra-arg=-isystem${_gcc_include_dir}")
        if(_reported_sysroot)
          list(APPEND _clangtidy_extra_args
               "--extra-arg=--sysroot=${_reported_sysroot}")
        endif()
        message(STATUS
          "clang-tidy: conda gcc-include=${_gcc_include_dir} "
          "sysroot=${_reported_sysroot} (issue #84 fix active)")
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
