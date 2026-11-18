#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <boost/optional.hpp>

#include "crypto/crypto.h"
#include "cryptonote_config.h"

namespace cryptonote
{
namespace assets
{
  constexpr uint8_t ISSUANCE_DESCRIPTOR_VERSION = 2;
  constexpr uint8_t MAX_DISPLAY_DECIMALS = CRYPTONOTE_DISPLAY_DECIMAL_POINT;
  constexpr size_t MAX_METADATA_REFERENCE_BYTES = 256;

  enum class asset_class : uint8_t
  {
    fungible = 1,
    non_fungible = 2,
    collection = 3,
    edition = 4
  };

  // Research-only fixed-supply issuance identity. This type is deliberately
  // not part of transaction serialization and is not accepted by consensus.
  struct issuance_descriptor
  {
    uint8_t version = ISSUANCE_DESCRIPTOR_VERSION;
    network_type network = UNDEFINED;
    asset_class type = asset_class::fungible;
    crypto::public_key issuer_key{};
    crypto::hash issuance_nonce{};
    uint64_t atomic_supply = 0;
    uint8_t display_decimals = 0;
    crypto::hash metadata_hash{};
    // Required for NFTs/editions that claim collection membership. The zero
    // hash means no collection; fungible and collection descriptors must use
    // zero so the field cannot acquire ambiguous meaning.
    crypto::hash collection_id{};
    std::string metadata_reference;
  };

  bool validate_issuance_descriptor(const issuance_descriptor& descriptor, std::string* error = nullptr);
  bool encode_issuance_descriptor(const issuance_descriptor& descriptor, std::vector<uint8_t>& encoded, std::string* error = nullptr);
  bool decode_issuance_descriptor(const std::vector<uint8_t>& encoded, issuance_descriptor& descriptor, std::string* error = nullptr);
  bool derive_asset_id(const issuance_descriptor& descriptor, crypto::hash& asset_id, std::string* error = nullptr);
  bool derive_issuance_authorization_hash(const issuance_descriptor& descriptor, crypto::hash& message, std::string* error = nullptr);
  bool verify_issuance_authorization(const issuance_descriptor& descriptor, const crypto::signature& signature, std::string* error = nullptr);
  bool derive_collection_membership_hash(const crypto::hash& collection_id, const crypto::hash& member_asset_id, crypto::hash& message, std::string* error = nullptr);
  bool verify_collection_membership(
    const crypto::hash& collection_id,
    const crypto::hash& member_asset_id,
    const crypto::public_key& collection_controller,
    const crypto::signature& signature,
    std::string* error = nullptr);

  constexpr uint8_t ISSUANCE_PAYLOAD_VERSION = 1;

  // Inactive, bounded wire prototype for an authenticated fixed-supply
  // issuance. It is not yet a transaction field or consensus type.
  struct issuance_payload
  {
    uint8_t version = ISSUANCE_PAYLOAD_VERSION;
    issuance_descriptor descriptor;
    crypto::signature issuer_signature{};
    boost::optional<crypto::signature> collection_signature;
  };

  // Constructs and signs a canonical inactive issuance payload. The issuer
  // public key is always derived from issuer_secret, and a fresh nonce is
  // generated when descriptor.issuance_nonce is zero. A collection controller
  // secret is required exactly when the descriptor claims collection
  // membership. This helper does not submit, broadcast, or activate assets.
  bool create_issuance_payload(
    issuance_descriptor descriptor,
    const crypto::secret_key& issuer_secret,
    const boost::optional<crypto::secret_key>& collection_controller_secret,
    issuance_payload& payload,
    crypto::hash& asset_id,
    std::string* error = nullptr);

  bool encode_issuance_payload(const issuance_payload& payload, std::vector<uint8_t>& encoded, std::string* error = nullptr);
  bool decode_issuance_payload(const std::vector<uint8_t>& encoded, issuance_payload& payload, std::string* error = nullptr);

