# ============================================================
# C++ EPI - Easy Package Installer
# Portable static build (Windows/MinGW, Linux, macOS)
# ============================================================
cmake_minimum_required(VERSION 3.24)

project(EPI
    VERSION 0.3.0
    DESCRIPTION "C++ Easy Package Installer"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

option(EPI_STATIC "Static-link everything" ON)
option(EPI_MINIMAL_CURL "Build a minimal static curl via FetchContent" ON)

# ------------------------------------------------------------
# MINGW_PREFIX detection (no hardcoded paths)
# ------------------------------------------------------------
if(NOT DEFINED MINGW_PREFIX OR MINGW_PREFIX STREQUAL "")
    if(DEFINED ENV{MINGW_PREFIX})
        set(MINGW_PREFIX "$ENV{MINGW_PREFIX}")
    elseif(WIN32 AND MINGW)
        # Try to derive from the compiler location (e.g. C:/msys64/mingw64/bin/g++.exe -> C:/msys64/mingw64)
        get_filename_component(_epi_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_epi_maybe_prefix "${_epi_cxx_dir}" DIRECTORY)
        if(EXISTS "${_epi_maybe_prefix}/include" AND EXISTS "${_epi_maybe_prefix}/lib")
            set(MINGW_PREFIX "${_epi_maybe_prefix}")
        endif()
    endif()
endif()

if(WIN32 AND MINGW AND (NOT DEFINED MINGW_PREFIX OR MINGW_PREFIX STREQUAL ""))
    message(FATAL_ERROR
        "EPI: MINGW_PREFIX is not set.\n"
        "Run from MSYS2 MinGW shell (which sets MINGW_PREFIX),\n"
        "or pass -DMINGW_PREFIX=C:/path/to/msys64/mingw64")
endif()

# ------------------------------------------------------------
# Source lists via GLOB
# ------------------------------------------------------------
file(GLOB EPI_SRC       CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB MINIZ_SRC     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/*.c")

if(NOT EPI_SRC)
    message(FATAL_ERROR "EPI: no sources found in src/*.cpp")
endif()
if(NOT MINIZ_SRC)
    message(FATAL_ERROR "EPI: no sources found in third_party/miniz/*.c")
endif()

# ------------------------------------------------------------
# miniz (zip, public domain, no deps)
# ------------------------------------------------------------
add_library(miniz STATIC ${MINIZ_SRC})
target_include_directories(miniz PUBLIC third_party/miniz)
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
if(NOT MSVC)
    target_compile_options(miniz PRIVATE -w)
endif()

# ------------------------------------------------------------
# zlib (gzip for tar.gz)
# ------------------------------------------------------------
find_package(ZLIB REQUIRED)

# ------------------------------------------------------------
# curl (static on MinGW)
# ------------------------------------------------------------
if(MINGW AND EPI_STATIC AND EPI_MINIMAL_CURL)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    set(_epi_openssl_ssl    "${MINGW_PREFIX}/lib/libssl.a")
    set(_epi_openssl_crypto "${MINGW_PREFIX}/lib/libcrypto.a")
    set(_epi_zlib           "${MINGW_PREFIX}/lib/libz.a")
    foreach(_f IN ITEMS "${_epi_openssl_ssl}" "${_epi_openssl_crypto}" "${_epi_zlib}")
        if(NOT EXISTS "${_f}")
            message(FATAL_ERROR "EPI: missing static library: ${_f}")
        endif()
    endforeach()

    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB ON CACHE BOOL "" FORCE)
    set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    set(USE_NGTCP2 OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP3 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_PROXY ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_COOKIES ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_MANUAL ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(OPENSSL_SSL_LIBRARY    "${_epi_openssl_ssl}"    CACHE FILEPATH "" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${_epi_openssl_crypto}" CACHE FILEPATH "" FORCE)
    set(OPENSSL_INCLUDE_DIR    "${MINGW_PREFIX}/include" CACHE PATH     "" FORCE)
    set(ZLIB_LIBRARY           "${_epi_zlib}"           CACHE FILEPATH "" FORCE)
    set(ZLIB_INCLUDE_DIR       "${MINGW_PREFIX}/include" CACHE PATH     "" FORCE)

    FetchContent_Declare(curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_10_1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(curl)
else()
    find_package(CURL REQUIRED)
endif()

# ------------------------------------------------------------
# Target
# ------------------------------------------------------------
add_executable(epi ${EPI_SRC})
target_include_directories(epi PRIVATE src)
target_compile_definitions(epi PRIVATE EPI_VERSION="${PROJECT_VERSION}" CURL_STATICLIB)

if(MINGW AND EPI_STATIC AND EPI_MINIMAL_CURL)
    target_link_libraries(epi PRIVATE
        miniz
        "${MINGW_PREFIX}/lib/libz.a"
        "${MINGW_PREFIX}/lib/liblzma.a"
        "${MINGW_PREFIX}/lib/libbz2.a"
    )
    set(_epi_libcurl "${CMAKE_BINARY_DIR}/_deps/curl-build/lib/libcurl.a")
    if(NOT EXISTS "${_epi_libcurl}")
        message(FATAL_ERROR "EPI: libcurl.a not built: ${_epi_libcurl}")
    endif()
    add_dependencies(epi libcurl_object)
    target_link_libraries(epi PRIVATE
        "${_epi_libcurl}"
        "${_epi_openssl_ssl}"
        "${_epi_openssl_crypto}"
        "${_epi_zlib}"
        ws2_32 bcrypt advapi32 crypt32 secur32 iphlpapi user32 gdi32
    )
    target_include_directories(epi PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/curl-build/include"
        "${CMAKE_BINARY_DIR}/_deps/curl-src/include"
        "${MINGW_PREFIX}/include"
    )
else()
    target_link_libraries(epi PRIVATE miniz CURL::libcurl)
endif()

if(MSVC)
    target_compile_options(epi PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(epi PRIVATE -Wall -Wextra)
endif()

if(EPI_STATIC)
    if(MSVC)
        set_property(TARGET epi PROPERTY MSVC_RUNTIME_LIBRARY
                     "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_link_options(epi PRIVATE /NODEFAULTLIB:MSVCRT)
    elseif(MINGW)
        target_link_options(epi PRIVATE -static -static-libgcc -static-libstdc++)
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            target_link_options(epi PRIVATE -s)
        endif()
    else()
        target_link_options(epi PRIVATE -static-libgcc -static-libstdc++)
    endif()
endif()

if(WIN32 AND EPI_STATIC)
    add_custom_command(TARGET epi POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:epi>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_static.cmake
        COMMENT "Checking epi.exe has no non-system DLL dependencies"
        VERBATIM)
endif()

# ------------------------------------------------------------
# Copy registry.epi to build dir after build
# ------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/registry/registry.epi")
    add_custom_command(TARGET epi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/registry/registry.epi"
            "$<TARGET_FILE_DIR:epi>/registry.epi"
        COMMENT "Copying registry.epi to build directory"
        VERBATIM)
endif()

install(TARGETS epi RUNTIME DESTINATION bin)