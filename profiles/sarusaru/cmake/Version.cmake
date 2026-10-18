# Release identity also invalidates the installed game module on updates.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" _sarusaru_default_version LIMIT_COUNT 1)
set(SARUSARU_RELEASE_VERSION "${_sarusaru_default_version}" CACHE STRING "SaruSaru release version (X.Y.Z)")
if(NOT SARUSARU_RELEASE_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "SARUSARU_RELEASE_VERSION must be X.Y.Z without a v prefix")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/../host/release_version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/release_version.hpp" @ONLY)
# The same version for the Windows executable's VERSIONINFO (resources/sarusaru.rc).
string(REPLACE "." "," SARUSARU_RELEASE_VERSION_COMMAS "${SARUSARU_RELEASE_VERSION}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/../resources/release_version_rc.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/release_version_rc.h" @ONLY)
file(WRITE "${CMAKE_BINARY_DIR}/sarusaru-version.txt" "${SARUSARU_RELEASE_VERSION}\n")