  constexpr uint8_t TRANSACTION_EXTENSION_VERSION = 1;
  enum class transaction_operation : uint8_t
  {
    issuance = 1
  };

  // Detached transaction-extension prototype. The carrier commitment binds
  // the operation to a native transaction prefix without changing active
  // transaction serialization or validity.
  struct transaction_extension
  {
    uint8_t version = TRANSACTION_EXTENSION_VERSION;
    network_type network = UNDEFINED;
    transaction_operation operation = transaction_operation::issuance;
    crypto::hash carrier_prefix_hash{};
    issuance_payload issuance;
  };

  bool encode_transaction_extension(const transaction_extension& extension, std::vector<uint8_t>& encoded, std::string* error = nullptr);
  bool decode_transaction_extension(const std::vector<uint8_t>& encoded, transaction_extension& extension, std::string* error = nullptr);
  bool derive_transaction_extension_id(const transaction_extension& extension, crypto::hash& extension_id, std::string* error = nullptr);
  bool validate_transaction_extension_carrier(
    const transaction_extension& extension,
    network_type expected_network,
    const crypto::hash& expected_prefix_hash,
    std::string* error = nullptr);

  struct asset_record
  {
    issuance_descriptor descriptor;
    uint64_t issuance_height = 0;
    crypto::signature issuer_signature{};
    boost::optional<crypto::signature> collection_signature;
  };

  // Inactive in-memory reference model for authenticated issuance state and
  // deterministic reorg rollback. Production consensus/database code must not
  // use this class without a separately reviewed activation change.
  class asset_registry
  {
  public:
    bool apply_issuance(
      const issuance_descriptor& descriptor,
      const crypto::signature& issuer_signature,
      const boost::optional<crypto::signature>& collection_signature,
      uint64_t height,
      crypto::hash& asset_id,
      std::string* error = nullptr);
    bool apply_issuance(const issuance_payload& payload, uint64_t height, crypto::hash& asset_id, std::string* error = nullptr);
    bool apply_block_issuances(
      const std::vector<issuance_payload>& payloads,
      uint64_t height,
      std::vector<crypto::hash>& asset_ids,
      std::string* error = nullptr);
    bool apply_block_extensions(
      const std::vector<transaction_extension>& extensions,
      const std::vector<crypto::hash>& carrier_prefix_hashes,
      network_type expected_network,
      uint64_t height,
      std::vector<crypto::hash>& asset_ids,
      std::string* error = nullptr);
    void detach(uint64_t height);
    bool contains(const crypto::hash& asset_id) const;
    const asset_record* find(const crypto::hash& asset_id) const;
    size_t size() const { return records_.size(); }
    std::set<crypto::hash> known_assets() const;
    bool encode_snapshot(network_type network, std::vector<uint8_t>& encoded, std::string* error = nullptr) const;
    bool decode_snapshot(const std::vector<uint8_t>& encoded, network_type expected_network, std::string* error = nullptr);
    bool derive_snapshot_hash(network_type network, crypto::hash& snapshot_hash, std::string* error = nullptr) const;

  private:
    std::map<crypto::hash, asset_record> records_;
  };

  struct transparent_amount
  {
    crypto::hash asset_id{};
    uint64_t amount = 0;
  };

  // Test-only semantic statement used to validate conservation rules before a
  // confidential commitment/proof construction is selected. It is not a wire
  // transaction and must never be serialized into the public protocol.
  struct transparent_balance_statement
  {
    uint64_t xmz_inputs = 0;
    uint64_t xmz_outputs = 0;
    uint64_t xmz_fee = 0;
    boost::optional<issuance_descriptor> issuance;
    std::vector<transparent_amount> asset_inputs;
    std::vector<transparent_amount> asset_outputs;
    std::vector<transparent_amount> asset_burns;
  };

  bool validate_transparent_balance_statement(
    const transparent_balance_statement& statement,
    const std::set<crypto::hash>& known_assets,
    std::string* error = nullptr);
}
}
