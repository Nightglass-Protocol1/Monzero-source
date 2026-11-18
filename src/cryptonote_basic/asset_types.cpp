#include "asset_types.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>

#include <boost/uuid/uuid.hpp>

#include "crypto/hash.h"

namespace cryptonote
{
namespace assets
{
  namespace
  {
    constexpr char DOMAIN[] = "MonzeroAssetIssuanceV2";
    constexpr char ISSUANCE_AUTHORIZATION_DOMAIN[] = "MonzeroAssetIssuanceAuthorizationV1";
    constexpr char COLLECTION_MEMBERSHIP_DOMAIN[] = "MonzeroCollectionMembershipV1";
    constexpr char ISSUANCE_PAYLOAD_DOMAIN[] = "MonzeroAssetIssuancePayloadV1";
    constexpr char REGISTRY_SNAPSHOT_DOMAIN[] = "MonzeroAssetRegistrySnapshotV1";
    constexpr char TRANSACTION_EXTENSION_DOMAIN[] = "MonzeroAssetTransactionExtensionV1";
    constexpr uint8_t REGISTRY_SNAPSHOT_VERSION = 1;
    constexpr uint32_t MAX_REGISTRY_SNAPSHOT_RECORDS = 100000;

    bool fail(std::string* error, const char* message)
    {
      if (error)
        *error = message;
      return false;
    }

    template<typename T>
    void append_pod(std::vector<uint8_t>& target, const T& value)
    {
      const auto* begin = reinterpret_cast<const uint8_t*>(&value);
      target.insert(target.end(), begin, begin + sizeof(value));
    }

    void append_u16_le(std::vector<uint8_t>& target, uint16_t value)
    {
      target.push_back(static_cast<uint8_t>(value));
      target.push_back(static_cast<uint8_t>(value >> 8));
    }

    void append_u64_le(std::vector<uint8_t>& target, uint64_t value)
    {
      for (unsigned shift = 0; shift < 64; shift += 8)
        target.push_back(static_cast<uint8_t>(value >> shift));
    }

    void append_u32_le(std::vector<uint8_t>& target, uint32_t value)
    {
      for (unsigned shift = 0; shift < 32; shift += 8)
        target.push_back(static_cast<uint8_t>(value >> shift));
    }

    template<typename T>
    bool read_pod(const std::vector<uint8_t>& source, size_t& offset, T& value)
    {
      if (offset > source.size() || sizeof(value) > source.size() - offset)
        return false;
      std::memcpy(&value, source.data() + offset, sizeof(value));
      offset += sizeof(value);
      return true;
    }

    bool read_u16_le(const std::vector<uint8_t>& source, size_t& offset, uint16_t& value)
    {
      if (offset > source.size() || 2 > source.size() - offset)
        return false;
      value = static_cast<uint16_t>(source[offset])
        | static_cast<uint16_t>(source[offset + 1]) << 8;
      offset += 2;
      return true;
    }

    bool read_u64_le(const std::vector<uint8_t>& source, size_t& offset, uint64_t& value)
    {
      if (offset > source.size() || 8 > source.size() - offset)
        return false;
      value = 0;
      for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<uint64_t>(source[offset++]) << shift;
      return true;
    }

    bool read_u32_le(const std::vector<uint8_t>& source, size_t& offset, uint32_t& value)
    {
      if (offset > source.size() || 4 > source.size() - offset)
        return false;
      value = 0;
      for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<uint32_t>(source[offset++]) << shift;
      return true;
    }

    unsigned snapshot_class_priority(asset_class type)
    {
      return type == asset_class::collection ? 0 : 1;
    }
  }

