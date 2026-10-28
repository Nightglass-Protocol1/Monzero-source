# Wallet asset workflow

Status: experimental and inactive

Monzero assets are not active at consensus on the currently configured
networks. `asset_create` and `asset_inspect` therefore provide a safe offline
workflow today. The activation-gated `asset_issue` path is also implemented,
but refuses to construct a transaction until the connected daemon reports the
asset hard fork active. Do not represent an exported artifact as an issued
token or NFT.

## Create an artifact

Open a full software wallet on the intended network, then run:

```text
asset_create <class> <atomic_supply> <decimals> <metadata_hash|none> <metadata_reference|none> <collection_id|none> <filename>
```

Classes are `fungible`, `nft`, `collection`, and `edition`.

Examples:

```text
asset_create fungible 100000000 2 none ipfs://example none token.mzi
asset_create nft 1 0 <64-character-metadata-hash> ipfs://example none artwork.mzi
asset_create collection 1 0 none ipfs://example none collection.mzi
asset_create edition 100 0 <64-character-metadata-hash> ipfs://example <collection-id> edition.mzi
```

NFTs and collections require a supply of one and zero decimals. Editions are
indivisible. NFTs and editions require a non-zero metadata content hash. A
collection member is authorized with the current wallet's primary spend key;
future registry validation will only accept it if that wallet controls the
referenced collection.

Creation is rejected for watch-only, background, multisig, and hardware
wallets. Those signing modes need separate reviewed protocols before support.

## Inspect an artifact

```text
asset_inspect <filename>
```

Inspection validates the bounded canonical payload, issuer signature, declared
asset ID, network, class invariants, and inactive marker. A collection signature
can be identified in isolation, but its authority cannot be established until
the referenced collection is available in an authenticated asset registry.

Metadata references and their content are public and untrusted. Applications
must verify downloaded metadata against the signed metadata hash and must not
execute active content.

## Issue after activation

Once the connected daemon reports the asset hard fork active, a full software
wallet can construct and submit an issuance directly:

```text
asset_issue <address> <fungible|nft|collection|edition> <atomic_supply> <decimals> <metadata_hash|none> <metadata_reference|none> <collection_id|none>
```

The command constructs exactly one native fee-paying transaction, attaches the
signed asset envelope before the native prefix is signed, verifies the finished
envelope, displays the permanent asset ID and fee, and asks for confirmation
before relay. It refuses inactive daemons, light wallets, watch-only wallets,
multisig wallets, and hardware wallets. With `--do-not-relay`, it uses the
wallet's existing raw-transaction save path instead of submission.

Each issuance creates a 16-output same-asset anonymity pool. The recipient's
funded output is accompanied by zero-value blinded outputs so even a unique NFT
has enough members for the fixed ownership ring.

## View confirmed holdings

After synchronizing the wallet, run:

```text
asset_list
```

The command groups confirmed outputs by asset ID and reports raw unspent atomic
amounts plus spent/unspent output counts. It deliberately does not fetch or
trust external metadata, so display precision and names are not applied.

## Transfer or burn after activation

```text
asset_transfer <asset_id> <address> <atomic_amount>
asset_burn <asset_id> <atomic_amount>
```

Both commands select a confirmed wallet-owned output, retrieve a bounded set of
ring members by index/output ID, authenticate the real output against its
wallet opening, construct a confidential transfer proof, and pay the native
transaction fee from the wallet. Change returns to the active account. A burn
permanently removes the requested amount and requires an explicit warning and
confirmation before submission.

The current constructor is intentionally limited to one asset input. A wallet
therefore needs one unspent asset output large enough for the transfer plus
burn amount. Light, watch-only, background, multisig, and hardware wallets are
not supported. These paths fail closed before HF17 activates and still require
activated-chain restoration/reorg tests and independent cryptographic review.

## Wallet RPC

Automated software-wallet applications can use these JSON-RPC methods:

```text
create_asset
get_assets
transfer_asset
```

`create_asset` accepts the same descriptor fields as `asset_issue`, including
`asset_type`, `atomic_supply`, `decimals`, metadata hash/reference, collection
ID, recipient address, native account/priority/ring settings, and optional
`do_not_relay`, `get_tx_hex`, and `get_tx_metadata` controls.

`get_assets` returns confirmed wallet-owned outputs with raw atomic amount,
asset/output/transaction IDs, subaddress indices, spend state, and heights. It
can filter by asset ID and optionally include spent outputs.

`transfer_asset` accepts one asset ID, a recipient and transfer amount, an
optional explicit burn amount, native account/priority/ring settings, and the
same non-relay/export controls. A burn-only request omits the recipient and
sets `amount` to zero. Mutating asset methods are unavailable in restricted
wallet RPC mode. Integrated addresses are rejected because asset envelopes do
not use native payment IDs. All amounts are unsigned raw atomic units.
