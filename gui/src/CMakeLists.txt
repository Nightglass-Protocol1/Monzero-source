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

install(TARGETS monzero-wallet-gui
    DESTINATION bin
)

# DEX remains an explicitly enabled private prototype.
if(WITH_DEX_PROTOTYPE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DEX_SODIUM REQUIRED IMPORTED_TARGET libsodium)
    target_link_libraries(monzero-wallet-gui PkgConfig::DEX_SODIUM Qt5::Concurrent)
    option(DEX_BUILD_TESTS "Build private DEX regression tests" ON)
    if(DEX_BUILD_TESTS)
        find_package(Qt5 REQUIRED COMPONENTS Test)
        add_executable(dex_tests dex/tests/test_dex.cpp dex/DexEngine.cpp dex/DexEngine.h dex/DexUtils.cpp)
        target_sources(dex_tests PRIVATE dex/OfferBookStore.cpp dex/OfferMatcher.cpp dex/SignedOffer.cpp dex/SignedOfferCancellation.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_tests Qt5::Core Qt5::Concurrent Qt5::Test PkgConfig::DEX_SODIUM)
        add_executable(dex_identity_tests ../tests/offer-identity-test.cpp dex/OfferIdentity.cpp)
        target_include_directories(dex_identity_tests PRIVATE "${CMAKE_SOURCE_DIR}")
        target_link_libraries(dex_identity_tests Qt5::Core PkgConfig::DEX_SODIUM)
        enable_testing()
        add_test(NAME dex_engine COMMAND dex_tests)
        add_test(NAME dex_identity COMMAND dex_identity_tests)
        add_executable(dex_identity_store_tests dex/tests/test_offer_identity_store.cpp dex/OfferIdentityStore.cpp dex/OfferIdentity.cpp)
        target_link_libraries(dex_identity_store_tests Qt5::Core Qt5::Test PkgConfig::DEX_SODIUM)
        add_test(NAME dex_identity_store COMMAND dex_identity_store_tests)
        add_executable(dex_settlement_tests dex/tests/test_settlement.cpp dex/SettlementTerms.cpp)
        target_link_libraries(dex_settlement_tests Qt5::Core Qt5::Test)
        add_test(NAME dex_settlement COMMAND dex_settlement_tests)
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
