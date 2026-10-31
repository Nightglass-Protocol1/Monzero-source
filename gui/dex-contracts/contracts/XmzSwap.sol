// SPDX-License-Identifier: BSD-3-Clause
pragma solidity 0.8.28;

/// @title XMZ <-> EVM asset atomic swap
/// @notice One side of an atomic swap between XMZ and native coin or an allowlisted ERC-20 /
/// BEP-20 token. See DEX_XMZ_EVM_SWAP_DESIGN.md. The XMZ side is an output sent to the shared
/// key s_owner + s_claimer; this contract makes each side reveal its secret at the right time:
///  - the claimer (XMZ seller) reveals s_claimer to claim the funds, letting the owner sweep the XMZ;
///  - the owner (EVM side) reveals s_owner to refund, letting the claimer sweep the XMZ back.
/// No owner, admin, upgrade or pause: a new version is a new deployment.
/// @dev A secret s is committed to as the Ethereum address of s*G on secp256k1. A cross-group
/// DLEQ proof (checked off-chain by both apps) binds it to the ed25519 key share on the XMZ side.
contract XmzSwap {
    enum Stage {
        INVALID,
        PENDING,
        READY,
        COMPLETED
    }

    struct Swap {
        address payable owner; // EVM-side trader: locks the funds, can refund
        address payable claimer; // XMZ-side trader's EVM address: receives the funds
        address claimCommitment; // address of s_claimer * G
        address refundCommitment; // address of s_owner * G
        uint256 timeout1; // before: owner may setReady or refund
        uint256 timeout2; // from: claimer may no longer claim, owner may refund
        address asset; // address(0) = native coin
        uint256 value;
        uint256 nonce;
    }

    // secp256k1 group order and generator x coordinate; G.y is even, so recovery id 27
    uint256 private constant N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141;
    uint256 private constant GX = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798;
    uint256 private constant HALF_N = N / 2;

    mapping(bytes32 => Stage) public swaps;

    event New(
        bytes32 indexed swapId,
        address indexed owner,
        address indexed claimer,
        address claimCommitment,
        address refundCommitment,
        uint256 timeout1,
        uint256 timeout2,
        address asset,
        uint256 value,
        uint256 nonce
    );
    event Ready(bytes32 indexed swapId);
    /// The secret is in the event so watchers do not depend on reading calldata.
    event Claimed(bytes32 indexed swapId, bytes32 secret);
    event Refunded(bytes32 indexed swapId, bytes32 secret);

    error ZeroValue();
    error ZeroAddress();
    error ZeroDuration();
    error WrongNativeValue();
    error FeeOnTransferToken();
    error SwapExists();
    error NotOwner();
    error NotClaimer();
    error WrongStage();
    error TooLate();
    error TooEarly();
    error NotTimeToRefund();
    error InvalidSecret();
    error InvalidRelaySignature();
    error FeeTooHigh();
    error TransferFailed();

    function swapId(Swap memory swap) public pure returns (bytes32) {
        return keccak256(abi.encode(swap));
    }

    /// @notice Lock `value` of `asset` for `claimer`. Durations are in seconds.
    function newSwap(
        address payable claimer,
        address claimCommitment,
        address refundCommitment,
        uint256 duration1,
        uint256 duration2,
        address asset,
        uint256 value,
        uint256 nonce
    ) external payable returns (bytes32 id) {
        if (value == 0) revert ZeroValue();
        if (claimer == address(0) || claimCommitment == address(0) || refundCommitment == address(0)) {
            revert ZeroAddress();
        }
        if (duration1 == 0 || duration2 == 0) revert ZeroDuration();

        Swap memory swap = Swap({
            owner: payable(msg.sender),
            claimer: claimer,
            claimCommitment: claimCommitment,
            refundCommitment: refundCommitment,
            timeout1: block.timestamp + duration1,
            timeout2: block.timestamp + duration1 + duration2,
            asset: asset,
            value: value,
            nonce: nonce
        });
        id = swapId(swap);
        if (swaps[id] != Stage.INVALID) revert SwapExists();
        swaps[id] = Stage.PENDING;

        if (asset == address(0)) {
            if (msg.value != value) revert WrongNativeValue();
        } else {
            if (msg.value != 0) revert WrongNativeValue();
            // Reject fee-on-transfer tokens: the contract must hold exactly `value` for this swap,
            // or a claimer could reveal their secret and receive nothing.
            uint256 before = _balanceOf(asset);
            _call(asset, abi.encodeWithSelector(0x23b872dd, msg.sender, address(this), value)); // transferFrom
            if (_balanceOf(asset) - before != value) revert FeeOnTransferToken();
        }

        emit New(id, msg.sender, claimer, claimCommitment, refundCommitment, swap.timeout1, swap.timeout2, asset, value, nonce);
    }

    /// @notice The owner confirms the XMZ lock. Only before timeout1.
    function setReady(Swap calldata swap) external {
        bytes32 id = swapId(swap);
        if (swaps[id] != Stage.PENDING) revert WrongStage();
        if (msg.sender != swap.owner) revert NotOwner();
        if (block.timestamp >= swap.timeout1) revert TooLate();
        swaps[id] = Stage.READY;
        emit Ready(id);
    }

    /// @notice The claimer reveals s_claimer and receives the funds.
    function claim(Swap calldata swap, bytes32 secret) external {
        if (msg.sender != swap.claimer) revert NotClaimer();
        bytes32 id = _checkClaim(swap, secret);
        _send(swap.asset, swap.claimer, swap.value);
        emit Claimed(id, secret);
    }

    /// @notice Anyone can submit the claimer's signed claim and receive `fee` for the gas.
    /// The claimer signs (chain, contract, swap id, fee, relayer) with an Ethereum signed message;
    /// naming the relayer stops a front-runner from taking the fee.
    function claimRelayer(Swap calldata swap, bytes32 secret, uint256 fee, uint8 v, bytes32 r, bytes32 s) external {
        if (fee > swap.value) revert FeeTooHigh();
        bytes32 id = swapId(swap);
        bytes32 digest = keccak256(
            abi.encodePacked(
                "\x19Ethereum Signed Message:\n32",
                keccak256(abi.encode("XMZ-SWAP-RELAY-V1", block.chainid, address(this), id, fee, msg.sender))
            )
        );
        if (uint256(s) > HALF_N) revert InvalidRelaySignature();
        address signer = ecrecover(digest, v, r, s);
        if (signer == address(0) || signer != swap.claimer) revert InvalidRelaySignature();

        _checkClaim(swap, secret);
        _send(swap.asset, swap.claimer, swap.value - fee);
        if (fee > 0) _send(swap.asset, payable(msg.sender), fee);
        emit Claimed(id, secret);
    }

    /// @notice The owner reveals s_owner and takes the funds back: before timeout1 while not
    /// READY, or from timeout2 on.
    function refund(Swap calldata swap, bytes32 secret) external {
        if (msg.sender != swap.owner) revert NotOwner();
        bytes32 id = swapId(swap);
        Stage stage = swaps[id];
        if (stage != Stage.PENDING && stage != Stage.READY) revert WrongStage();
        bool early = block.timestamp < swap.timeout1 && stage == Stage.PENDING;
        if (!early && block.timestamp < swap.timeout2) revert NotTimeToRefund();
        if (!secretMatches(secret, swap.refundCommitment)) revert InvalidSecret();

        swaps[id] = Stage.COMPLETED;
        _send(swap.asset, swap.owner, swap.value);
        emit Refunded(id, secret);
    }

    /// @notice True when `commitment` is the address of secret * G on secp256k1.
    /// ecrecover(0, 27, Gx, secret * Gx mod N) returns the address of Gx^-1 * (secret * Gx) * G.
    function secretMatches(bytes32 secret, address commitment) public pure returns (bool) {
        uint256 k = uint256(secret);
        if (k == 0 || k >= N) return false;
        address signer = ecrecover(bytes32(0), 27, bytes32(GX), bytes32(mulmod(k, GX, N)));
        return signer != address(0) && signer == commitment;
    }

    function _checkClaim(Swap calldata swap, bytes32 secret) private returns (bytes32 id) {
        id = swapId(swap);
        Stage stage = swaps[id];
        if (stage != Stage.PENDING && stage != Stage.READY) revert WrongStage();
        if (block.timestamp < swap.timeout1 && stage != Stage.READY) revert TooEarly();
        if (block.timestamp >= swap.timeout2) revert TooLate();
        if (!secretMatches(secret, swap.claimCommitment)) revert InvalidSecret();
        swaps[id] = Stage.COMPLETED;
    }

    function _send(address asset, address payable to, uint256 amount) private {
        if (asset == address(0)) {
            (bool ok,) = to.call{value: amount}("");
            if (!ok) revert TransferFailed();
        } else {
            _call(asset, abi.encodeWithSelector(0xa9059cbb, to, amount)); // transfer
        }
    }

    /// Token call that accepts both bool-returning and non-returning (USDT-style) tokens.
    function _call(address token, bytes memory data) private {
        (bool ok, bytes memory ret) = token.call(data);
        if (!ok || (ret.length != 0 && !abi.decode(ret, (bool))) || token.code.length == 0) {
            revert TransferFailed();
        }
    }

    function _balanceOf(address token) private view returns (uint256) {
        (bool ok, bytes memory ret) = token.staticcall(abi.encodeWithSelector(0x70a08231, address(this)));
        if (!ok || ret.length < 32) revert TransferFailed();
        return abi.decode(ret, (uint256));
    }
}
