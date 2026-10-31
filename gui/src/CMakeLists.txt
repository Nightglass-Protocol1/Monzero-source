add_subdirectory(QR-Code-scanner)
add_subdirectory(openpgp)
add_subdirectory(zxcvbn-c)

qt5_add_resources(RESOURCES ../qml.qrc)

# Compile source files (.h/.cpp)
file(GLOB SOURCE_FILES
	"*.h"
	"*.cpp"
    "main/*.h"
    "main/*.cpp"
    "libwalletqt/WalletManager.cpp"
    "libwalletqt/WalletListenerImpl.cpp"
    "libwalletqt/Wallet.cpp"
    "libwalletqt/PassphraseHelper.cpp"
    "libwalletqt/PendingTransaction.cpp"
    "libwalletqt/TransactionHistory.cpp"
    "libwalletqt/TransactionInfo.cpp"
    "libwalletqt/QRCodeImageProvider.cpp"
    "libwalletqt/AddressBook.cpp"
    "libwalletqt/Subaddress.cpp"
    "libwalletqt/SubaddressAccount.cpp"
    "libwalletqt/UnsignedTransaction.cpp"
    "libwalletqt/WalletManager.h"
    "libwalletqt/Wallet.h"
    "libwalletqt/PassphraseHelper.h"
    "libwalletqt/PendingTransaction.h"
    "libwalletqt/TransactionHistory.h"
    "libwalletqt/TransactionInfo.h"
    "libwalletqt/QRCodeImageProvider.h"
    "libwalletqt/Transfer.h"
    "libwalletqt/AddressBook.h"
    "libwalletqt/Subaddress.h"
    "libwalletqt/SubaddressAccount.h"
    "libwalletqt/UnsignedTransaction.h"
    "daemon/*.h"
    "daemon/*.cpp"
    "p2pool/*.h"
    "p2pool/*.cpp"
    "model/*.h"
    "model/*.cpp"
    "qt/*.h"
    "qt/*.cpp"
)
if(WITH_DEX_PROTOTYPE)
    find_package(Qt5 REQUIRED COMPONENTS Concurrent)
    file(GLOB DEX_SOURCE_FILES "dex/*.h" "dex/*.cpp")
    list(APPEND SOURCE_FILES ${DEX_SOURCE_FILES})
    # Operator control public key (64 lowercase hex). Empty: only regtest may start trades.
    set(MONZERO_DEX_CONTROL_PUBKEY "" CACHE STRING "Ed25519 public key for signed DEX control documents")
    # CMake regular expressions have no {n} repetition, so check the length separately.
    string(LENGTH "${MONZERO_DEX_CONTROL_PUBKEY}" dex_control_pubkey_length)
    if(NOT MONZERO_DEX_CONTROL_PUBKEY STREQUAL ""
            AND (NOT dex_control_pubkey_length EQUAL 64 OR NOT MONZERO_DEX_CONTROL_PUBKEY MATCHES "^[0-9a-f]+$"))
        message(FATAL_ERROR "MONZERO_DEX_CONTROL_PUBKEY must be 64 lowercase hex characters")
    endif()
endif()
if(QML_TESTS)
    list(APPEND SOURCE_FILES "${CMAKE_SOURCE_DIR}/tests/qml/QmlTestHarness.cpp")
endif()
if(APPLE)
    list(APPEND SOURCE_FILES "qt/macoshelper.mm")
endif()

