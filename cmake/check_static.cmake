if(NOT DEFINED EXE OR NOT EXISTS "${EXE}")
    message(FATAL_ERROR "check_static: exe not found: ${EXE}")
endif()
find_program(OBJDUMP_EXE objdump)
find_program(LLVM_OBJDUMP_EXE llvm-objdump)
find_program(DUMPBIN_EXE dumpbin)
set(_out "")
if(OBJDUMP_EXE)
    execute_process(COMMAND "${OBJDUMP_EXE}" -p "${EXE}" OUTPUT_VARIABLE _out ERROR_QUIET)
elseif(LLVM_OBJDUMP_EXE)
    execute_process(COMMAND "${LLVM_OBJDUMP_EXE}" -p "${EXE}" OUTPUT_VARIABLE _out ERROR_QUIET)
elseif(DUMPBIN_EXE)
    execute_process(COMMAND "${DUMPBIN_EXE}" /nologo /dependents "${EXE}" OUTPUT_VARIABLE _out ERROR_QUIET)
else()
    message(WARNING "check_static: no objdump/dumpbin found, skipping")
    return()
endif()
set(_bad
    "libcurl-.*\\.dll" "libstdc\\+\\+-6\\.dll" "libgcc_s_.*\\.dll"
    "libwinpthread-1\\.dll" "zlib1\\.dll" "libzstd\\.dll"
    "libbrotli.*\\.dll" "libpsl.*\\.dll" "libssh2.*\\.dll"
    "libssl.*\\.dll" "libcrypto.*\\.dll" "libidn2.*\\.dll"
    "nghttp2.*\\.dll" "nghttp3.*\\.dll" "ngtcp2.*\\.dll")
foreach(pat IN LISTS _bad)
    if(_out MATCHES "${pat}")
        message(FATAL_ERROR "epi.exe depends on non-system DLL matching '${pat}'")
    endif()
endforeach()
message(STATUS "check_static: OK - only system DLLs remain")
