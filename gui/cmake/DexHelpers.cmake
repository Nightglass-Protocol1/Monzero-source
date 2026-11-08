# The DEX helpers the app runs for trades, bundled like the light-wallet helper: the builder
# supplies each reviewed binary and its independently known SHA-256, the build refuses a mismatch,
# and the app pins that digest (it never trusts whatever executable sits next to it).
#   MONZERO_EVM_HELPER       monzero-dex-evm-helper (XMZ <-> ETH/BNB trades and public offers)
#   MONZERO_JOURNAL_HELPER   native-ltc-journal (XMZ <-> BTC/LTC trades)
foreach(helper EVM JOURNAL)
    set(MONZERO_${helper}_HELPER "" CACHE FILEPATH "Reviewed DEX ${helper} helper to bundle")
    set(MONZERO_${helper}_HELPER_SHA256 "" CACHE STRING "Expected SHA-256 of the reviewed DEX ${helper} helper")
    set(MONZERO_BUNDLED_${helper}_HELPER_SHA256 "")
endforeach()
set(MONZERO_EVM_HELPER_NAME "monzero-dex-evm-helper")
set(MONZERO_JOURNAL_HELPER_NAME "native-ltc-journal")
foreach(helper EVM JOURNAL)
    if(MONZERO_${helper}_HELPER OR MONZERO_${helper}_HELPER_SHA256)
        if(NOT WITH_DEX_PROTOTYPE)
            message(FATAL_ERROR "Bundled DEX helpers require WITH_DEX_PROTOTYPE")
        endif()
        string(LENGTH "${MONZERO_${helper}_HELPER_SHA256}" digest_length)
        if(NOT IS_ABSOLUTE "${MONZERO_${helper}_HELPER}" OR NOT EXISTS "${MONZERO_${helper}_HELPER}"
            OR IS_DIRECTORY "${MONZERO_${helper}_HELPER}" OR NOT digest_length EQUAL 64
            OR NOT MONZERO_${helper}_HELPER_SHA256 MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR "Supply an absolute ${helper} helper file and its expected lowercase SHA-256")
        endif()
        file(SHA256 "${MONZERO_${helper}_HELPER}" actual_digest)
        if(NOT actual_digest STREQUAL MONZERO_${helper}_HELPER_SHA256)
            message(FATAL_ERROR "Reviewed DEX ${helper} helper digest mismatch")
        endif()
        set(MONZERO_BUNDLED_${helper}_HELPER_SHA256 "${MONZERO_${helper}_HELPER_SHA256}")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/monzero-dex")
        configure_file("${MONZERO_${helper}_HELPER}" "${CMAKE_BINARY_DIR}/bin/monzero-dex/${MONZERO_${helper}_HELPER_NAME}" COPYONLY)
        install(PROGRAMS "${CMAKE_BINARY_DIR}/bin/monzero-dex/${MONZERO_${helper}_HELPER_NAME}"
            DESTINATION bin/monzero-dex COMPONENT DexHelpers
            PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
endforeach()
configure_file("${CMAKE_CURRENT_LIST_DIR}/DexHelpers.h.in" "${CMAKE_CURRENT_BINARY_DIR}/DexHelpers.h" @ONLY)