  bool validate_issuance_descriptor(const issuance_descriptor& descriptor, std::string* error)
  {
    if (descriptor.version != ISSUANCE_DESCRIPTOR_VERSION)
      return fail(error, "unsupported issuance descriptor version");
    if (descriptor.network != MAINNET && descriptor.network != TESTNET && descriptor.network != STAGENET)
      return fail(error, "issuance descriptor requires an explicit public network");
    if (descriptor.type != asset_class::fungible
        && descriptor.type != asset_class::non_fungible
        && descriptor.type != asset_class::collection
        && descriptor.type != asset_class::edition)
      return fail(error, "unsupported asset class");
    if (!crypto::check_key(descriptor.issuer_key))
      return fail(error, "invalid issuer public key");
    if (descriptor.atomic_supply == 0)
      return fail(error, "asset supply must be greater than zero");
    if (descriptor.display_decimals > MAX_DISPLAY_DECIMALS)
      return fail(error, "asset display precision exceeds XMZ precision");
    if ((descriptor.type == asset_class::non_fungible || descriptor.type == asset_class::collection)
        && (descriptor.atomic_supply != 1 || descriptor.display_decimals != 0))
      return fail(error, "NFT and collection supply must be exactly one indivisible unit");
    if (descriptor.type == asset_class::edition && descriptor.display_decimals != 0)
      return fail(error, "edition supply must be indivisible");
    if ((descriptor.type == asset_class::fungible || descriptor.type == asset_class::collection)
        && descriptor.collection_id != crypto::null_hash)
      return fail(error, "this asset class cannot reference a collection");
    if ((descriptor.type == asset_class::non_fungible || descriptor.type == asset_class::edition)
        && descriptor.metadata_hash == crypto::null_hash)
      return fail(error, "NFT and edition metadata must have a content hash");
    if (descriptor.metadata_reference.size() > MAX_METADATA_REFERENCE_BYTES)
      return fail(error, "metadata reference is too long");
    if (std::any_of(descriptor.metadata_reference.begin(), descriptor.metadata_reference.end(), [](unsigned char c) {
          return c < 0x20 || c == 0x7f;
        }))
      return fail(error, "metadata reference contains an ASCII control byte");
    return true;
  }

  bool encode_issuance_descriptor(const issuance_descriptor& descriptor, std::vector<uint8_t>& encoded, std::string* error)
  {
    if (!validate_issuance_descriptor(descriptor, error))
      return false;

    encoded.clear();
    encoded.reserve(sizeof(DOMAIN) - 1 + 1 + 16 + 1 + 32 + 32 + 8 + 1 + 32 + 32 + 2 + descriptor.metadata_reference.size());
    encoded.insert(encoded.end(), DOMAIN, DOMAIN + sizeof(DOMAIN) - 1);
    encoded.push_back(descriptor.version);
    const config_t& network = get_config(descriptor.network);
    append_pod(encoded, network.NETWORK_ID);
    encoded.push_back(static_cast<uint8_t>(descriptor.type));
    append_pod(encoded, descriptor.issuer_key);
    append_pod(encoded, descriptor.issuance_nonce);
    append_u64_le(encoded, descriptor.atomic_supply);
    encoded.push_back(descriptor.display_decimals);
    append_pod(encoded, descriptor.metadata_hash);
    append_pod(encoded, descriptor.collection_id);
    append_u16_le(encoded, static_cast<uint16_t>(descriptor.metadata_reference.size()));
    encoded.insert(encoded.end(), descriptor.metadata_reference.begin(), descriptor.metadata_reference.end());
    return true;
  }

  bool decode_issuance_descriptor(const std::vector<uint8_t>& encoded, issuance_descriptor& descriptor, std::string* error)
  {
    constexpr size_t domain_size = sizeof(DOMAIN) - 1;
    constexpr size_t fixed_size = domain_size + 1 + 16 + 1 + 32 + 32 + 8 + 1 + 32 + 32 + 2;
    if (encoded.size() < fixed_size)
      return fail(error, "truncated issuance descriptor");
    if (!std::equal(DOMAIN, DOMAIN + domain_size, encoded.begin()))
      return fail(error, "invalid issuance descriptor domain");

    issuance_descriptor parsed;
    size_t offset = domain_size;
    parsed.version = encoded[offset++];

    boost::uuids::uuid network_id{};
    if (!read_pod(encoded, offset, network_id))
      return fail(error, "truncated issuance descriptor network");
    parsed.network = UNDEFINED;
    for (const network_type candidate : {MAINNET, TESTNET, STAGENET})
    {
      if (network_id == get_config(candidate).NETWORK_ID)
      {
        parsed.network = candidate;
        break;
      }
    }

    parsed.type = static_cast<asset_class>(encoded[offset++]);
    if (!read_pod(encoded, offset, parsed.issuer_key)
        || !read_pod(encoded, offset, parsed.issuance_nonce)
        || !read_u64_le(encoded, offset, parsed.atomic_supply))
      return fail(error, "truncated issuance descriptor identity");
    parsed.display_decimals = encoded[offset++];
    if (!read_pod(encoded, offset, parsed.metadata_hash)
        || !read_pod(encoded, offset, parsed.collection_id))
      return fail(error, "truncated issuance descriptor metadata");

    uint16_t reference_size = 0;
    if (!read_u16_le(encoded, offset, reference_size))
      return fail(error, "truncated issuance descriptor metadata length");
    if (reference_size > MAX_METADATA_REFERENCE_BYTES)
      return fail(error, "metadata reference is too long");
    if (offset > encoded.size() || reference_size != encoded.size() - offset)
      return fail(error, "issuance descriptor length is not canonical");
    parsed.metadata_reference.assign(
      reinterpret_cast<const char*>(encoded.data() + offset), reference_size);

    if (!validate_issuance_descriptor(parsed, error))
      return false;

    std::vector<uint8_t> canonical;
    if (!encode_issuance_descriptor(parsed, canonical, error) || canonical != encoded)
      return fail(error, "issuance descriptor encoding is not canonical");

    descriptor = std::move(parsed);
    return true;
  }