set(EXECUTABLE_FLAG)
if(MINGW)
    set(EXECUTABLE_FLAG WIN32)

    set(ICON ${PROJECT_SOURCE_DIR}/images/appicon.ico)
    set(ICON_RC ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
    set(ICON_RES ${CMAKE_CURRENT_BINARY_DIR}/icon.o)
    file(WRITE ${ICON_RC} "IDI_ICON1 ICON DISCARDABLE \"${ICON}\"")
    find_program(Qt5_WINDRES_EXECUTABLE NAMES windres x86_64-w64-mingw32-windres REQUIRED CMAKE_FIND_ROOT_PATH_BOTH)
    add_custom_command(OUTPUT ${ICON_RES} COMMAND ${Qt5_WINDRES_EXECUTABLE} ${ICON_RC} ${ICON_RES} MAIN_DEPENDENCY ${ICON_RC})
    list(APPEND RESOURCES ${ICON_RES})
endif()

if(APPLE)
    set(ICON ${PROJECT_SOURCE_DIR}/images/appicon.icns)
    set_source_files_properties(${ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND RESOURCES ${ICON})
endif()

set(monero_wallet_gui_sources
    ${SOURCE_FILES}
    ${RESOURCES}
)

if(NOT ANDROID)
    add_executable(monzero-wallet-gui ${EXECUTABLE_FLAG} ${monero_wallet_gui_sources})
else()
    add_library(monzero-wallet-gui SHARED ${monero_wallet_gui_sources})
    set_target_properties(monzero-wallet-gui PROPERTIES COMPILE_DEFINITIONS "ANDROID")
endif()

set_target_properties(monzero-wallet-gui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/share/Info.plist"
)

# OpenGL
target_include_directories(monzero-wallet-gui PUBLIC ${OPENGL_INCLUDE_DIR})
message(STATUS "OpenGL: include dir at ${OPENGL_INCLUDE_DIR}")
message(STATUS "OpenGL: libraries at ${OPENGL_LIBRARIES}")

target_include_directories(monzero-wallet-gui PUBLIC ${Qt5Gui_PRIVATE_INCLUDE_DIRS})

target_include_directories(monzero-wallet-gui PUBLIC
	${CMAKE_SOURCE_DIR}/monero/include
    ${CMAKE_SOURCE_DIR}/monero/src
    ${CMAKE_SOURCE_DIR}/monero/external/easylogging++
    ${CMAKE_SOURCE_DIR}/monero/contrib/epee/include
    ${CMAKE_SOURCE_DIR}/monero/external/qrcodegen
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/daemon
    ${CMAKE_CURRENT_SOURCE_DIR}/p2pool
	${CMAKE_CURRENT_SOURCE_DIR}/libwalletqt
	${CMAKE_CURRENT_SOURCE_DIR}/model
	${CMAKE_CURRENT_SOURCE_DIR}/QR-Code-scanner
	${CMAKE_CURRENT_SOURCE_DIR}/zxcvbn-c
)

target_compile_definitions(monzero-wallet-gui
	PUBLIC
	${Qt5Widgets_DEFINITIONS}
    ${Qt5Qml_DEFINITIONS}
)

if(WITH_DEX_PROTOTYPE)
    target_compile_definitions(monzero-wallet-gui PRIVATE MONZERO_DEX_CONTROL_PUBKEY="${MONZERO_DEX_CONTROL_PUBKEY}")
endif()

if(QML_TESTS)
    target_include_directories(monzero-wallet-gui PRIVATE "${CMAKE_SOURCE_DIR}/tests/qml")
    target_compile_definitions(monzero-wallet-gui PRIVATE
        QML_TESTS
        QML_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}/tests/qml"
    )
endif()

if(APPLE)
    if(NOT ICU_ROOT)
        execute_process(COMMAND brew --prefix icu4c OUTPUT_VARIABLE ICU_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    find_package(ICU REQUIRED COMPONENTS data i18n uc)
    target_link_directories(monzero-wallet-gui PRIVATE ${ICU_ROOT}/lib)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${Qt5Widgets_EXECUTABLE_COMPILE_FLAGS}")

target_link_libraries(monzero-wallet-gui
    epee
    common
    net
    wallet_api
    qrcodegen
    easylogging
    ${QT5_LIBRARIES}
    ${EXTRA_LIBRARIES}
    openpgp
    qrdecoder
    translations
    zxcvbn
)

if(WITH_SCANNER)
    target_link_libraries(monzero-wallet-gui qrscanner)
    if(LINUX AND NOT ANDROID)
        target_link_libraries(monzero-wallet-gui
            jpeg
            v4l2
            v4lconvert
            rt
        )
    endif()
endif()

add_custom_command(TARGET monzero-wallet-gui POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:daemon> $<TARGET_FILE_DIR:monzero-wallet-gui>)

include(Deploy)

include("${CMAKE_SOURCE_DIR}/cmake/LightWalletHelper.cmake")
target_include_directories(monzero-wallet-gui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

install(TARGETS monzero-wallet-gui
    DESTINATION bin
)

# DEX remains an explicitly enabled private prototype.
if(WITH_DEX_PROTOTYPE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DEX_SODIUM REQUIRED IMPORTED_TARGET libsodium)
    # DexEngine uses header-only multiprecision amounts. Respect the Boost
    # location selected by the core, including non-system toolchains.
    get_directory_property(DEX_BOOST_INCLUDE_DIRS DIRECTORY "${CMAKE_SOURCE_DIR}/monero" DEFINITION Boost_INCLUDE_DIRS)
    target_include_directories(monzero-wallet-gui SYSTEM PRIVATE ${DEX_BOOST_INCLUDE_DIRS})
    target_link_libraries(monzero-wallet-gui PkgConfig::DEX_SODIUM Qt5::Concurrent)
    option(DEX_BUILD_TESTS "Build private DEX regression tests" ON)
    if(DEX_BUILD_TESTS)
        find_package(Qt5 REQUIRED COMPONENTS Test)
        add_executable(dex_recovery_receiver_tests dex/tests/test_native_recovery_receiver.cpp dex/NativeRecoveryReceiver.cpp)
        add_executable(dex_backend_config_tests dex/tests/test_private_backend_config.cpp dex/PrivateBackendConfig.cpp)
        target_link_libraries(dex_backend_config_tests Qt5::Core)
        target_link_libraries(dex_recovery_receiver_tests Qt5::Core)
        add_executable(dex_tests dex/tests/test_dex.cpp dex/DexEngine.cpp dex/DexEngine.h dex/DexUtils.cpp)
        target_compile_definitions(dex_tests PRIVATE MONZERO_DEX_TESTING)
        target_include_directories(dex_tests SYSTEM PRIVATE ${DEX_BOOST_INCLUDE_DIRS})
        target_sources(dex_tests PRIVATE dex/NativeFeePreviewReceiver.cpp dex/LitecoinLightJob.cpp dex/LitecoinLightJob.h)
        target_sources(dex_tests PRIVATE dex/DexLitecoinLightFunding.cpp dex/LitecoinLightFundingController.cpp dex/LitecoinLightFundingController.h dex/LitecoinLightFundingJob.cpp dex/LitecoinLightFundingJob.h)
        target_sources(dex_tests PRIVATE dex/DexBchLightFunding.cpp dex/BchLightFundingController.cpp dex/BchLightFundingController.h dex/BchLightFundingJob.cpp dex/BchLightFundingJob.h)
        target_sources(dex_tests PRIVATE dex/BchFundingReceiptJob.cpp dex/BchFundingReceiptJob.h)
        target_sources(dex_tests PRIVATE dex/BchNativePreparationJob.cpp dex/BchNativePreparationJob.h)
        target_sources(dex_tests PRIVATE dex/BchNativeRelayJob.cpp dex/BchNativeRelayJob.h)
        target_sources(dex_tests PRIVATE dex/BchNativeProofJob.cpp dex/BchNativeProofJob.h)
        target_sources(dex_tests PRIVATE dex/BchNativeObservationJob.cpp dex/BchNativeObservationJob.h)
        target_sources(dex_tests PRIVATE dex/BchSuccessReleaseReviewJob.cpp dex/BchSuccessReleaseReviewJob.h)
        target_sources(dex_tests PRIVATE dex/BchSuccessPreparationJob.cpp dex/BchSuccessPreparationJob.h)
        target_sources(dex_tests PRIVATE dex/BchPublicationRepairJob.cpp dex/BchPublicationRepairJob.h)
        target_sources(dex_tests PRIVATE dex/BchRefundWalletJob.cpp dex/BchRefundWalletJob.h dex/BchPunishmentJob.cpp dex/BchPunishmentJob.h dex/BchRefundReleaseJob.cpp dex/BchRefundReleaseJob.h dex/BchCancellationJob.cpp dex/BchCancellationJob.h dex/BchRecoveryRetentionJob.cpp dex/BchRecoveryRetentionJob.h dex/BchRefundProtectionJob.cpp dex/BchRefundProtectionJob.h dex/BchPreparationJob.cpp dex/BchPreparationJob.h dex/BchAdmissionJob.cpp dex/BchAdmissionJob.h dex/BchFundingJob.cpp dex/BchFundingJob.h)
        target_sources(dex_tests PRIVATE dex/DexRiskControl.cpp dex/DexRiskControl.h dex/DexTradeFolders.cpp dex/DexTradeFolders.h)
        add_executable(dex_bch_light_funding_job_tests dex/tests/test_bch_light_funding_job.cpp dex/BchLightFundingJob.cpp dex/BchLightFundingJob.h dex/LitecoinLightJob.cpp dex/LitecoinLightJob.h)
        target_link_libraries(dex_bch_light_funding_job_tests Qt5::Core)
        add_executable(dex_bch_light_funding_controller_probe dex/tests/bch_light_funding_controller_probe.cpp dex/BchLightFundingController.cpp dex/BchLightFundingController.h dex/BchLightFundingJob.cpp dex/BchLightFundingJob.h dex/LitecoinLightJob.cpp dex/LitecoinLightJob.h)
        target_link_libraries(dex_bch_light_funding_controller_probe Qt5::Core)
        add_executable(dex_bch_light_funding_probe dex/tests/bch_light_funding_probe.cpp dex/BchLightFundingJob.cpp dex/BchLightFundingJob.h dex/LitecoinLightJob.cpp dex/LitecoinLightJob.h)
        target_link_libraries(dex_bch_light_funding_probe Qt5::Core)
        add_executable(dex_bch_light_funding_context_tests dex/tests/test_bch_light_funding_context.cpp)
        target_link_libraries(dex_bch_light_funding_context_tests Qt5::Core)
        add_executable(dex_light_recovery_protocol_tests dex/tests/test_light_wallet_recovery_protocol.cpp)
        target_link_libraries(dex_light_recovery_protocol_tests Qt5::Core)
        add_executable(dex_litecoin_light_probe dex/tests/litecoin_light_probe.cpp dex/LitecoinLightJob.cpp dex/LitecoinLightJob.h)
        target_link_libraries(dex_litecoin_light_probe Qt5::Core)
        add_executable(dex_litecoin_light_funding_probe dex/tests/litecoin_light_funding_probe.cpp dex/LitecoinLightFundingJob.cpp dex/LitecoinLightFundingJob.h dex/LitecoinLightJob.cpp dex/LitecoinLightJob.h)
        target_link_libraries(dex_litecoin_light_funding_probe Qt5::Core)
        add_executable(dex_litecoin_light_funding_controller_probe dex/tests/litecoin_light_funding_controller_probe.cpp dex/LitecoinLightFundingController.cpp dex/LitecoinLightFundingController.h dex/LitecoinLightFundingJob.cpp dex/LitecoinLightFundingJob.h dex/LitecoinLightJob.cpp dex/LitecoinLightJob.h)
        target_link_libraries(dex_litecoin_light_funding_controller_probe Qt5::Core)
        add_executable(dex_bch_funding_receipt_probe dex/tests/bch_funding_receipt_probe.cpp dex/BchFundingReceiptJob.cpp dex/BchFundingReceiptJob.h)
        target_link_libraries(dex_bch_funding_receipt_probe Qt5::Core)
        add_executable(dex_bch_funding_receipt_tests dex/tests/test_bch_funding_receipt.cpp dex/BchFundingReceiptJob.cpp dex/BchFundingReceiptJob.h)
        target_link_libraries(dex_bch_funding_receipt_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_native_preparation_probe dex/tests/bch_native_preparation_probe.cpp dex/BchNativePreparationJob.cpp dex/BchNativePreparationJob.h)
        target_link_libraries(dex_bch_native_preparation_probe Qt5::Core)
        add_executable(dex_bch_native_preparation_tests dex/tests/test_bch_native_preparation.cpp dex/BchNativePreparationJob.cpp dex/BchNativePreparationJob.h)
        target_link_libraries(dex_bch_native_preparation_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_native_preparation_protocol_tests dex/tests/test_bch_native_preparation_protocol.cpp)
        target_link_libraries(dex_bch_native_preparation_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_success_release_review_tests dex/tests/test_bch_success_release_review.cpp dex/BchSuccessReleaseReviewJob.cpp dex/BchSuccessReleaseReviewJob.h)
        target_link_libraries(dex_bch_success_release_review_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_success_release_review_probe dex/tests/bch_success_release_review_probe.cpp dex/BchSuccessReleaseReviewJob.cpp dex/BchSuccessReleaseReviewJob.h)
        target_link_libraries(dex_bch_success_release_review_probe Qt5::Core)
        add_executable(dex_bch_success_signing_review_probe dex/tests/bch_success_signing_review_probe.cpp dex/BchSuccessReleaseReviewJob.cpp dex/BchSuccessReleaseReviewJob.h)
        target_link_libraries(dex_bch_success_signing_review_probe Qt5::Core)
        find_package(Threads REQUIRED)
        add_executable(dex_bch_private_success_process_tests dex/tests/test_bch_private_success_process.cpp dex/BchPrivateSuccessProcess.cpp)
        target_link_libraries(dex_bch_private_success_process_tests Qt5::Core PkgConfig::DEX_SODIUM Threads::Threads)
        add_executable(dex_bch_private_success_request_probe dex/tests/bch_private_success_request_probe.cpp dex/OfferIdentityStore.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_bch_private_success_request_probe Qt5::Core PkgConfig::DEX_SODIUM)
        add_executable(dex_bch_private_success_probe dex/tests/bch_private_success_probe.cpp dex/OfferIdentityStore.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_bch_private_success_probe Qt5::Core PkgConfig::DEX_SODIUM)
        add_executable(dex_bch_private_success_protocol_tests dex/tests/test_bch_private_success_protocol.cpp)
        target_link_libraries(dex_bch_private_success_protocol_tests Qt5::Core PkgConfig::DEX_SODIUM)
        add_executable(dex_bch_success_identity_probe dex/tests/bch_success_identity_probe.cpp dex/OfferIdentityStore.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_bch_success_identity_probe Qt5::Core PkgConfig::DEX_SODIUM)
        add_executable(dex_bch_success_status_probe dex/tests/bch_success_status_probe.cpp dex/BchSuccessPreparationJob.cpp dex/BchSuccessPreparationJob.h)
        target_link_libraries(dex_bch_success_status_probe Qt5::Core)
        add_executable(dex_bch_success_preparation_probe dex/tests/bch_success_preparation_probe.cpp dex/BchSuccessPreparationJob.cpp dex/BchSuccessPreparationJob.h)
        target_link_libraries(dex_bch_success_preparation_probe Qt5::Core)
        add_executable(dex_bch_success_preparation_tests dex/tests/test_bch_success_preparation.cpp dex/BchSuccessPreparationJob.cpp dex/BchSuccessPreparationJob.h)
        target_link_libraries(dex_bch_success_preparation_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_success_release_protocol_tests dex/tests/test_bch_success_release_protocol.cpp)
        target_link_libraries(dex_bch_success_release_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_publication_repair_protocol_tests dex/tests/test_bch_publication_repair_protocol.cpp)
        target_link_libraries(dex_bch_publication_repair_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_publication_repair_job_tests dex/tests/test_bch_publication_repair_job.cpp dex/BchPublicationRepairJob.cpp dex/BchPublicationRepairJob.h)
        target_link_libraries(dex_bch_publication_repair_job_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_publication_repair_probe dex/tests/bch_publication_repair_probe.cpp dex/BchPublicationRepairJob.cpp dex/BchPublicationRepairJob.h)
        target_link_libraries(dex_bch_publication_repair_probe Qt5::Core)
        add_executable(dex_bch_native_proof_context_probe dex/tests/bch_native_proof_context_probe.cpp dex/BchNativeProofJob.cpp dex/BchNativeProofJob.h)
        target_link_libraries(dex_bch_native_proof_context_probe Qt5::Core)
        add_executable(dex_bch_native_proof_probe dex/tests/bch_native_proof_probe.cpp dex/BchNativeProofJob.cpp dex/BchNativeProofJob.h)
        target_link_libraries(dex_bch_native_proof_probe Qt5::Core)
        add_executable(dex_bch_native_proof_diagnostic_probe dex/tests/bch_native_proof_probe.cpp dex/BchNativeProofJob.cpp dex/BchNativeProofJob.h)
        target_link_libraries(dex_bch_native_proof_diagnostic_probe Qt5::Core)
        add_executable(dex_bch_native_proof_tests dex/tests/test_bch_native_proof.cpp dex/BchNativeProofJob.cpp dex/BchNativeProofJob.h)
        target_link_libraries(dex_bch_native_proof_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_native_observation_probe dex/tests/bch_native_observation_probe.cpp dex/BchNativeObservationJob.cpp dex/BchNativeObservationJob.h)
        target_link_libraries(dex_bch_native_observation_probe Qt5::Core)
        # XMZ<->BNB/ETH trade job against the real monzero-dex-evm-helper (see the test's header)
        add_executable(dex_evm_trade_job_test dex/tests/evm_trade_job_test.cpp dex/EvmTradeJob.cpp dex/EvmTradeJob.h)
        target_link_libraries(dex_evm_trade_job_test Qt5::Core)
        add_executable(dex_bch_native_observation_tests dex/tests/test_bch_native_observation.cpp dex/BchNativeObservationJob.cpp dex/BchNativeObservationJob.h)
        target_link_libraries(dex_bch_native_observation_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_native_observation_protocol_tests dex/tests/test_bch_native_observation_protocol.cpp)
        target_link_libraries(dex_bch_native_observation_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_native_proof_protocol_tests dex/tests/test_bch_native_proof_protocol.cpp)
        target_link_libraries(dex_bch_native_proof_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_native_relay_protocol_tests dex/tests/test_bch_native_relay_protocol.cpp)
        target_link_libraries(dex_bch_native_relay_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_native_relay_tests dex/tests/test_bch_native_relay.cpp dex/BchNativeRelayJob.cpp dex/BchNativeRelayJob.h)
        target_link_libraries(dex_bch_native_relay_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_native_relay_probe dex/tests/bch_native_relay_probe.cpp dex/BchNativeRelayJob.cpp dex/BchNativeRelayJob.h)
        target_link_libraries(dex_bch_native_relay_probe Qt5::Core)
        add_executable(dex_bch_funding_receipt_protocol_tests dex/tests/test_bch_funding_receipt_protocol.cpp)
        target_link_libraries(dex_bch_funding_receipt_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_refund_protection_tests dex/tests/test_bch_refund_protection.cpp dex/BchRefundProtectionJob.cpp dex/BchRefundProtectionJob.h)
        target_link_libraries(dex_bch_refund_protection_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_recovery_retention_tests dex/tests/test_bch_recovery_retention.cpp dex/BchRecoveryRetentionJob.cpp dex/BchRecoveryRetentionJob.h)
        target_link_libraries(dex_bch_recovery_retention_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_refund_release_tests dex/tests/test_bch_refund_release.cpp dex/BchRefundReleaseJob.cpp dex/BchRefundReleaseJob.h)
        target_link_libraries(dex_bch_refund_release_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_refund_release_probe dex/tests/bch_refund_release_probe.cpp dex/BchRefundReleaseJob.cpp dex/BchRefundReleaseJob.h)
        target_link_libraries(dex_bch_refund_release_probe Qt5::Core)
        add_executable(dex_bch_refund_release_protocol_tests dex/tests/test_bch_refund_release_protocol.cpp)
        target_link_libraries(dex_bch_refund_release_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_punishment_probe dex/tests/bch_punishment_probe.cpp dex/BchPunishmentJob.cpp dex/BchPunishmentJob.h)
        target_link_libraries(dex_bch_punishment_probe Qt5::Core)
        add_executable(dex_bch_punishment_tests dex/tests/test_bch_punishment.cpp dex/BchPunishmentJob.cpp dex/BchPunishmentJob.h)
        target_link_libraries(dex_bch_punishment_tests Qt5::Core Qt5::Test)
        add_dependencies(dex_bch_punishment_tests dex_bch_punishment_probe)
        add_executable(dex_bch_refund_wallet_tests dex/tests/test_bch_refund_wallet.cpp dex/BchRefundWalletJob.cpp dex/BchRefundWalletJob.h dex/SwapWalletRpcProcess.cpp dex/SwapWalletRpcProcess.h)
        target_link_libraries(dex_bch_refund_wallet_tests PkgConfig::DEX_SODIUM Qt5::Core Qt5::Test)
        add_executable(dex_bch_refund_wallet_probe dex/tests/bch_refund_wallet_probe.cpp dex/BchRefundWalletJob.cpp dex/BchRefundWalletJob.h dex/SwapWalletRpcProcess.cpp dex/SwapWalletRpcProcess.h)
        target_link_libraries(dex_bch_refund_wallet_probe PkgConfig::DEX_SODIUM Qt5::Core)
        add_dependencies(dex_bch_refund_wallet_tests dex_bch_refund_wallet_probe)
        add_executable(dex_bch_refund_wallet_protocol_tests dex/tests/test_bch_refund_wallet_protocol.cpp)
        target_link_libraries(dex_bch_refund_wallet_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_punishment_protocol_tests dex/tests/test_bch_punishment_protocol.cpp)
        target_link_libraries(dex_bch_punishment_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_cancellation_protocol_tests dex/tests/test_bch_cancellation_protocol.cpp)
        target_link_libraries(dex_bch_cancellation_protocol_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_cancellation_tests dex/tests/test_bch_cancellation.cpp dex/BchCancellationJob.cpp dex/BchCancellationJob.h)
        target_link_libraries(dex_bch_cancellation_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_cancellation_probe dex/tests/bch_cancellation_probe.cpp dex/BchCancellationJob.cpp dex/BchCancellationJob.h)
        target_link_libraries(dex_bch_cancellation_probe Qt5::Core)
        add_executable(dex_bch_recovery_retention_probe dex/tests/bch_recovery_retention_probe.cpp dex/BchRecoveryRetentionJob.cpp dex/BchRecoveryRetentionJob.h)
        target_link_libraries(dex_bch_recovery_retention_probe Qt5::Core)
        add_executable(dex_bch_refund_protection_probe dex/tests/bch_refund_protection_probe.cpp dex/BchRefundProtectionJob.cpp dex/BchRefundProtectionJob.h)
        target_link_libraries(dex_bch_refund_protection_probe Qt5::Core)
        add_executable(dex_bch_admission_tests dex/tests/test_bch_admission.cpp dex/BchAdmissionJob.cpp dex/BchAdmissionJob.h)
        target_link_libraries(dex_bch_admission_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_admission_probe dex/tests/bch_admission_probe.cpp dex/BchAdmissionJob.cpp dex/BchAdmissionJob.h)
        target_link_libraries(dex_bch_admission_probe Qt5::Core)
        add_executable(dex_bch_preparation_probe dex/tests/bch_preparation_probe.cpp dex/BchPreparationJob.cpp dex/BchPreparationJob.h)
        target_link_libraries(dex_bch_preparation_probe Qt5::Core)
        add_executable(dex_bch_preparation_tests dex/tests/test_bch_preparation.cpp dex/BchPreparationJob.cpp dex/BchPreparationJob.h)
        target_link_libraries(dex_bch_preparation_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_funding_tests dex/tests/test_bch_funding.cpp dex/BchFundingJob.cpp dex/BchFundingJob.h)
        target_link_libraries(dex_bch_funding_tests Qt5::Core Qt5::Test)
        add_executable(dex_bch_funding_probe dex/tests/bch_funding_probe.cpp dex/BchFundingJob.cpp dex/BchFundingJob.h)
        target_link_libraries(dex_bch_funding_probe Qt5::Core)


        target_sources(dex_tests PRIVATE dex/LitecoinJournalJob.cpp dex/LitecoinJournalJob.h
            dex/LitecoinReservationJob.cpp dex/LitecoinReservationJob.h
            dex/LitecoinRedemptionJob.cpp dex/LitecoinRedemptionJob.h
            dex/LitecoinAppConfig.cpp dex/LitecoinAppConfig.h dex/DexLitecoinApp.cpp
            dex/LitecoinNegotiationSession.cpp dex/LitecoinNegotiationSession.h dex/LitecoinNegotiationSettlement.cpp
            dex/LitecoinPeerStartup.cpp dex/LitecoinPeerStartup.h
            dex/PrivateQuoteChannel.cpp dex/PrivateQuoteChannel.h dex/PrivateQuoteServer.cpp dex/PrivateQuoteServer.h)
        target_sources(dex_tests PRIVATE dex/NativeRecoveryReceiver.cpp dex/NativeRecoveryJob.cpp dex/NativeRecoveryJob.h)
        target_sources(dex_tests PRIVATE dex/PrivateBackendConfig.cpp)
        target_sources(dex_tests PRIVATE dex/NativeFeePreviewController.cpp dex/NativeFeePreviewController.h)
        target_sources(dex_tests PRIVATE dex/NativeFeePreviewJob.cpp dex/NativeFeePreviewJob.h)
        target_sources(dex_tests PRIVATE dex/SwapWalletRpcProcess.cpp dex/SwapWalletRpcProcess.h)
        target_sources(dex_tests PRIVATE dex/NativeWalletControlJob.cpp dex/NativeWalletControlJob.h)
        target_sources(dex_tests PRIVATE dex/SwapWalletSession.cpp dex/SwapWalletSession.h)
        target_sources(dex_tests PRIVATE dex/NativeQuoteCoordinator.cpp dex/NativeQuoteCoordinator.h
            dex/NativeDestinationJob.cpp dex/NativeDestinationJob.h dex/NativeTradeQuote.cpp dex/NativeQuoteReplay.cpp)
        target_sources(dex_tests PRIVATE dex/OfferIdentityStore.cpp dex/OfferBookStore.cpp dex/OfferMatcher.cpp dex/SignedOffer.cpp dex/SignedOfferCancellation.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_tests Qt5::Core Qt5::Network Qt5::Concurrent Qt5::Test PkgConfig::DEX_SODIUM)
        add_executable(dex_identity_tests ../tests/offer-identity-test.cpp dex/OfferIdentity.cpp)
        target_include_directories(dex_identity_tests PRIVATE "${CMAKE_SOURCE_DIR}")
        target_link_libraries(dex_identity_tests Qt5::Core PkgConfig::DEX_SODIUM)
        enable_testing()
        add_test(NAME dex_recovery_receiver COMMAND dex_recovery_receiver_tests)
        add_test(NAME dex_backend_config COMMAND dex_backend_config_tests)
        add_test(NAME dex_engine COMMAND dex_tests)
        add_test(NAME dex_bch_admission COMMAND dex_bch_admission_tests)
        add_test(NAME dex_identity COMMAND dex_identity_tests)
        add_executable(dex_identity_store_tests dex/tests/test_offer_identity_store.cpp dex/OfferIdentityStore.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_identity_store_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_identity_store COMMAND dex_identity_store_tests)
        add_executable(dex_settlement_tests dex/tests/test_settlement.cpp dex/SettlementTerms.cpp)
        target_link_libraries(dex_settlement_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_settlement COMMAND dex_settlement_tests)
        add_executable(dex_native_quote_tests dex/tests/test_native_trade_quote.cpp dex/NativeTradeQuote.cpp dex/OfferIdentity.cpp)
        target_sources(dex_native_quote_tests PRIVATE dex/DexUtils.cpp)
        target_sources(dex_native_quote_tests PRIVATE dex/NativeQuoteReplay.cpp)
        target_sources(dex_native_quote_tests PRIVATE dex/NativeDestinationJob.cpp dex/NativeDestinationJob.h)
        target_sources(dex_native_quote_tests PRIVATE dex/NativeQuoteCoordinator.cpp dex/NativeQuoteCoordinator.h)
        target_sources(dex_native_quote_tests PRIVATE dex/NativeQuoteConfig.cpp)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_definitions(dex_native_quote_tests PRIVATE NATIVE_QUOTE_FSYNC_FAULT_TEST)
            target_link_options(dex_native_quote_tests PRIVATE "-Wl,--wrap=fsync")
        endif()
        target_link_libraries(dex_native_quote_tests Qt5::Core PkgConfig::DEX_SODIUM)
        add_test(NAME dex_native_quote COMMAND dex_native_quote_tests)
        add_executable(dex_node_rpc_tests dex/tests/test_node_rpc.cpp dex/NodeRpcValidator.cpp)
        target_link_libraries(dex_node_rpc_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_node_rpc COMMAND dex_node_rpc_tests)
        add_executable(dex_rpc_client_tests dex/tests/test_rpc_client.cpp dex/ReadOnlyRpcClient.cpp dex/NodeRpcValidator.cpp)
        target_link_libraries(dex_rpc_client_tests Qt5::Core Qt5::Network Qt5::Test)
        add_test(NAME dex_rpc_client COMMAND dex_rpc_client_tests)
        add_executable(dex_bitcoin_regtest_integration_tests dex/tests/test_bitcoin_regtest_integration.cpp dex/ReadOnlyRpcClient.cpp dex/NodeRpcValidator.cpp dex/BitcoinFeeEvidence.cpp dex/BitcoinPsbtEvidence.cpp dex/BitcoinPsbtRpcVerifier.cpp dex/BitcoinSettlementTemplate.cpp dex/BitcoinSharedOutputScript.cpp)
        target_link_libraries(dex_bitcoin_regtest_integration_tests Qt5::Core Qt5::Network Qt5::Test ${OPENSSL_CRYPTO_LIBRARY})
        add_test(NAME dex_bitcoin_regtest_integration COMMAND dex_bitcoin_regtest_integration_tests)
        set_tests_properties(dex_bitcoin_regtest_integration PROPERTIES TIMEOUT 90)
        add_executable(dex_monzero_fakechain_integration_tests dex/tests/test_monzero_fakechain_integration.cpp dex/ReadOnlyRpcClient.cpp dex/NodeRpcValidator.cpp)
        target_link_libraries(dex_monzero_fakechain_integration_tests Qt5::Core Qt5::Network Qt5::Test)
        add_test(NAME dex_monzero_fakechain_integration COMMAND dex_monzero_fakechain_integration_tests)
        set_tests_properties(dex_monzero_fakechain_integration PROPERTIES TIMEOUT 120)
        add_executable(dex_rpc_live_probe dex/tests/rpc_live_probe.cpp dex/ReadOnlyRpcClient.cpp dex/NodeRpcValidator.cpp dex/XmzFundingCoordinator.cpp dex/BalanceReservationStore.cpp dex/BitcoinTransactionEvidence.cpp dex/BitcoinPsbtRpcVerifier.cpp dex/BitcoinPsbtEvidence.cpp dex/BitcoinFeeEvidence.cpp dex/BitcoinSettlementTemplate.cpp dex/BitcoinSharedOutputScript.cpp dex/MonzeroTransactionEvidence.cpp dex/ConfirmationTracker.cpp)
        target_link_libraries(dex_rpc_live_probe Qt5::Core Qt5::Network ${OPENSSL_CRYPTO_LIBRARY})
        add_executable(dex_reservation_tests dex/tests/test_reservations.cpp dex/BalanceReservationStore.cpp)
        add_executable(reservation_crash_writer dex/tests/reservation_crash_writer.cpp dex/BalanceReservationStore.cpp)
        target_link_libraries(reservation_crash_writer Qt5::Core)
        add_dependencies(dex_reservation_tests reservation_crash_writer)
        target_link_libraries(dex_reservation_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_reservations COMMAND dex_reservation_tests)
        add_executable(dex_xmz_funding_tests dex/tests/test_xmz_funding.cpp dex/XmzFundingCoordinator.cpp dex/BalanceReservationStore.cpp dex/NodeRpcValidator.cpp)
        target_link_libraries(dex_xmz_funding_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_xmz_funding COMMAND dex_xmz_funding_tests)
        add_executable(dex_quote_replay_tests dex/tests/test_quote_replay.cpp dex/QuoteReplayStore.cpp)
        target_link_libraries(dex_quote_replay_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_quote_replay COMMAND dex_quote_replay_tests)
        add_executable(dex_signed_quote_tests dex/tests/test_signed_quote.cpp dex/SignedQuoteAcceptor.cpp dex/QuoteReplayStore.cpp dex/SettlementTerms.cpp dex/OfferIdentity.cpp)
        add_executable(dex_private_quote_channel_tests dex/tests/test_private_quote_channel.cpp dex/PrivateQuoteChannel.cpp dex/PrivateQuoteChannel.h)
        target_sources(dex_private_quote_channel_tests PRIVATE dex/PrivateQuoteServer.cpp dex/PrivateQuoteServer.h)
        target_sources(dex_private_quote_channel_tests PRIVATE dex/PrivateQuoteSession.cpp dex/PrivateQuoteSession.h dex/SignedQuoteAcceptor.cpp dex/QuoteReplayStore.cpp dex/SettlementTerms.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_private_quote_channel_tests PkgConfig::DEX_SODIUM)
        add_executable(dex_private_quote_service dex/tools/private_quote_service.cpp dex/PrivateQuoteChannel.cpp dex/PrivateQuoteChannel.h dex/PrivateQuoteServer.cpp dex/PrivateQuoteServer.h dex/PrivateQuoteSession.cpp dex/PrivateQuoteSession.h dex/SignedQuoteAcceptor.cpp dex/QuoteReplayStore.cpp dex/SettlementTerms.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_private_quote_service Qt5::Core Qt5::Network PkgConfig::DEX_SODIUM)
        add_dependencies(dex_private_quote_channel_tests dex_private_quote_service)
        foreach(quote_target dex_private_quote_service dex_private_quote_channel_tests)
            target_sources(${quote_target} PRIVATE dex/SettlementJournal.cpp dex/BitcoinRecoveryConfirmationTracker.cpp dex/ConfirmationTracker.cpp)
        endforeach()
        target_link_libraries(dex_private_quote_channel_tests Qt5::Core Qt5::Network Qt5::Test)
        add_test(NAME dex_private_quote_channel COMMAND dex_private_quote_channel_tests)
        target_link_libraries(dex_signed_quote_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_signed_quote COMMAND dex_signed_quote_tests)
        add_executable(dex_signed_offer_tests dex/tests/test_signed_offer.cpp dex/SignedOffer.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_signed_offer_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_signed_offer COMMAND dex_signed_offer_tests)
        add_executable(dex_relay_envelope_tests dex/tests/test_relay_envelope.cpp dex/RelayEnvelope.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_relay_envelope_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_relay_envelope COMMAND dex_relay_envelope_tests)
        add_executable(dex_relay_replay_tests dex/tests/test_relay_replay.cpp dex/RelayReplayStore.cpp dex/RelayEnvelope.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_relay_replay_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_relay_replay COMMAND dex_relay_replay_tests)
        set(DEX_RELAY_SOURCES dex/RelayIngress.cpp dex/RelayReplayStore.cpp dex/RelayEnvelope.cpp dex/OfferBookStore.cpp dex/OfferMatcher.cpp dex/SignedOffer.cpp dex/SignedOfferCancellation.cpp dex/OfferIdentity.cpp dex/RelayWireCodec.cpp dex/RelayPeerConnection.cpp dex/RelayPeerConnection.h dex/RelayPeerServer.cpp dex/RelayPeerServer.h)
        list(APPEND DEX_RELAY_SOURCES dex/RelayReceipt.cpp)
        add_executable(dex_relay_ingress_tests dex/tests/test_relay_ingress.cpp ${DEX_RELAY_SOURCES})
        target_sources(dex_relay_ingress_tests PRIVATE dex/RelayOutboxStore.cpp dex/RelaySender.cpp dex/RelaySender.h)
        add_executable(dex_relay_service dex/tools/relay_service.cpp ${DEX_RELAY_SOURCES})
        target_sources(dex_relay_service PRIVATE dex/OfferIdentityStore.cpp)
        target_link_libraries(dex_relay_service Qt5::Core Qt5::Network PkgConfig::DEX_SODIUM)
        add_dependencies(dex_relay_ingress_tests dex_relay_service)
        add_executable(dex_relay_send dex/tools/relay_send.cpp ${DEX_RELAY_SOURCES} dex/RelayOutboxStore.cpp dex/RelaySender.cpp dex/RelaySender.h)
        target_link_libraries(dex_relay_send Qt5::Core Qt5::Network PkgConfig::DEX_SODIUM)
        add_dependencies(dex_relay_ingress_tests dex_relay_send)
        target_link_libraries(dex_relay_ingress_tests Qt5::Core Qt5::Network Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_relay_ingress COMMAND dex_relay_ingress_tests)
        add_executable(dex_relay_wire_codec_tests dex/tests/test_relay_wire_codec.cpp dex/RelayWireCodec.cpp dex/RelayEnvelope.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_relay_wire_codec_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_relay_wire_codec COMMAND dex_relay_wire_codec_tests)
        add_executable(dex_offer_book_tests dex/tests/test_offer_book.cpp dex/OfferBookStore.cpp dex/OfferMatcher.cpp dex/SignedOffer.cpp dex/SignedOfferCancellation.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_offer_book_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_offer_book COMMAND dex_offer_book_tests)
        add_executable(dex_offer_cancellation_tests dex/tests/test_offer_cancellation.cpp dex/SignedOfferCancellation.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_offer_cancellation_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_offer_cancellation COMMAND dex_offer_cancellation_tests)
        add_executable(dex_offer_matcher_tests dex/tests/test_offer_matcher.cpp dex/OfferMatcher.cpp dex/SignedOffer.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_offer_matcher_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_offer_matcher COMMAND dex_offer_matcher_tests)
        add_executable(dex_offer_fill_reservation_tests dex/tests/test_offer_fill_reservations.cpp dex/OfferFillReservationStore.cpp dex/OfferBookStore.cpp dex/OfferMatcher.cpp dex/SignedOffer.cpp dex/SignedOfferCancellation.cpp dex/SignedQuoteAcceptor.cpp dex/QuoteReplayStore.cpp dex/SettlementTerms.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_offer_fill_reservation_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_offer_fill_reservations COMMAND dex_offer_fill_reservation_tests)
        add_executable(dex_confirmation_tests dex/tests/test_confirmations.cpp dex/ConfirmationTracker.cpp)
        target_link_libraries(dex_confirmation_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_confirmations COMMAND dex_confirmation_tests)
        add_executable(dex_bitcoin_evidence_tests dex/tests/test_bitcoin_evidence.cpp dex/BitcoinTransactionEvidence.cpp)
        target_link_libraries(dex_bitcoin_evidence_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_bitcoin_evidence COMMAND dex_bitcoin_evidence_tests)
        add_executable(dex_monzero_evidence_tests dex/tests/test_monzero_evidence.cpp dex/MonzeroTransactionEvidence.cpp)
        target_link_libraries(dex_monzero_evidence_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_monzero_evidence COMMAND dex_monzero_evidence_tests)
        add_executable(dex_monzero_success_evidence_tests dex/tests/test_monzero_success_evidence.cpp dex/MonzeroSuccessTransactionEvidence.cpp dex/SettlementTerms.cpp dex/NodeRpcValidator.cpp)
        target_link_libraries(dex_monzero_success_evidence_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_monzero_success_evidence COMMAND dex_monzero_success_evidence_tests)
        add_executable(dex_monzero_refund_evidence_tests dex/tests/test_monzero_refund_evidence.cpp dex/MonzeroRefundTransactionEvidence.cpp dex/SettlementTerms.cpp dex/NodeRpcValidator.cpp)
        target_link_libraries(dex_monzero_refund_evidence_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_monzero_refund_evidence COMMAND dex_monzero_refund_evidence_tests)
        add_executable(settlement_journal_crash_writer dex/tests/settlement_journal_crash_writer.cpp dex/SettlementJournal.cpp dex/BitcoinRecoveryConfirmationTracker.cpp dex/SignedQuoteAcceptor.cpp dex/QuoteReplayStore.cpp dex/SettlementTerms.cpp dex/OfferIdentity.cpp)
        target_link_libraries(settlement_journal_crash_writer Qt5::Core PkgConfig::DEX_SODIUM)
        add_executable(dex_settlement_journal_tests dex/tests/test_settlement_journal.cpp dex/SettlementJournal.cpp dex/BitcoinRecoveryConfirmationTracker.cpp dex/CrossCurveProofEvidence.cpp dex/CryptoVerifierClient.cpp dex/CryptoVerifierProtocol.cpp dex/SignedQuoteAcceptor.cpp dex/QuoteReplayStore.cpp dex/SettlementTerms.cpp dex/OfferIdentity.cpp)
        add_dependencies(dex_settlement_journal_tests settlement_journal_crash_writer)
        target_sources(settlement_journal_crash_writer PRIVATE dex/ConfirmationTracker.cpp)
        target_sources(dex_settlement_journal_tests PRIVATE dex/ConfirmationTracker.cpp)
        target_link_libraries(dex_settlement_journal_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_settlement_journal COMMAND dex_settlement_journal_tests)
        add_executable(dex_bitcoin_settlement_template_tests dex/tests/test_bitcoin_settlement_template.cpp dex/BitcoinSettlementTemplate.cpp)
        target_link_libraries(dex_bitcoin_settlement_template_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_bitcoin_settlement_template COMMAND dex_bitcoin_settlement_template_tests)
        add_executable(dex_bitcoin_psbt_evidence_tests dex/tests/test_bitcoin_psbt_evidence.cpp dex/BitcoinPsbtEvidence.cpp dex/BitcoinSettlementTemplate.cpp dex/BitcoinSharedOutputScript.cpp)
        target_link_libraries(dex_bitcoin_psbt_evidence_tests Qt5::Core Qt5::Test ${OPENSSL_CRYPTO_LIBRARY})
        add_test(NAME dex_bitcoin_psbt_evidence COMMAND dex_bitcoin_psbt_evidence_tests)
        add_executable(dex_bitcoin_psbt_rpc_tests dex/tests/test_bitcoin_psbt_rpc.cpp dex/BitcoinLockPsbtRpcVerifier.cpp dex/BitcoinPsbtRpcVerifier.cpp dex/BitcoinFeeEvidence.cpp dex/BitcoinPsbtEvidence.cpp dex/BitcoinSettlementTemplate.cpp dex/BitcoinSharedOutputScript.cpp dex/SettlementTerms.cpp dex/SettlementJournal.cpp dex/SignedQuoteAcceptor.cpp dex/QuoteReplayStore.cpp dex/OfferIdentity.cpp dex/CrossCurveProofEvidence.cpp dex/CryptoVerifierClient.cpp dex/CryptoVerifierProtocol.cpp dex/BitcoinRecoveryPsbtRpcVerifier.cpp dex/BitcoinRecoveryPsbtEvidence.cpp dex/BitcoinSpendPsbtEvidence.cpp dex/BitcoinSegwitV0Sighash.cpp dex/BitcoinSignedRecoveryEvidence.cpp dex/BitcoinRawTransaction.cpp dex/BitcoinEcdsaSignatureVerifier.cpp dex/BitcoinRecoveryChainObserver.cpp dex/BitcoinRecoveryConfirmationTracker.cpp dex/ReadOnlyRpcClient.cpp dex/NodeRpcValidator.cpp)
        target_link_libraries(dex_bitcoin_psbt_rpc_tests Qt5::Core Qt5::Network Qt5::Test ${OPENSSL_CRYPTO_LIBRARY} PkgConfig::DEX_SODIUM)
        target_sources(dex_bitcoin_psbt_rpc_tests PRIVATE dex/ConfirmationTracker.cpp)
        add_test(NAME dex_bitcoin_psbt_rpc COMMAND dex_bitcoin_psbt_rpc_tests)
        add_executable(dex_bitcoin_fee_evidence_tests dex/tests/test_bitcoin_fee_evidence.cpp dex/BitcoinFeeEvidence.cpp)
        target_link_libraries(dex_bitcoin_fee_evidence_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_bitcoin_fee_evidence COMMAND dex_bitcoin_fee_evidence_tests)
        add_executable(dex_bitcoin_spend_psbt_evidence_tests dex/tests/test_bitcoin_spend_psbt_evidence.cpp dex/BitcoinSpendPsbtEvidence.cpp dex/BitcoinSettlementTemplate.cpp dex/BitcoinSharedOutputScript.cpp dex/BitcoinSegwitV0Sighash.cpp)
        target_link_libraries(dex_bitcoin_spend_psbt_evidence_tests Qt5::Core Qt5::Test ${OPENSSL_CRYPTO_LIBRARY})
        add_test(NAME dex_bitcoin_spend_psbt_evidence COMMAND dex_bitcoin_spend_psbt_evidence_tests)
        add_executable(dex_bitcoin_recovery_psbt_evidence_tests dex/tests/test_bitcoin_recovery_psbt_evidence.cpp dex/BitcoinRecoveryPsbtEvidence.cpp dex/BitcoinRecoveryPsbtRpcVerifier.cpp dex/BitcoinRecoveryMaturityVerifier.cpp dex/BitcoinSignedRecoveryEvidence.cpp dex/BitcoinRecoveryMempoolVerifier.cpp dex/BitcoinRecoveryChainObserver.cpp dex/BitcoinRecoveryConfirmationTracker.cpp dex/BitcoinRawTransaction.cpp dex/BitcoinEcdsaSignatureVerifier.cpp dex/BitcoinSpendPsbtEvidence.cpp dex/BitcoinFeeEvidence.cpp dex/BitcoinSettlementTemplate.cpp dex/BitcoinSharedOutputScript.cpp dex/BitcoinSegwitV0Sighash.cpp dex/SettlementTerms.cpp dex/SettlementJournal.cpp dex/MonzeroSuccessTransactionEvidence.cpp dex/MonzeroRefundTransactionEvidence.cpp dex/SignedQuoteAcceptor.cpp dex/QuoteReplayStore.cpp dex/OfferIdentity.cpp dex/ReadOnlyRpcClient.cpp dex/NodeRpcValidator.cpp dex/OfferFillReservationStore.cpp dex/OfferBookStore.cpp dex/OfferMatcher.cpp dex/SignedOffer.cpp dex/SignedOfferCancellation.cpp)
        target_sources(dex_bitcoin_recovery_psbt_evidence_tests PRIVATE dex/OfferFillSettlement.cpp)
        target_sources(dex_bitcoin_recovery_psbt_evidence_tests PRIVATE dex/ConfirmationTracker.cpp)
        target_link_libraries(dex_bitcoin_recovery_psbt_evidence_tests Qt5::Core Qt5::Network Qt5::Test ${OPENSSL_CRYPTO_LIBRARY} PkgConfig::DEX_SODIUM)
        add_test(NAME dex_bitcoin_recovery_psbt_evidence COMMAND dex_bitcoin_recovery_psbt_evidence_tests)
        add_executable(dex_bitcoin_spend_psbt_rpc_tests dex/tests/test_bitcoin_spend_psbt_rpc.cpp dex/BitcoinSpendPsbtRpcVerifier.cpp dex/BitcoinSpendPsbtEvidence.cpp dex/BitcoinFeeEvidence.cpp dex/BitcoinSettlementTemplate.cpp dex/BitcoinSharedOutputScript.cpp dex/BitcoinSegwitV0Sighash.cpp dex/ReadOnlyRpcClient.cpp dex/NodeRpcValidator.cpp)
        target_link_libraries(dex_bitcoin_spend_psbt_rpc_tests Qt5::Core Qt5::Network Qt5::Test ${OPENSSL_CRYPTO_LIBRARY})
        add_test(NAME dex_bitcoin_spend_psbt_rpc COMMAND dex_bitcoin_spend_psbt_rpc_tests)
        add_executable(dex_bitcoin_shared_output_script_tests dex/tests/test_bitcoin_shared_output_script.cpp dex/BitcoinSharedOutputScript.cpp)
        target_link_libraries(dex_bitcoin_shared_output_script_tests Qt5::Core Qt5::Test ${OPENSSL_CRYPTO_LIBRARY})
        add_test(NAME dex_bitcoin_shared_output_script COMMAND dex_bitcoin_shared_output_script_tests)
        add_executable(dex_bitcoin_segwit_v0_sighash_tests dex/tests/test_bitcoin_segwit_v0_sighash.cpp dex/BitcoinSegwitV0Sighash.cpp)
        target_link_libraries(dex_bitcoin_segwit_v0_sighash_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_bitcoin_segwit_v0_sighash COMMAND dex_bitcoin_segwit_v0_sighash_tests)
        add_executable(dex_bitcoin_ecdsa_signature_tests dex/tests/test_bitcoin_ecdsa_signature.cpp dex/BitcoinEcdsaSignatureVerifier.cpp)
        target_link_libraries(dex_bitcoin_ecdsa_signature_tests Qt5::Core Qt5::Test ${OPENSSL_CRYPTO_LIBRARY})
        add_test(NAME dex_bitcoin_ecdsa_signature COMMAND dex_bitcoin_ecdsa_signature_tests)
        add_executable(dex_crypto_verifier_protocol_tests dex/tests/test_crypto_verifier_protocol.cpp dex/CryptoVerifierProtocol.cpp)
        target_link_libraries(dex_crypto_verifier_protocol_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_crypto_verifier_protocol COMMAND dex_crypto_verifier_protocol_tests)
        add_executable(dex_crypto_verifier_client_tests dex/tests/test_crypto_verifier_client.cpp dex/CryptoVerifierClient.cpp dex/CryptoVerifierProtocol.cpp)
        target_link_libraries(dex_crypto_verifier_client_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_crypto_verifier_client COMMAND dex_crypto_verifier_client_tests)
        add_executable(dex_cross_curve_proof_evidence_tests dex/tests/test_cross_curve_proof_evidence.cpp dex/CrossCurveProofEvidence.cpp dex/CryptoVerifierClient.cpp dex/CryptoVerifierProtocol.cpp dex/SettlementTerms.cpp)
        target_sources(dex_cross_curve_proof_evidence_tests PRIVATE dex/SignedQuoteAcceptor.cpp dex/QuoteReplayStore.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_cross_curve_proof_evidence_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_cross_curve_proof_evidence COMMAND dex_cross_curve_proof_evidence_tests)
        add_executable(dex_chain_adapter_tests dex/tests/test_chain_adapter.cpp)
        target_link_libraries(dex_chain_adapter_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_chain_adapter COMMAND dex_chain_adapter_tests)
    endif()
endif()
