#!/usr/bin/env bash
# Reconstruct the port in an isolated Git index without changing any worktree.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# Worktrees outside core/ point these at the shared toolchain and a private engine copy.
core_dir=${DEX_CORE_DIR:-$(cd -- "$script_dir/../../.." && pwd)}
engine_dir=${DEX_ENGINE_DIR:-"$core_dir/build/eigenwallet-engine-port"}
expected=0e17c7f7cd8f0657af176c8852aa4c9949586051
[[ $(git -C "$engine_dir" rev-parse HEAD) == "$expected" ]] || {
  echo 'Unexpected engine revision' >&2; exit 1;
}
audit_dir=$(mktemp -d "$core_dir/build/dex-patch-audit.XXXXXXXX")
export GIT_INDEX_FILE="$audit_dir/index"
git -C "$engine_dir" read-tree "$expected"
for patch in upstream-test-harness.patch upstream-xmz-amount.patch \
             upstream-refund-fixture.patch upstream-xmz-address.patch upstream-bitcoin-integration.patch \
             upstream-bitcoin-settlement.patch upstream-xmz-funding.patch upstream-native-funding-rpc.patch \
             upstream-xmz-wallet-restore.patch upstream-xmz-relay.patch \
             upstream-xmz-swap-wallet-spendable.patch upstream-xmz-swap-wallet-spendable-fixture.patch \
             upstream-xmz-spendable-ownership.patch upstream-xmz-relay-outcome.patch \
             upstream-xmz-spend-journal.patch upstream-xmz-prepared-relay.patch \
             upstream-xmz-spend-recovery.patch upstream-xmz-recovery-command.patch \
             upstream-isolated-alice-peer.patch upstream-alice-checkpoint-fixture.patch \
             upstream-native-quote-destinations.patch upstream-native-destinations-command.patch \
             upstream-native-quote-signatures.patch upstream-native-trade-quote.patch \
             upstream-native-quote-policy.patch upstream-native-quote-negotiation-test.patch \
             upstream-native-negotiation-transcript.patch upstream-native-price-band.patch \
             upstream-native-bob-quote-preflight.patch upstream-native-alice-quote-preflight.patch \
             upstream-native-gui-reservation-decoder.patch upstream-native-qt-reservation-interop.patch \
             upstream-native-reservation-reader.patch upstream-native-qt-file-handoff-test.patch \
             upstream-native-verified-reservation.patch upstream-native-qt-negotiation-test.patch \
             upstream-isolated-peer-principals.patch upstream-funded-quote-preflight.patch \
             upstream-funded-authenticated-negotiation.patch upstream-fixture-checkpoint-association.patch \
             upstream-historical-quote-recovery.patch upstream-pending-recorded-quote-check.patch \
             upstream-quoted-recovery-spend.patch upstream-native-price-freshness.patch \
             upstream-signed-quote-price-band.patch upstream-native-execution-claim.patch \
             upstream-funded-execution-claim.patch upstream-recovery-lock-input-binding.patch \
             upstream-recovery-input-rpc-regression.patch upstream-full-proceeds-preview.patch \
             upstream-recoverable-execution-context.patch upstream-exclusive-recovery-ownership.patch \
             upstream-owned-checkpoint-resume.patch upstream-guarded-pending-checkpoint.patch \
             upstream-prepared-pending-checkpoint.patch upstream-durable-checkpoint-pin.patch \
             upstream-pre-funding-bitcoin-checkpoint.patch upstream-alice-pre-funding-recovery.patch \
             upstream-guarded-alice-checkpoint.patch upstream-bitcoin-submission-intent.patch \
             upstream-bitcoin-submission-observation.patch upstream-bitcoin-lost-ack-fixture.patch \
             upstream-bitcoin-observation-history.patch upstream-exact-net-preview.patch \
             upstream-top-up-preview.patch upstream-top-up-preview-fixture.patch \
             upstream-prepared-exact-net.patch upstream-exact-net-context-tests.patch \
             upstream-exact-net-journal.patch upstream-exact-net-reload.patch \
             upstream-exact-net-reconcile.patch upstream-exact-net-relay.patch \
             upstream-native-topup-journal.patch upstream-topup-redeem-confirmation.patch \
             upstream-topup-dispatch.patch upstream-exact-net-claim.patch \
             upstream-topup-fixture-tests.patch \
             upstream-topup-recovery.patch \
             upstream-litecoin-contract-fixture.patch \
             upstream-transparent-script-spends.patch \
             upstream-litecoin-recovery-paths.patch \
             upstream-fee-dust-property.patch upstream-litecoin-addresses.patch \
             upstream-litecoin-wallet-rpc.patch \
             upstream-litecoin-execution-fixture.patch \
             upstream-dogecoin-primitives.patch \
             upstream-dogecoin-observation.patch \
             upstream-dogecoin-unspent.patch \
             upstream-dogecoin-parent-binding.patch \
             upstream-litecoin-start-helper.patch \
             upstream-litecoin-peer-admission.patch \
             upstream-litecoin-external-signature.patch \
             upstream-litecoin-negotiation-status.patch \
             upstream-litecoin-funding-preparation.patch \
             upstream-litecoin-recovery-signing.patch \
             upstream-litecoin-reservation-helper.patch upstream-litecoin-lock-approval.patch upstream-litecoin-payment-preparation.patch upstream-litecoin-wallet-restoration.patch upstream-litecoin-recovery-approval.patch upstream-litecoin-recovery-confirmation.patch upstream-litecoin-redeem-signing.patch upstream-litecoin-funding-proof-helper.patch upstream-litecoin-redemption-approval.patch upstream-litecoin-redemption-helper.patch upstream-litecoin-indexed-settlement.patch upstream-litecoin-native-recovery-helper.patch upstream-route-approval.patch upstream-xmr-build-jobs.patch upstream-xmr-libiconv-c17.patch upstream-xmr-selection-review.patch upstream-bch-data-signature.patch upstream-bch-covenant.patch upstream-bch-spend.patch upstream-bch-spend-validation.patch upstream-bch-recovery-plan.patch upstream-bch-native-key.patch upstream-bch-quote-signatures.patch upstream-bch-quote-terms.patch upstream-bch-quote-record.patch upstream-bch-authenticated-proof.patch upstream-bch-native-wallet.patch upstream-bch-local-ownership.patch upstream-bch-mutual-agreement.patch upstream-bch-secret-checkpoint.patch upstream-bch-private-storage.patch upstream-bch-process-restore.patch upstream-bch-local-keys.patch upstream-bch-peer-selection.patch upstream-bch-received-view.patch upstream-bch-negotiation-restart.patch upstream-bch-execution-admission.patch upstream-bch-funding-preview.patch upstream-bch-funding-retention.patch upstream-bch-input-claims.patch upstream-bch-rpc-inputs.patch upstream-bch-wallet-locks.patch upstream-bch-signed-funding.patch upstream-bch-wallet-signing.patch upstream-bch-refund-adaptor.patch upstream-bch-private-recovery.patch upstream-bch-funding-observation.patch upstream-bch-signing-reconciliation.patch upstream-bch-funding-relay.patch upstream-bch-funding-message.patch upstream-bch-native-funding-window.patch upstream-bch-native-preparation.patch upstream-bch-native-relay.patch upstream-bch-native-proof.patch upstream-bch-success-release.patch upstream-bch-success-receipt.patch upstream-bch-private-redemption.patch upstream-bch-redemption-review.patch upstream-bch-redemption-relay.patch upstream-bch-confirmed-observation.patch upstream-bch-redemption-confirmation.patch upstream-bch-native-claim.patch upstream-bch-claim-wallet.patch upstream-bch-claim-wallet-tests.patch upstream-bch-native-payout-binding.patch upstream-bch-native-payout-preparation.patch upstream-bch-native-payout-session-test-ports.patch upstream-bch-native-payout-flow-tests.patch upstream-bch-native-payout-review.patch upstream-bch-native-payout-relay.patch upstream-bch-native-payout-confirmation.patch upstream-bch-cancellation-relay.patch upstream-bch-cancellation-observation.patch upstream-bch-recovery-output.patch upstream-bch-refund-review.patch upstream-bch-refund-relay.patch upstream-bch-refund-confirmation.patch upstream-bch-native-refund.patch upstream-bch-refund-wallet.patch upstream-bch-refund-payout.patch upstream-bch-refund-payout-review.patch upstream-bch-refund-dispatch.patch upstream-bch-punish-review.patch upstream-bch-punish-window.patch upstream-bch-punish-dispatch.patch upstream-bch-native-cancel.patch upstream-bch-admission-review.patch upstream-bch-admission-helper.patch upstream-bch-admission-helper-resume.patch upstream-bch-admission-durable-consent.patch upstream-bch-qt-admission-fixture.patch upstream-bch-funding-helper.patch upstream-bch-funding-helper-fixture.patch upstream-bch-qt-funding-fixture.patch upstream-bch-funding-dispatch-helper.patch upstream-bch-qt-relay-fixture.patch upstream-bch-funding-observe-helper.patch upstream-bch-qt-observe-fixture.patch upstream-bch-wallet-preparation.patch upstream-bch-relay-fixture-context.patch upstream-bch-wallet-preparation-retention.patch upstream-bch-preparation-review.patch upstream-bch-preparation-helper.patch upstream-bch-preparation-initial-fixture.patch upstream-bch-qt-preparation-fixture.patch upstream-bch-lock-approval.patch upstream-bch-lock-helper.patch upstream-bch-qt-lock-fixture.patch upstream-bch-signing-status.patch upstream-bch-signing-helper.patch upstream-bch-signing-recovery-helper.patch upstream-bch-qt-signing-fixture.patch upstream-bch-qt-signing-recovery-fixture.patch upstream-bch-refund-protection-review.patch upstream-bch-refund-protection-helper.patch upstream-litecoin-light-proof.patch upstream-litecoin-light-session.patch upstream-litecoin-light-tls.patch upstream-litecoin-light-system-roots.patch upstream-litecoin-light-headers.patch upstream-litecoin-light-sync.patch upstream-litecoin-light-forks.patch upstream-litecoin-light-confirmations.patch upstream-litecoin-light-cache.patch upstream-litecoin-light-candidate.patch upstream-litecoin-light-state.patch upstream-litecoin-light-helper.patch upstream-litecoin-light-account.patch upstream-litecoin-light-allocation.patch upstream-litecoin-light-history.patch upstream-litecoin-light-scan.patch upstream-litecoin-light-transactions.patch upstream-litecoin-light-accounting.patch upstream-litecoin-light-refresh.patch upstream-litecoin-light-funded.patch upstream-litecoin-light-pending.patch upstream-litecoin-light-descriptor.patch upstream-litecoin-light-wallet-helper.patch upstream-litecoin-light-vault.patch upstream-litecoin-light-vault-lifecycle.patch upstream-litecoin-light-wallet-backup.patch upstream-litecoin-light-backup-storage.patch upstream-litecoin-light-receive-helper.patch upstream-litecoin-light-sign-input.patch upstream-litecoin-light-spend-review.patch upstream-litecoin-light-confirmed-review.patch upstream-litecoin-light-unspent.patch upstream-litecoin-light-candidates.patch upstream-litecoin-light-reconcile.patch upstream-litecoin-light-claims.patch upstream-litecoin-light-retained-signing.patch upstream-litecoin-light-reopen.patch upstream-litecoin-light-observed-signing.patch upstream-litecoin-light-selection.patch upstream-litecoin-light-preparation.patch upstream-litecoin-light-scan-prepare.patch upstream-litecoin-light-discovery-allocation.patch upstream-litecoin-light-prepare-helper.patch upstream-litecoin-light-fees.patch upstream-litecoin-light-estimated-selection.patch upstream-litecoin-light-estimate-helper.patch \
             upstream-litecoin-light-reobserved.patch \
             upstream-litecoin-light-reopen-helper.patch \
             upstream-litecoin-light-vault-signing.patch \
             upstream-litecoin-light-sign-helper.patch \
             upstream-litecoin-light-signed-recovery.patch \
             upstream-litecoin-light-broadcast.patch \
             upstream-litecoin-light-broadcast-helper.patch \
             upstream-litecoin-light-payment-reconcile.patch \
             upstream-litecoin-light-backup-helper.patch \
             upstream-litecoin-light-restore-helper.patch \
             upstream-litecoin-light-setup.patch \
             upstream-litecoin-light-create-helper.patch \
             upstream-bch-recovery-retention.patch \
             upstream-bch-cancel-offline-review.patch \
             upstream-bch-cancel-helper.patch \
             upstream-bch-cancel-helper-relay-tests.patch \
             upstream-bch-cancel-qt-fixture.patch \
             upstream-bch-refund-release-helper.patch \
             upstream-bch-node-funding-executor-test.patch \
             upstream-bch-node-handoff-test.patch \
             upstream-bch-paired-funding-test.patch \
             upstream-bch-funding-receipt-helper.patch \
             upstream-bch-funding-receipt-qt-fixture.patch \
             upstream-bch-native-preparation-helper.patch \
             upstream-bch-native-preparation-qt-fixture.patch \
             upstream-bch-native-relay-helper.patch \
             upstream-bch-native-relay-qt-fixture.patch \
             upstream-bch-native-proof-review.patch \
             upstream-bch-native-proof-helper-context-cache.patch \
             upstream-bch-observation-success-review.patch \
             upstream-bch-success-preparation-helper.patch \
             upstream-bch-success-status-helper.patch \
             upstream-bch-success-consent-helper.patch \
             upstream-bch-success-signing-review-helper.patch \
             upstream-bch-success-signing-review-qt-helper.patch \
             upstream-light-network-core.patch \
             upstream-light-wallet-recovery-reveal.patch \
             upstream-light-wallet-key-restore.patch \
             upstream-light-wallet-mnemonic-core.patch \
             upstream-light-wallet-mnemonic-signing.patch \
             upstream-light-wallet-mnemonic-review.patch \
             upstream-light-wallet-mnemonic-backup.patch \
             upstream-light-wallet-mnemonic-helper.patch \
             upstream-light-wallet-mnemonic-setup.patch \
             upstream-light-wallet-mnemonic-reveal.patch \
             upstream-light-wallet-mnemonic-import.patch \
             upstream-light-wallet-import-setup.patch \
             upstream-light-wallet-import-helper.patch \
             upstream-light-wallet-recovery-coverage.patch \
             upstream-light-wallet-import-discovery.patch \
             upstream-light-wallet-recovery-backup.patch \
             upstream-light-wallet-recovery-retention.patch \
             upstream-light-wallet-recovery-coordinator.patch \
             upstream-light-wallet-recovery-helper.patch \
             upstream-light-wallet-import-activation.patch \
             upstream-light-wallet-backup-discovery.patch \
             upstream-light-wallet-backup-configuration.patch \
             upstream-light-wallet-dex-funding-proposal.patch \
             upstream-light-wallet-dex-funding-signing.patch \
             upstream-light-wallet-dex-funding-relay.patch \
             upstream-light-wallet-dex-helper.patch \
             upstream-light-wallet-dex-terms.patch \
             upstream-light-wallet-refund-ownership.patch \
             upstream-bch-light-funding-terms.patch \
             upstream-bch-light-p2sh32.patch \
             upstream-bch-light-signed-export.patch \
             upstream-bch-light-local-retention.patch \
             upstream-bch-light-first-send.patch \
             upstream-bch-light-retry.patch \
             upstream-bch-light-acknowledgement.patch \
             upstream-bch-light-refund-wallet.patch \
             upstream-bch-light-helper-terms.patch \
             upstream-bch-light-helper-prepare.patch \
             upstream-bch-light-funding-sources.patch \
             upstream-bch-light-helper-sign.patch \
             upstream-bch-light-helper-observe.patch \
             upstream-light-funding-input-access.patch \
             upstream-bch-light-helper-send.patch \
             upstream-bch-light-helper-retry.patch \
             upstream-bch-light-source-selection.patch \
             upstream-bch-walletless-observer.patch \
             upstream-bch-light-observer-binding.patch \
             upstream-bch-light-success-release.patch \
             upstream-bch-light-native-claim.patch \
             upstream-bch-light-claim-wallet.patch \
             upstream-bch-light-native-payout.patch \
             upstream-bch-light-recovery-output.patch \
             upstream-bch-light-cancel-observation.patch \
             upstream-bch-light-contract-relay.patch \
             upstream-bch-success-publication-repair.patch \
             upstream-bch-success-publication-helper.patch \
             upstream-bch-publication-helper-integration.patch \
             upstream-bch-publication-node-fixture.patch \
             upstream-bch-publication-qt-integration.patch \
             upstream-bch-native-walletless-preflight.patch \
             upstream-bch-native-walletless-preflight-review.patch \
             upstream-bch-native-walletless-payment.patch \
             upstream-bch-native-walletless-refund.patch \
             upstream-bch-native-walletless-client-fixture.patch \
             upstream-bch-native-walletless-refund-wallet.patch \
             upstream-bch-native-walletless-refund-payout.patch \
             upstream-native-rpc-sensitive-buffers.patch \
             upstream-bch-native-walletless-punishment.patch \
             upstream-bch-native-cancellation-review.patch \
             upstream-bch-native-observer-helper.patch \
             upstream-bch-owned-observer-paired.patch \
             upstream-bch-owned-observer-qt-paired.patch \
             upstream-bch-native-cancel-helper.patch \
             upstream-bch-native-cancel-helper-fixture.patch \
             upstream-bch-punish-offline-helper.patch upstream-bch-punish-helper-fixture.patch upstream-bch-punish-observation-helper.patch upstream-bch-punish-observation-fixture.patch upstream-bch-punish-submit-helper.patch upstream-bch-punish-submit-native-fixture.patch upstream-bch-punish-submission-integration.patch upstream-bch-refund-wallet-status.patch upstream-bch-refund-wallet-approval.patch upstream-bch-refund-wallet-status-helper.patch upstream-bch-refund-wallet-helper-integration.patch upstream-bch-refund-wallet-review-helper.patch upstream-bch-refund-wallet-review-output.patch upstream-bch-refund-wallet-review-integration.patch upstream-bch-refund-wallet-submit-helper.patch upstream-bch-refund-wallet-separate-observer.patch upstream-bch-refund-wallet-mutation-fixture.patch upstream-bch-refund-wallet-mutation-assertions.patch \
             upstream-litecoin-testnet-stage.patch upstream-xmz-live-observation.patch \
             upstream-xmz-live-observation-bch-test.patch upstream-light-testnet-funding.patch \
             upstream-light-checkpoint.patch upstream-light-mweb-transactions.patch \
             upstream-ltc-observer-view-wallet.patch \
             upstream-bitcoin-swap-chain.patch \
             upstream-light-wallet-evm-swap-keys.patch; do
  git -C "$engine_dir" apply --cached "$script_dir/$patch"
done
# Includes added source files because the temporary index now tracks them.
git -C "$engine_dir" diff --exit-code --name-only
echo 'PASS: ordered patches reproduce all indexed engine sources'
echo "Temporary audit index retained at $audit_dir"
