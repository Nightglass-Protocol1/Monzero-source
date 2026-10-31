//! The in-app EVM account, from the wallet seed in the seed vault.
//!
//! Standard BIP-32 on the Ethereum path m/44'/60'/0'/0/i, so the wallet's recovery words
//! restore the same accounts in any EVM wallet (MetaMask and others), not only in this app.
//! BNB Chain uses the same accounts.

use alloy::primitives::B256;
use alloy::signers::local::PrivateKeySigner;

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum EvmKeyError {
    #[error("wallet seed must be 16 to 64 bytes")]
    SeedLength,
    #[error("account index must be below 2^31")]
    Index,
    /// Probability about 2^-127 per step; BIP-32 says to skip to the next index
    #[error("this index derives an invalid key; use the next one")]
    InvalidChild,
}

/// The EVM account at m/44'/60'/0'/0/`index` of the BIP-32 seed `wallet_seed` (the 64-byte
/// BIP-39 seed for wallets with recovery words). Derived by `monzero-evm-keys`, which the
/// vault helper uses too.
pub fn evm_account(wallet_seed: &[u8], index: u32) -> Result<PrivateKeySigner, EvmKeyError> {
    let key = monzero_evm_keys::evm_private_key(wallet_seed, index).map_err(|e| match e {
        monzero_evm_keys::Error::SeedLength => EvmKeyError::SeedLength,
        monzero_evm_keys::Error::Index => EvmKeyError::Index,
        monzero_evm_keys::Error::InvalidChild => EvmKeyError::InvalidChild,
    })?;
    Ok(PrivateKeySigner::from_bytes(&B256::from(*key)).expect("nonzero scalar below n"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloy::primitives::address;

    /// BIP-39 seed of "test test test test test test test test test test test junk", whose
    /// accounts every EVM tool (Hardhat, Foundry, MetaMask) shows
    const SEED: &str = "9dfc3c64c2f8bede1533b6a79f8570e5943e0b8fd1cf77107adf7b72cef42185d564a3aee24cab43f80e3c4538087d70fc824eabbad596a23c97b6ee8322ccc0";

    #[test]
    fn same_accounts_as_other_evm_wallets() {
        let seed = hex::decode(SEED).unwrap();
        assert_eq!(evm_account(&seed, 0).unwrap().address(), address!("f39Fd6e51aad88F6F4ce6aB8827279cffFb92266"));
        assert_eq!(evm_account(&seed, 1).unwrap().address(), address!("70997970C51812dc3A010C7d01b50e0d17dc79C8"));
        assert_eq!(evm_account(&seed, 2).unwrap().address(), address!("3C44CdDdB6a900fa2b585dd299e03d12FA4293BC"));
        assert_eq!(
            &evm_account(&seed, 0).unwrap().credential().to_bytes()[..],
            hex::decode("ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80").unwrap()
        );
    }

    #[test]
    fn bad_seeds_and_indexes_are_refused() {
        assert_eq!(evm_account(&[1; 15], 0).err(), Some(EvmKeyError::SeedLength));
        assert_eq!(evm_account(&[1; 65], 0).err(), Some(EvmKeyError::SeedLength));
        assert_eq!(evm_account(&[1; 32], 1 << 31).err(), Some(EvmKeyError::Index));
        assert!(evm_account(&[1; 32], (1 << 31) - 1).is_ok());
    }
}
