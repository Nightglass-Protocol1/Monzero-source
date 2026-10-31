# The release/development builder supplies a reviewed binary and an independent
# expected digest. Do not derive trust from whichever executable is next to GUI.
set(MONZERO_LIGHT_HELPER "" CACHE FILEPATH "Reviewed native light-wallet helper to bundle")
set(MONZERO_LIGHT_HELPER_SHA256 "" CACHE STRING "Expected SHA-256 of the reviewed light-wallet helper")
set(MONZERO_BUNDLED_LIGHT_HELPER_SHA256 "")
if(MONZERO_LIGHT_HELPER OR MONZERO_LIGHT_HELPER_SHA256)
    if(NOT WITH_DEX_PROTOTYPE OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Bundled light wallets require the Linux DEX prototype build")
    endif()
    string(LENGTH "${MONZERO_LIGHT_HELPER_SHA256}" digest_length)
    if(NOT IS_ABSOLUTE "${MONZERO_LIGHT_HELPER}" OR NOT EXISTS "${MONZERO_LIGHT_HELPER}"
        OR IS_DIRECTORY "${MONZERO_LIGHT_HELPER}" OR NOT digest_length EQUAL 64
        OR NOT MONZERO_LIGHT_HELPER_SHA256 MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "Supply an absolute helper file and its expected lowercase SHA-256")
    endif()
    file(SHA256 "${MONZERO_LIGHT_HELPER}" actual_digest)
    if(NOT actual_digest STREQUAL MONZERO_LIGHT_HELPER_SHA256)
        message(FATAL_ERROR "Reviewed light-wallet helper digest mismatch")
    endif()
    set(MONZERO_BUNDLED_LIGHT_HELPER_SHA256 "${MONZERO_LIGHT_HELPER_SHA256}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/monzero-light")
    configure_file("${MONZERO_LIGHT_HELPER}" "${CMAKE_BINARY_DIR}/bin/monzero-light/native-ltc-light" COPYONLY)
    install(PROGRAMS "${CMAKE_BINARY_DIR}/bin/monzero-light/native-ltc-light"
        DESTINATION bin/monzero-light COMPONENT LightWalletHelper
        PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/LightWalletHelper.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LightWalletHelper.h" @ONLY)