  bool derive_asset_id(const issuance_descriptor& descriptor, crypto::hash& asset_id, std::string* error)
  {
    std::vector<uint8_t> encoded;
    if (!encode_issuance_descriptor(descriptor, encoded, error))
      return false;
    asset_id = crypto::cn_fast_hash(encoded.data(), encoded.size());
    return true;
  }

  bool derive_issuance_authorization_hash(const issuance_descriptor& descriptor, crypto::hash& message, std::string* error)
  {
    std::vector<uint8_t> encoded;
    if (!encode_issuance_descriptor(descriptor, encoded, error))
      return false;
    std::vector<uint8_t> authorization;
    authorization.reserve(sizeof(ISSUANCE_AUTHORIZATION_DOMAIN) - 1 + encoded.size());
    authorization.insert(authorization.end(), ISSUANCE_AUTHORIZATION_DOMAIN,
      ISSUANCE_AUTHORIZATION_DOMAIN + sizeof(ISSUANCE_AUTHORIZATION_DOMAIN) - 1);
    authorization.insert(authorization.end(), encoded.begin(), encoded.end());
    message = crypto::cn_fast_hash(authorization.data(), authorization.size());
    return true;
  }

  bool verify_issuance_authorization(const issuance_descriptor& descriptor, const crypto::signature& signature, std::string* error)
  {
    crypto::hash message{};
    if (!derive_issuance_authorization_hash(descriptor, message, error))
      return false;
    if (!crypto::check_signature(message, descriptor.issuer_key, signature))
      return fail(error, "invalid asset issuer authorization signature");
    return true;
  }

  bool derive_collection_membership_hash(const crypto::hash& collection_id, const crypto::hash& member_asset_id, crypto::hash& message, std::string* error)
  {
    if (collection_id == crypto::null_hash || member_asset_id == crypto::null_hash)
      return fail(error, "collection and member asset IDs must be non-zero");
    std::vector<uint8_t> encoded;
    encoded.reserve(sizeof(COLLECTION_MEMBERSHIP_DOMAIN) - 1 + sizeof(collection_id) + sizeof(member_asset_id));
    encoded.insert(encoded.end(), COLLECTION_MEMBERSHIP_DOMAIN,
      COLLECTION_MEMBERSHIP_DOMAIN + sizeof(COLLECTION_MEMBERSHIP_DOMAIN) - 1);
    append_pod(encoded, collection_id);
    append_pod(encoded, member_asset_id);
    message = crypto::cn_fast_hash(encoded.data(), encoded.size());
    return true;
  }

  bool verify_collection_membership(
    const crypto::hash& collection_id,
    const crypto::hash& member_asset_id,
    const crypto::public_key& collection_controller,
    const crypto::signature& signature,
    std::string* error)
  {
    if (!crypto::check_key(collection_controller))
      return fail(error, "invalid collection controller public key");
    crypto::hash message{};
    if (!derive_collection_membership_hash(collection_id, member_asset_id, message, error))
      return false;
    if (!crypto::check_signature(message, collection_controller, signature))
      return fail(error, "invalid collection membership signature");
    return true;
  }

  bool create_issuance_payload(
    issuance_descriptor descriptor,
    const crypto::secret_key& issuer_secret,
    const boost::optional<crypto::secret_key>& collection_controller_secret,
    issuance_payload& payload,
    crypto::hash& asset_id,
    std::string* error)
  {
    crypto::public_key issuer_public{};
    if (!crypto::secret_key_to_public_key(issuer_secret, issuer_public))
      return fail(error, "invalid asset issuer secret key");
    descriptor.issuer_key = issuer_public;
    if (descriptor.issuance_nonce == crypto::null_hash)
      descriptor.issuance_nonce = crypto::rand<crypto::hash>();

    if (!validate_issuance_descriptor(descriptor, error)
        || !derive_asset_id(descriptor, asset_id, error))
      return false;

    const bool claims_collection = descriptor.collection_id != crypto::null_hash;
    if (claims_collection != static_cast<bool>(collection_controller_secret))
      return fail(error, claims_collection
        ? "collection controller secret key is required"
        : "collection controller secret key is only valid for a collection member");

    issuance_payload created;
    created.descriptor = std::move(descriptor);
    crypto::hash issuance_message{};
    if (!derive_issuance_authorization_hash(created.descriptor, issuance_message, error))
      return false;
    crypto::generate_signature(issuance_message, issuer_public, issuer_secret, created.issuer_signature);

    if (collection_controller_secret)
    {
      crypto::public_key collection_controller{};
      if (!crypto::secret_key_to_public_key(*collection_controller_secret, collection_controller))
        return fail(error, "invalid collection controller secret key");
      crypto::hash membership_message{};
      if (!derive_collection_membership_hash(created.descriptor.collection_id, asset_id, membership_message, error))
        return false;
      crypto::signature signature{};
      crypto::generate_signature(membership_message, collection_controller,
        *collection_controller_secret, signature);
      created.collection_signature = signature;
    }

    std::vector<uint8_t> canonical;
    if (!encode_issuance_payload(created, canonical, error))
      return false;
    payload = std::move(created);
    return true;
  }

