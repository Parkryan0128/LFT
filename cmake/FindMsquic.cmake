# Find libmsquic installed via Homebrew (Apple Silicon: /opt/homebrew).
#
# Provides:
#   Msquic::msquic  - imported target
#   MSQUIC_HOME     - install prefix
#
# Usage:
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
#   find_package(Msquic REQUIRED)

find_program(BREW_EXECUTABLE brew)

function(_lft_brew_prefix formula out_var)
    if(BREW_EXECUTABLE)
        execute_process(
            COMMAND ${BREW_EXECUTABLE} --prefix ${formula}
            OUTPUT_VARIABLE _prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_prefix)
            set(${out_var} "${_prefix}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

set(MSQUIC_HOME "")
set(OPENSSL_HOME "")

if(APPLE)
    _lft_brew_prefix(libmsquic MSQUIC_HOME)
    _lft_brew_prefix(openssl@3 OPENSSL_HOME)

    # Apple Silicon Homebrew default when brew is unavailable in CMake's environment.
    if(NOT MSQUIC_HOME AND EXISTS "/opt/homebrew/opt/libmsquic")
        set(MSQUIC_HOME "/opt/homebrew/opt/libmsquic")
    endif()
    if(NOT OPENSSL_HOME AND EXISTS "/opt/homebrew/opt/openssl@3")
        set(OPENSSL_HOME "/opt/homebrew/opt/openssl@3")
    endif()
endif()

if(MSQUIC_HOME)
    list(APPEND CMAKE_PREFIX_PATH "${MSQUIC_HOME}")
endif()
if(OPENSSL_HOME)
    list(APPEND CMAKE_PREFIX_PATH "${OPENSSL_HOME}")
    set(OpenSSL_ROOT "${OPENSSL_HOME}")
endif()

find_path(
    MSQUIC_INCLUDE_DIR
    NAMES msquic.h
    HINTS ${MSQUIC_HOME} ENV MSQUIC_ROOT
    PATH_SUFFIXES include
)

find_library(
    MSQUIC_LIBRARY
    NAMES msquic
    HINTS ${MSQUIC_HOME} ENV MSQUIC_ROOT
    PATH_SUFFIXES lib
)

find_package(OpenSSL REQUIRED)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    Msquic
    REQUIRED_VARS MSQUIC_INCLUDE_DIR MSQUIC_LIBRARY
    FAIL_MESSAGE
        "libmsquic not found. On Apple Silicon Mac: brew install libmsquic"
)

if(Msquic_FOUND AND NOT TARGET Msquic::msquic)
    add_library(Msquic::msquic UNKNOWN IMPORTED)
    set_target_properties(Msquic::msquic PROPERTIES
        IMPORTED_LOCATION "${MSQUIC_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MSQUIC_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto"
    )
endif()

mark_as_advanced(MSQUIC_INCLUDE_DIR MSQUIC_LIBRARY MSQUIC_HOME OPENSSL_HOME)

# Embed Homebrew lib paths so binaries run without DYLD_FALLBACK_LIBRARY_PATH.
function(lft_set_msquic_rpath target)
    if(APPLE AND MSQUIC_HOME AND OPENSSL_HOME)
        set_target_properties(${target} PROPERTIES
            BUILD_RPATH "${MSQUIC_HOME}/lib;${OPENSSL_HOME}/lib"
        )
    endif()
endfunction()
