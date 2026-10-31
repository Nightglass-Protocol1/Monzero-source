//! XMZ amount boundary for the engine port. No wallet or network authority.
//! Raw wallet/RPC atom counts are preserved, never scaled from/to XMR units.
use std::{fmt, str::FromStr};

#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub struct XmzAmount(u64);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AmountError { Invalid, Precision, Overflow }

impl XmzAmount {
    pub const SCALE: u64 = 100_000_000_000;
    pub const fn from_atoms(atoms: u64) -> Self { Self(atoms) }
    pub const fn atoms(self) -> u64 { self.0 }
    // Decimal strings avoid JSON/JavaScript number precision loss at the bridge.
    pub fn atom_string(self) -> String { self.0.to_string() }
    pub fn from_atom_string(value: &str) -> Result<Self, AmountError> {
        if !canonical_digits(value) { return Err(AmountError::Invalid); }
        value.parse::<u64>().map(Self).map_err(|_| AmountError::Overflow)
    }
    pub fn checked_add(self, other: Self) -> Option<Self> {
        self.0.checked_add(other.0).map(Self)
    }
    pub fn checked_sub(self, other: Self) -> Option<Self> {
        self.0.checked_sub(other.0).map(Self)
    }
}

fn canonical_digits(value: &str) -> bool {
    !value.is_empty() && value.bytes().all(|b| b.is_ascii_digit())
        && (value.len() == 1 || !value.starts_with('0'))
}

impl FromStr for XmzAmount {
    type Err = AmountError;
    fn from_str(value: &str) -> Result<Self, Self::Err> {
        let (whole, fraction) = match value.split_once('.') {
            Some((w, f)) => {
                if f.is_empty() || !f.bytes().all(|b| b.is_ascii_digit()) {
                    return Err(AmountError::Invalid);
                }
                if f.len() > 11 { return Err(AmountError::Precision); }
                (w, f)
            }
            None => (value, ""),
        };
        if !canonical_digits(whole) { return Err(AmountError::Invalid); }
        let whole = whole.parse::<u64>().map_err(|_| AmountError::Overflow)?;
        let fractional = if fraction.is_empty() { 0 } else {
            fraction.parse::<u64>().map_err(|_| AmountError::Invalid)?
                * 10u64.pow(11 - fraction.len() as u32)
        };
        whole.checked_mul(Self::SCALE).and_then(|w| w.checked_add(fractional))
            .map(Self).ok_or(AmountError::Overflow)
    }
}

impl fmt::Display for XmzAmount {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let whole = self.0 / Self::SCALE;
        let fraction = self.0 % Self::SCALE;
        if fraction == 0 { return write!(f, "{whole}"); }
        let padded = format!("{fraction:011}");
        write!(f, "{whole}.{}", padded.trim_end_matches('0'))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn xmz_not_xmr_scale() {
        assert_eq!("1".parse::<XmzAmount>().unwrap().atoms(), 100_000_000_000);
        assert_eq!(XmzAmount::from_atoms(1_000_000_000_000).to_string(), "10");
        assert_eq!(XmzAmount::from_atoms(3_000_000_000).to_string(), "0.03");
        assert_eq!("0.00000000001".parse::<XmzAmount>().unwrap().atoms(), 1);
    }
    #[test]
    fn strict_input_and_precision() {
        for s in ["", ".1", "1.", "01", "+1", "-1", " 1", "1 ", "1e2", "NaN", "1.2.3", "１"] {
            assert!(s.parse::<XmzAmount>().is_err(), "accepted {s:?}");
        }
        assert_eq!("0.000000000001".parse::<XmzAmount>(), Err(AmountError::Precision));
        assert_eq!("1.000000000000".parse::<XmzAmount>(), Err(AmountError::Precision));
    }
    #[test]
    fn full_u64_range_roundtrips_without_float() {
        for atoms in [0, 1, 10, XmzAmount::SCALE, 9_007_199_254_740_993, u64::MAX] {
            let amount = XmzAmount::from_atoms(atoms);
            assert_eq!(amount.to_string().parse::<XmzAmount>(), Ok(amount));
            assert_eq!(XmzAmount::from_atom_string(&amount.atom_string()), Ok(amount));
        }
        assert_eq!(XmzAmount::from_atoms(u64::MAX).to_string(), "184467440.73709551615");
        assert_eq!("184467440.73709551616".parse::<XmzAmount>(), Err(AmountError::Overflow));
        assert!(XmzAmount::from_atom_string("18446744073709551616").is_err());
        assert!(XmzAmount::from_atom_string("01").is_err());
    }
    #[test]
    fn checked_arithmetic_does_not_saturate() {
        assert_eq!(XmzAmount::from_atoms(u64::MAX).checked_add(XmzAmount::from_atoms(1)), None);
        assert_eq!(XmzAmount::from_atoms(0).checked_sub(XmzAmount::from_atoms(1)), None);
        assert_eq!(XmzAmount::from_atoms(7).checked_sub(XmzAmount::from_atoms(2)).unwrap().atoms(), 5);
    }
}