  bool encode_issuance_payload(const issuance_payload& payload, std::vector<uint8_t>& encoded, std::string* error)
  {
    if (payload.version != ISSUANCE_PAYLOAD_VERSION)
      return fail(error, "unsupported issuance payload version");

    std::vector<uint8_t> descriptor;
    if (!encode_issuance_descriptor(payload.descriptor, descriptor, error))
      return false;
    if (descriptor.size() > std::numeric_limits<uint16_t>::max())
      return fail(error, "issuance descriptor does not fit its bounded payload");
    if (!verify_issuance_authorization(payload.descriptor, payload.issuer_signature, error))
      return false;

    const bool claims_collection = payload.descriptor.collection_id != crypto::null_hash;
    if (claims_collection != static_cast<bool>(payload.collection_signature))
      return fail(error, claims_collection
        ? "collection membership signature is missing"
        : "unexpected collection signature for an uncollected asset");

    encoded.clear();
    encoded.reserve(sizeof(ISSUANCE_PAYLOAD_DOMAIN) - 1 + 1 + 2 + descriptor.size()
      + sizeof(payload.issuer_signature) + 1
      + (payload.collection_signature ? sizeof(*payload.collection_signature) : 0));
    encoded.insert(encoded.end(), ISSUANCE_PAYLOAD_DOMAIN,
      ISSUANCE_PAYLOAD_DOMAIN + sizeof(ISSUANCE_PAYLOAD_DOMAIN) - 1);
    encoded.push_back(payload.version);
    append_u16_le(encoded, static_cast<uint16_t>(descriptor.size()));
    encoded.insert(encoded.end(), descriptor.begin(), descriptor.end());
    append_pod(encoded, payload.issuer_signature);
    encoded.push_back(payload.collection_signature ? 1 : 0);
    if (payload.collection_signature)
      append_pod(encoded, *payload.collection_signature);
    return true;
  }

  bool decode_issuance_payload(const std::vector<uint8_t>& encoded, issuance_payload& payload, std::string* error)
  {
    constexpr size_t domain_size = sizeof(ISSUANCE_PAYLOAD_DOMAIN) - 1;
    constexpr size_t minimum_size = domain_size + 1 + 2 + sizeof(crypto::signature) + 1;
    if (encoded.size() < minimum_size)
      return fail(error, "truncated issuance payload");
    if (!std::equal(ISSUANCE_PAYLOAD_DOMAIN,
          ISSUANCE_PAYLOAD_DOMAIN + domain_size, encoded.begin()))
      return fail(error, "invalid issuance payload domain");

    issuance_payload parsed;
    size_t offset = domain_size;
    parsed.version = encoded[offset++];
    if (parsed.version != ISSUANCE_PAYLOAD_VERSION)
      return fail(error, "unsupported issuance payload version");

    uint16_t descriptor_size = 0;
    if (!read_u16_le(encoded, offset, descriptor_size)
        || offset > encoded.size() || descriptor_size > encoded.size() - offset)
      return fail(error, "invalid issuance payload descriptor length");
    const std::vector<uint8_t> descriptor_bytes(
      encoded.begin() + offset, encoded.begin() + offset + descriptor_size);
    offset += descriptor_size;
    if (!decode_issuance_descriptor(descriptor_bytes, parsed.descriptor, error))
      return false;
    if (!read_pod(encoded, offset, parsed.issuer_signature))
      return fail(error, "truncated issuer authorization signature");
    if (offset >= encoded.size())
      return fail(error, "truncated collection signature flag");

    const uint8_t has_collection_signature = encoded[offset++];
    if (has_collection_signature > 1)
      return fail(error, "invalid collection signature flag");
    if (has_collection_signature)
    {
      crypto::signature signature{};
      if (!read_pod(encoded, offset, signature))
        return fail(error, "truncated collection authorization signature");
      parsed.collection_signature = signature;
    }
    if (offset != encoded.size())
      return fail(error, "issuance payload has trailing bytes");

    std::vector<uint8_t> canonical;
    if (!encode_issuance_payload(parsed, canonical, error) || canonical != encoded)
      return fail(error, "issuance payload encoding is not canonical");
    payload = std::move(parsed);
    return true;
  }

