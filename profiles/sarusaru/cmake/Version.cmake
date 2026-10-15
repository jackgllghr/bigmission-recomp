# Release identity is deliberately separate from PROJECT_VERSION, which is
# part of the generated game module's compatibility hash.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" _sarusaru_default_version LIMIT_COUNT 1)
set(SARUSARU_RELEASE_VERSION "${_sarusaru_default_version}" CACHE STRING "SaruSaru release version (X.Y.Z)")
if(NOT SARUSARU_RELEASE_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "SARUSARU_RELEASE_VERSION must be X.Y.Z without a v prefix")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/../host/release_version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/release_version.hpp" @ONLY)
file(WRITE "${CMAKE_BINARY_DIR}/sarusaru-version.txt" "${SARUSARU_RELEASE_VERSION}\n")