  bool encode_transaction_extension(const transaction_extension& extension, std::vector<uint8_t>& encoded, std::string* error)
  {
    if (extension.version != TRANSACTION_EXTENSION_VERSION)
      return fail(error, "unsupported asset transaction extension version");
    if (extension.network != MAINNET && extension.network != TESTNET && extension.network != STAGENET)
      return fail(error, "asset transaction extension requires an explicit public network");
    if (extension.operation != transaction_operation::issuance)
      return fail(error, "unsupported asset transaction operation");
    if (extension.carrier_prefix_hash == crypto::null_hash)
      return fail(error, "asset transaction extension requires a carrier prefix commitment");
    if (extension.issuance.descriptor.network != extension.network)
      return fail(error, "asset transaction extension network does not match its issuance");

    std::vector<uint8_t> operation;
    if (!encode_issuance_payload(extension.issuance, operation, error))
      return false;
    if (operation.size() > std::numeric_limits<uint16_t>::max())
      return fail(error, "asset transaction operation is too large");

    encoded.clear();
    encoded.reserve(sizeof(TRANSACTION_EXTENSION_DOMAIN) - 1 + 1 + 16 + 1
      + sizeof(extension.carrier_prefix_hash) + 2 + operation.size());
    encoded.insert(encoded.end(), TRANSACTION_EXTENSION_DOMAIN,
      TRANSACTION_EXTENSION_DOMAIN + sizeof(TRANSACTION_EXTENSION_DOMAIN) - 1);
    encoded.push_back(extension.version);
    append_pod(encoded, get_config(extension.network).NETWORK_ID);
    encoded.push_back(static_cast<uint8_t>(extension.operation));
    append_pod(encoded, extension.carrier_prefix_hash);
    append_u16_le(encoded, static_cast<uint16_t>(operation.size()));
    encoded.insert(encoded.end(), operation.begin(), operation.end());
    return true;
  }

  bool decode_transaction_extension(const std::vector<uint8_t>& encoded, transaction_extension& extension, std::string* error)
  {
    constexpr size_t domain_size = sizeof(TRANSACTION_EXTENSION_DOMAIN) - 1;
    constexpr size_t header_size = domain_size + 1 + 16 + 1 + sizeof(crypto::hash) + 2;
    if (encoded.size() < header_size)
      return fail(error, "truncated asset transaction extension");
    if (!std::equal(TRANSACTION_EXTENSION_DOMAIN,
          TRANSACTION_EXTENSION_DOMAIN + domain_size, encoded.begin()))
      return fail(error, "invalid asset transaction extension domain");

    transaction_extension parsed;
    size_t offset = domain_size;
    parsed.version = encoded[offset++];
    if (parsed.version != TRANSACTION_EXTENSION_VERSION)
      return fail(error, "unsupported asset transaction extension version");
    boost::uuids::uuid network_id{};
    if (!read_pod(encoded, offset, network_id))
      return fail(error, "truncated asset transaction extension network");
    parsed.network = UNDEFINED;
    for (const network_type candidate : {MAINNET, TESTNET, STAGENET})
    {
      if (network_id == get_config(candidate).NETWORK_ID)
      {
        parsed.network = candidate;
        break;
      }
    }
    parsed.operation = static_cast<transaction_operation>(encoded[offset++]);
    if (!read_pod(encoded, offset, parsed.carrier_prefix_hash))
      return fail(error, "truncated asset transaction carrier commitment");
    uint16_t operation_size = 0;
    if (!read_u16_le(encoded, offset, operation_size)
        || offset > encoded.size() || operation_size != encoded.size() - offset)
      return fail(error, "asset transaction operation length is not canonical");
    const std::vector<uint8_t> operation(
      encoded.begin() + offset, encoded.begin() + offset + operation_size);
    if (parsed.operation != transaction_operation::issuance)
      return fail(error, "unsupported asset transaction operation");
    if (!decode_issuance_payload(operation, parsed.issuance, error))
      return false;

    std::vector<uint8_t> canonical;
    if (!encode_transaction_extension(parsed, canonical, error) || canonical != encoded)
      return fail(error, "asset transaction extension encoding is not canonical");
    extension = std::move(parsed);
    return true;
  }

  bool derive_transaction_extension_id(const transaction_extension& extension, crypto::hash& extension_id, std::string* error)
  {
    std::vector<uint8_t> encoded;
    if (!encode_transaction_extension(extension, encoded, error))
      return false;
    extension_id = crypto::cn_fast_hash(encoded.data(), encoded.size());
    return true;
  }

  bool validate_transaction_extension_carrier(
    const transaction_extension& extension,
    network_type expected_network,
    const crypto::hash& expected_prefix_hash,
    std::string* error)
  {
    if (expected_network != MAINNET && expected_network != TESTNET && expected_network != STAGENET)
      return fail(error, "asset transaction carrier validation requires an explicit network");
    if (expected_prefix_hash == crypto::null_hash)
      return fail(error, "expected native transaction prefix commitment is zero");
    if (extension.network != expected_network)
      return fail(error, "asset transaction extension is for another network");
    if (extension.carrier_prefix_hash != expected_prefix_hash)
      return fail(error, "asset transaction extension carrier commitment mismatch");
    std::vector<uint8_t> canonical;
    return encode_transaction_extension(extension, canonical, error);
  }

  bool asset_registry::apply_issuance(
    const issuance_descriptor& descriptor,
    const crypto::signature& issuer_signature,
    const boost::optional<crypto::signature>& collection_signature,
    uint64_t height,
    crypto::hash& asset_id,
    std::string* error)
  {
    if (!derive_asset_id(descriptor, asset_id, error))
      return false;
    if (contains(asset_id))
      return fail(error, "asset ID is already registered");
    if (!verify_issuance_authorization(descriptor, issuer_signature, error))
      return false;

    const bool claims_collection = descriptor.collection_id != crypto::null_hash;
    if (claims_collection)
    {
      const asset_record* collection = find(descriptor.collection_id);
      if (!collection || collection->descriptor.type != asset_class::collection)
        return fail(error, "referenced collection is unknown or not a collection");
      if (collection->descriptor.network != descriptor.network)
        return fail(error, "collection and member must use the same network");
      if (collection->issuance_height > height)
        return fail(error, "collection membership cannot precede collection issuance");
      if (!collection_signature)
        return fail(error, "collection membership requires controller authorization");
      if (!verify_collection_membership(
            descriptor.collection_id,
            asset_id,
            collection->descriptor.issuer_key,
            *collection_signature,
            error))
        return false;
    }
    else if (collection_signature)
      return fail(error, "unexpected collection signature for an uncollected asset");

    records_.emplace(asset_id, asset_record{descriptor, height, issuer_signature, collection_signature});
    return true;
  }

  bool asset_registry::apply_issuance(
    const issuance_payload& payload,
    uint64_t height,
    crypto::hash& asset_id,
    std::string* error)
  {
    return apply_issuance(
      payload.descriptor,
      payload.issuer_signature,
      payload.collection_signature,
      height,
      asset_id,
      error);
  }

  bool asset_registry::apply_block_issuances(
    const std::vector<issuance_payload>& payloads,
    uint64_t height,
    std::vector<crypto::hash>& asset_ids,
    std::string* error)
  {
    asset_registry candidate = *this;
    std::vector<crypto::hash> candidate_ids;
    candidate_ids.reserve(payloads.size());
    for (const issuance_payload& payload : payloads)
    {
      crypto::hash asset_id{};
      if (!candidate.apply_issuance(payload, height, asset_id, error))
        return false;
      candidate_ids.push_back(asset_id);
    }
    records_ = std::move(candidate.records_);
    asset_ids = std::move(candidate_ids);
    return true;
  }

  bool asset_registry::apply_block_extensions(
    const std::vector<transaction_extension>& extensions,
    const std::vector<crypto::hash>& carrier_prefix_hashes,
    network_type expected_network,
    uint64_t height,
    std::vector<crypto::hash>& asset_ids,
    std::string* error)
  {
    if (extensions.size() != carrier_prefix_hashes.size())
      return fail(error, "asset extension and carrier commitment counts differ");
    std::vector<issuance_payload> issuances;
    issuances.reserve(extensions.size());
    for (size_t index = 0; index < extensions.size(); ++index)
    {
      const transaction_extension& extension = extensions[index];
      if (!validate_transaction_extension_carrier(
            extension, expected_network, carrier_prefix_hashes[index], error))
        return false;
      if (extension.operation != transaction_operation::issuance)
        return fail(error, "unsupported asset block operation");
      issuances.push_back(extension.issuance);
    }
    return apply_block_issuances(issuances, height, asset_ids, error);
  }

  void asset_registry::detach(uint64_t height)
  {
    for (auto it = records_.begin(); it != records_.end();)
    {
      if (it->second.issuance_height >= height)
        it = records_.erase(it);
      else
        ++it;
    }
  }

  bool asset_registry::contains(const crypto::hash& asset_id) const
  {
    return records_.count(asset_id) != 0;
  }

  const asset_record* asset_registry::find(const crypto::hash& asset_id) const
  {
    const auto found = records_.find(asset_id);
    return found == records_.end() ? nullptr : &found->second;
  }

  std::set<crypto::hash> asset_registry::known_assets() const
  {
    std::set<crypto::hash> result;
    for (const auto& record : records_)
      result.insert(record.first);
    return result;
  }

  bool asset_registry::encode_snapshot(network_type network, std::vector<uint8_t>& encoded, std::string* error) const
  {
    if (network != MAINNET && network != TESTNET && network != STAGENET)
      return fail(error, "asset registry snapshot requires an explicit public network");
    if (records_.size() > MAX_REGISTRY_SNAPSHOT_RECORDS)
      return fail(error, "asset registry snapshot has too many records");

    using ordered_record = std::pair<crypto::hash, const asset_record*>;
    std::vector<ordered_record> ordered;
    ordered.reserve(records_.size());
    for (const auto& entry : records_)
    {
      if (entry.second.descriptor.network != network)
        return fail(error, "asset registry contains a record from another network");
      ordered.emplace_back(entry.first, &entry.second);
    }
    std::sort(ordered.begin(), ordered.end(), [](const ordered_record& left, const ordered_record& right) {
      if (left.second->issuance_height != right.second->issuance_height)
        return left.second->issuance_height < right.second->issuance_height;
      const unsigned left_priority = snapshot_class_priority(left.second->descriptor.type);
      const unsigned right_priority = snapshot_class_priority(right.second->descriptor.type);
      if (left_priority != right_priority)
        return left_priority < right_priority;
      return left.first < right.first;
    });

    encoded.clear();
    encoded.insert(encoded.end(), REGISTRY_SNAPSHOT_DOMAIN,
      REGISTRY_SNAPSHOT_DOMAIN + sizeof(REGISTRY_SNAPSHOT_DOMAIN) - 1);
    encoded.push_back(REGISTRY_SNAPSHOT_VERSION);
    append_pod(encoded, get_config(network).NETWORK_ID);
    append_u32_le(encoded, static_cast<uint32_t>(ordered.size()));
    for (const ordered_record& entry : ordered)
    {
      issuance_payload payload;
      payload.descriptor = entry.second->descriptor;
      payload.issuer_signature = entry.second->issuer_signature;
      payload.collection_signature = entry.second->collection_signature;
      std::vector<uint8_t> payload_bytes;
      if (!encode_issuance_payload(payload, payload_bytes, error))
        return false;
      if (payload_bytes.size() > std::numeric_limits<uint16_t>::max())
        return fail(error, "issuance payload is too large for a registry snapshot");
      append_u64_le(encoded, entry.second->issuance_height);
      append_u16_le(encoded, static_cast<uint16_t>(payload_bytes.size()));
      encoded.insert(encoded.end(), payload_bytes.begin(), payload_bytes.end());
    }
    return true;
  }

  bool asset_registry::decode_snapshot(const std::vector<uint8_t>& encoded, network_type expected_network, std::string* error)
  {
    constexpr size_t domain_size = sizeof(REGISTRY_SNAPSHOT_DOMAIN) - 1;
    constexpr size_t header_size = domain_size + 1 + 16 + 4;
    if (expected_network != MAINNET && expected_network != TESTNET && expected_network != STAGENET)
      return fail(error, "asset registry snapshot requires an explicit expected network");
    if (encoded.size() < header_size)
      return fail(error, "truncated asset registry snapshot");
    if (!std::equal(REGISTRY_SNAPSHOT_DOMAIN,
          REGISTRY_SNAPSHOT_DOMAIN + domain_size, encoded.begin()))
      return fail(error, "invalid asset registry snapshot domain");

    size_t offset = domain_size;
    if (encoded[offset++] != REGISTRY_SNAPSHOT_VERSION)
      return fail(error, "unsupported asset registry snapshot version");
    boost::uuids::uuid network_id{};
    if (!read_pod(encoded, offset, network_id)
        || network_id != get_config(expected_network).NETWORK_ID)
      return fail(error, "asset registry snapshot network mismatch");
    uint32_t count = 0;
    if (!read_u32_le(encoded, offset, count) || count > MAX_REGISTRY_SNAPSHOT_RECORDS)
      return fail(error, "invalid asset registry snapshot record count");

    asset_registry restored;
    bool have_previous = false;
    uint64_t previous_height = 0;
    unsigned previous_priority = 0;
    crypto::hash previous_id{};
    for (uint32_t index = 0; index < count; ++index)
    {
      uint64_t height = 0;
      uint16_t payload_size = 0;
      if (!read_u64_le(encoded, offset, height)
          || !read_u16_le(encoded, offset, payload_size)
          || offset > encoded.size() || payload_size > encoded.size() - offset)
        return fail(error, "truncated asset registry snapshot record");
      const std::vector<uint8_t> payload_bytes(
        encoded.begin() + offset, encoded.begin() + offset + payload_size);
      offset += payload_size;

      issuance_payload payload;
      if (!decode_issuance_payload(payload_bytes, payload, error))
        return false;
      if (payload.descriptor.network != expected_network)
        return fail(error, "asset registry record network mismatch");
      crypto::hash asset_id{};
      if (!derive_asset_id(payload.descriptor, asset_id, error))
        return false;
      const unsigned priority = snapshot_class_priority(payload.descriptor.type);
      if (have_previous && (height < previous_height
          || (height == previous_height && (priority < previous_priority
            || (priority == previous_priority && !(previous_id < asset_id))))))
        return fail(error, "asset registry snapshot records are not in canonical order");
      if (!restored.apply_issuance(payload, height, asset_id, error))
        return false;
      have_previous = true;
      previous_height = height;
      previous_priority = priority;
      previous_id = asset_id;
    }
    if (offset != encoded.size())
      return fail(error, "asset registry snapshot has trailing bytes");

    records_ = std::move(restored.records_);
    return true;
  }

  bool asset_registry::derive_snapshot_hash(network_type network, crypto::hash& snapshot_hash, std::string* error) const
  {
    std::vector<uint8_t> snapshot;
    if (!encode_snapshot(network, snapshot, error))
      return false;
    snapshot_hash = crypto::cn_fast_hash(snapshot.data(), snapshot.size());
    return true;
  }

  bool validate_transparent_balance_statement(
    const transparent_balance_statement& statement,
    const std::set<crypto::hash>& known_assets,
    std::string* error)
  {
    if (statement.xmz_outputs > std::numeric_limits<uint64_t>::max() - statement.xmz_fee
        || statement.xmz_inputs != statement.xmz_outputs + statement.xmz_fee)
      return fail(error, "XMZ inputs must equal XMZ outputs plus the XMZ fee");

    using balances = std::map<crypto::hash, uint64_t>;
    balances inputs;
    balances outputs;
    balances burns;

    const auto aggregate = [error](const std::vector<transparent_amount>& entries, balances& totals) {
      for (const transparent_amount& entry : entries)
      {
        if (entry.asset_id == crypto::null_hash)
          return fail(error, "the zero asset ID is reserved and invalid");
        if (entry.amount == 0)
          return fail(error, "zero-valued asset entries are not canonical");
        uint64_t& total = totals[entry.asset_id];
        if (entry.amount > std::numeric_limits<uint64_t>::max() - total)
          return fail(error, "asset amount aggregation overflow");
        total += entry.amount;
      }
      return true;
    };

    if (!aggregate(statement.asset_inputs, inputs)
        || !aggregate(statement.asset_outputs, outputs)
        || !aggregate(statement.asset_burns, burns))
      return false;

    boost::optional<crypto::hash> issued_asset;
    if (statement.issuance)
    {
      crypto::hash asset_id{};
      if (!derive_asset_id(*statement.issuance, asset_id, error))
        return false;
      if (known_assets.count(asset_id) != 0)
        return fail(error, "asset ID is already issued");
      if (inputs.count(asset_id) != 0 || burns.count(asset_id) != 0)
        return fail(error, "newly issued supply cannot have inputs or burns");
      const auto issued_outputs = outputs.find(asset_id);
      if (issued_outputs == outputs.end() || issued_outputs->second != statement.issuance->atomic_supply)
        return fail(error, "issuance outputs must equal the complete fixed supply");
      issued_asset = asset_id;
    }

    std::set<crypto::hash> touched;
    for (const auto& entry : inputs) touched.insert(entry.first);
    for (const auto& entry : outputs) touched.insert(entry.first);
    for (const auto& entry : burns) touched.insert(entry.first);
    if (touched.empty())
      return fail(error, "asset statement contains no issuance, transfer, or burn");

    for (const crypto::hash& asset_id : touched)
    {
      if (issued_asset && asset_id == *issued_asset)
        continue;
      if (known_assets.count(asset_id) == 0)
        return fail(error, "statement references an unknown asset ID");

      const uint64_t input = inputs.count(asset_id) ? inputs[asset_id] : 0;
      const uint64_t output = outputs.count(asset_id) ? outputs[asset_id] : 0;
      const uint64_t burn = burns.count(asset_id) ? burns[asset_id] : 0;
      if (output > std::numeric_limits<uint64_t>::max() - burn || input != output + burn)
        return fail(error, "asset inputs must equal outputs plus explicit burns");
    }
    return true;
  }
}
}
