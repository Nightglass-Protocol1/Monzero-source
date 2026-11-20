//! Both sides' drivers against two simulated swap contracts that enforce the contract's rules
//! (who may call, time limits, secrets), with a shared clock.

use super::*;
use crate::direct::{DirectOffer, DirectSession, DirectTiming, Leg};
use crate::direct_machine::DirectOutcome;
use crate::driver::Funds;
use crate::{secret_matches, Chain, ObservedLock, Revealed, Stage, XmzSwap};
use alloy::primitives::{keccak256, Bytes};
use alloy::rpc::types::Log;
use std::cell::RefCell;
use std::rc::Rc;

struct Contract {
    address: Address,
    swaps: BTreeMap<B256, (Swap, Stage, u64)>,
    revealed: BTreeMap<B256, Revealed>,
    balances: BTreeMap<Address, U256>,
}

struct World {
    now: u64,
    block: u64,
    contracts: [Contract; 2],
    /// Signed calls: (contract index, sender, call)
    calls: Vec<(usize, Address, EvmCall)>,
    applied: std::collections::BTreeSet<usize>,
}

type Shared = Rc<RefCell<World>>;

#[derive(Clone)]
struct FakeChain {
    world: Shared,
    leg: usize,
    me: Address,
}

fn chain_err(e: &str) -> DriverError {
    DriverError::Chain(e.into())
}

impl World {
    fn apply(&mut self, index: usize) -> Result<(), DriverError> {
        if !self.applied.insert(index) {
            return Ok(());
        }
        let (leg, from, call) = self.calls[index].clone();
        let now = self.now;
        let c = &mut self.contracts[leg];
        match call {
            EvmCall::NewSwap { terms, nonce } => {
                let swap = Swap {
                    owner: from,
                    claimer: terms.claimer,
                    claimCommitment: terms.claim_commitment,
                    refundCommitment: terms.refund_commitment,
                    timeout1: U256::from(now + terms.duration1),
                    timeout2: U256::from(now + terms.duration1 + terms.duration2),
                    asset: terms.asset,
                    value: terms.value,
                    nonce,
                };
                let bal = c.balances.entry(from).or_default();
                *bal = bal.checked_sub(terms.value).ok_or_else(|| chain_err("insufficient funds"))?;
                c.swaps.insert(swap_id(&swap), (swap, Stage::Pending, now));
            }
            EvmCall::SetReady(swap) => {
                let entry = c.swaps.get_mut(&swap_id(&swap)).ok_or_else(|| chain_err("no swap"))?;
                if entry.1 != Stage::Pending || from != swap.owner || U256::from(now) >= swap.timeout1 {
                    return Err(chain_err("setReady refused"));
                }
                entry.1 = Stage::Ready;
            }
            EvmCall::Claim(swap, secret) => {
                let id = swap_id(&swap);
                let entry = c.swaps.get_mut(&id).ok_or_else(|| chain_err("no swap"))?;
                let ok = from == swap.claimer
                    && matches!(entry.1, Stage::Pending | Stage::Ready)
                    && (entry.1 == Stage::Ready || U256::from(now) >= swap.timeout1)
                    && U256::from(now) < swap.timeout2
                    && secret_matches(&secret, swap.claimCommitment);
                if !ok {
                    return Err(chain_err("claim refused"));
                }
                entry.1 = Stage::Completed;
                *c.balances.entry(swap.claimer).or_default() += swap.value;
                c.revealed.insert(id, Revealed::Claimed(secret));
            }
            EvmCall::Refund(swap, secret) => {
                let id = swap_id(&swap);
                let entry = c.swaps.get_mut(&id).ok_or_else(|| chain_err("no swap"))?;
                let early = entry.1 == Stage::Pending && U256::from(now) < swap.timeout1;
                let ok = from == swap.owner
                    && matches!(entry.1, Stage::Pending | Stage::Ready)
                    && (early || U256::from(now) >= swap.timeout2)
                    && secret_matches(&secret, swap.refundCommitment);
                if !ok {
                    return Err(chain_err("refund refused"));
                }
                entry.1 = Stage::Completed;
                *c.balances.entry(swap.owner).or_default() += swap.value;
                c.revealed.insert(id, Revealed::Refunded(secret));
            }
            _ => return Err(chain_err("not used here")),
        }
        Ok(())
    }

    /// Would the call succeed now (the gas estimate)? Runs it on a copy.
    fn would_succeed(&self, leg: usize, from: Address, call: &EvmCall) -> bool {
        let mut copy = World {
            now: self.now,
            block: self.block,
            contracts: [self.contracts[0].clone_state(), self.contracts[1].clone_state()],
            calls: vec![(leg, from, call.clone())],
            applied: Default::default(),
        };
        copy.apply(0).is_ok()
    }
}

impl Contract {
    fn new(address: Address) -> Self {
        Contract { address, swaps: BTreeMap::new(), revealed: BTreeMap::new(), balances: BTreeMap::new() }
    }

    fn clone_state(&self) -> Self {
        Contract { address: self.address, swaps: self.swaps.clone(), revealed: self.revealed.clone(), balances: self.balances.clone() }
    }
}

impl EvmChain for FakeChain {
    async fn now(&self) -> Result<u64, DriverError> {
        Ok(self.world.borrow().now)
    }

    async fn find_lock(&self, terms: &AgreedTerms) -> Result<Option<ObservedLock>, DriverError> {
        let w = self.world.borrow();
        let c = &w.contracts[self.leg];
        Ok(c.swaps.values().find(|(s, _, _)| s.owner == terms.owner && s.claimer == terms.claimer && s.claimCommitment == terms.claim_commitment && s.refundCommitment == terms.refund_commitment).map(|(s, stage, at)| {
            let data = XmzSwap::New {
                swapId: swap_id(s),
                owner: s.owner,
                claimer: s.claimer,
                claimCommitment: s.claimCommitment,
                refundCommitment: s.refundCommitment,
                timeout1: s.timeout1,
                timeout2: s.timeout2,
                asset: s.asset,
                value: s.value,
                nonce: s.nonce,
            };
            ObservedLock {
                chain_id: Chain::Local.id(),
                log: Log {
                    inner: alloy::primitives::Log { address: c.address, data },
                    block_hash: None,
                    block_number: None,
                    block_timestamp: None,
                    transaction_hash: None,
                    transaction_index: None,
                    log_index: None,
                    removed: false,
                },
                block_timestamp: *at,
                stage: *stage as u8,
            }
        }))
    }

    async fn stage(&self, id: B256) -> Result<Stage, DriverError> {
        Ok(self.world.borrow().contracts[self.leg].swaps.get(&id).map(|e| e.1).unwrap_or(Stage::Invalid))
    }

    async fn revealed(&self, swap: &Swap) -> Result<Option<Revealed>, DriverError> {
        Ok(self.world.borrow().contracts[self.leg].revealed.get(&swap_id(swap)).copied())
    }

    async fn head(&self) -> Result<u64, DriverError> {
        Ok(self.world.borrow().block)
    }

    async fn sign(&self, call: &EvmCall) -> Result<Bytes, DriverError> {
        let mut w = self.world.borrow_mut();
        if !w.would_succeed(self.leg, self.me, call) {
            return Err(chain_err("the call would revert"));
        }
        w.calls.push((self.leg, self.me, call.clone()));
        Ok(Bytes::from((w.calls.len() - 1).to_be_bytes().to_vec()))
    }

    async fn replacement(&self, _raw: &Bytes) -> Result<Option<Bytes>, DriverError> {
        Ok(None)
    }

    async fn broadcast(&self, raw: &Bytes) -> Result<(), DriverError> {
        let index = usize::from_be_bytes(raw.as_ref().try_into().map_err(|_| chain_err("raw"))?);
        // A transaction that fails when mined just stays failed (as a reverted transaction would)
        let _ = self.world.borrow_mut().apply(index);
        Ok(())
    }

    async fn funds(&self) -> Result<Funds, DriverError> {
        let w = self.world.borrow();
        Ok(Funds { balance: w.contracts[self.leg].balances.get(&self.me).copied().unwrap_or_default(), fee_per_gas: U256::ZERO })
    }
}

fn account(byte: u8) -> PrivateKeySigner {
    PrivateKeySigner::from_bytes(&B256::repeat_byte(byte)).unwrap()
}

const START: u64 = 1_700_000_000;
const VALUE_A: u64 = 1_000;
const VALUE_B: u64 = 3_000;

struct Setup {
    world: Shared,
    leader: DirectDriver<FakeChain, FakeChain, MemoryDirectJournal>,
    follower: DirectDriver<FakeChain, FakeChain, MemoryDirectJournal>,
    transcript: B256,
}

fn setup() -> Setup {
    let (la, fa) = (account(1), account(2));
    let offer = DirectOffer {
        trade_id: keccak256(b"driver test"),
        leader: la.address(),
        follower: fa.address(),
        leg_a: Leg { chain_id: Chain::Local.id(), contract: Address::repeat_byte(0xa0), asset: Address::ZERO, value: U256::from(VALUE_A), duration1: 3600, duration2: 8 * 3600 },
        leg_b: Leg { chain_id: Chain::Local.id(), contract: Address::repeat_byte(0xb0), asset: Address::ZERO, value: U256::from(VALUE_B), duration1: 3600, duration2: 4 * 3600 },
        expires_at: START + 600,
    };
    let (ls, fs) = (SwapSeed::from_wallet_seed(b"leader"), SwapSeed::from_wallet_seed(b"follower"));
    let mut lsess = DirectSession::start(&ls, offer.clone(), Side::Leader, &la, START).unwrap();
    let mut fsess = DirectSession::start(&fs, offer.clone(), Side::Follower, &fa, START).unwrap();
    fsess.accept_theirs(&lsess.frame().clone(), START).unwrap();
    lsess.accept_theirs(&fsess.frame().clone(), START).unwrap();
    let deadline = START + 2 * 3600;
    let (lsaved, fsaved) = (lsess.saved(DirectTiming::DEFAULT, deadline, 0, 0).unwrap(), fsess.saved(DirectTiming::DEFAULT, deadline, 0, 0).unwrap());
    let transcript = lsaved.transcript;

    let mut world = World { now: START, block: 1, contracts: [Contract::new(offer.leg_a.contract), Contract::new(offer.leg_b.contract)], calls: vec![], applied: Default::default() };
    world.contracts[0].balances.insert(la.address(), U256::from(VALUE_A));
    world.contracts[1].balances.insert(fa.address(), U256::from(VALUE_B));
    let world = Rc::new(RefCell::new(world));
    let chain = |leg, me| FakeChain { world: world.clone(), leg, me };
    let leader = DirectDriver::start(lsaved, &ls, &la, chain(0, la.address()), chain(1, la.address()), MemoryDirectJournal(None)).unwrap();
    let follower = DirectDriver::start(fsaved, &fs, &fa, chain(0, fa.address()), chain(1, fa.address()), MemoryDirectJournal(None)).unwrap();
    Setup { world, leader, follower, transcript }
}

fn advance(world: &Shared, seconds: u64) {
    let mut w = world.borrow_mut();
    w.now += seconds;
    w.block += 1;
}

fn balance(world: &Shared, leg: usize, who: u8) -> u64 {
    world.borrow().contracts[leg].balances.get(&account(who).address()).copied().unwrap_or_default().to::<u64>()
}

async fn run(s: &mut Setup, leader_online: impl Fn(u64) -> bool, follower_online: impl Fn(u64) -> bool, until: u64) -> (DirectRole, DirectRole) {
    s.leader.approve(s.transcript).unwrap();
    s.follower.approve(s.transcript).unwrap();
    let (mut l, mut f) = (s.leader.journal().load().unwrap().state, s.follower.journal().load().unwrap().state);
    while s.world.borrow().now < until && !(l.is_done() && f.is_done()) {
        let now = s.world.borrow().now;
        if leader_online(now) && !l.is_done() {
            l = s.leader.tick().await.unwrap();
        }
        if follower_online(now) && !f.is_done() {
            f = s.follower.tick().await.unwrap();
        }
        advance(&s.world, 120);
    }
    (l, f)
}

#[tokio::test]
async fn honest_trade_swaps_both_coins() {
    let mut s = setup();
    let (l, f) = run(&mut s, |_| true, |_| true, START + 24 * 3600).await;
    assert_eq!(l, DirectRole::Leader(LeaderState::Done(DirectOutcome::Swapped)));
    assert_eq!(f, DirectRole::Follower(FollowerState::Done(DirectOutcome::Swapped)));
    assert_eq!((balance(&s.world, 0, 1), balance(&s.world, 0, 2)), (0, VALUE_A), "A went to the follower");
    assert_eq!((balance(&s.world, 1, 1), balance(&s.world, 1, 2)), (VALUE_B, 0), "B went to the leader");
    // Every transaction was signed once
    for record in [s.leader.journal().load().unwrap(), s.follower.journal().load().unwrap()] {
        assert!(record.retained.values().all(|v| v.len() == 1));
    }
}

#[tokio::test]
async fn nothing_is_locked_before_approval() {
    let mut s = setup();
    assert!(matches!(s.leader.tick().await, Err(DirectError::Driver(DriverError::AwaitingApproval))));
    assert!(s.world.borrow().calls.is_empty());
}

#[tokio::test]
async fn a_leader_without_the_coin_is_asked_to_fund_its_account_first() {
    let mut s = setup();
    s.world.borrow_mut().contracts[0].balances.clear();
    s.leader.approve(s.transcript).unwrap();
    match s.leader.tick().await {
        Err(DirectError::AwaitingFunding { chain_id, needed, have, .. }) => {
            assert_eq!((chain_id, needed, have), (Chain::Local.id(), U256::from(VALUE_A), U256::ZERO));
        }
        other => panic!("{other:?}"),
    }
    assert!(s.world.borrow().calls.is_empty());
}

#[tokio::test]
async fn a_follower_that_never_comes_costs_the_leader_time_not_coin() {
    let mut s = setup();
    let (l, _) = run(&mut s, |_| true, |_| false, START + 24 * 3600).await;
    assert_eq!(l, DirectRole::Leader(LeaderState::Done(DirectOutcome::Refunded)));
    assert_eq!(balance(&s.world, 0, 1), VALUE_A);
    // ...and not before A's second time limit (1 h + 8 h after the lock)
    assert!(s.world.borrow().now >= START + 9 * 3600);
}

#[tokio::test]
async fn a_leader_that_vanishes_after_the_follower_locked_leaves_both_refunded() {
    let mut s = setup();
    // The leader is offline from just after locking A and making it Ready (two rounds) until
    // well after both limits: it never sees the follower's lock of B
    let back = START + 12 * 3600;
    let (l, f) = run(&mut s, |now| now < START + 240 || now >= back, |_| true, START + 24 * 3600).await;
    assert_eq!(f, DirectRole::Follower(FollowerState::Done(DirectOutcome::Refunded)));
    assert_eq!(l, DirectRole::Leader(LeaderState::Done(DirectOutcome::Refunded)));
    assert_eq!((balance(&s.world, 0, 1), balance(&s.world, 1, 2)), (VALUE_A, VALUE_B));
}

#[tokio::test]
async fn a_restart_at_every_step_resumes_without_signing_anything_twice() {
    let mut s = setup();
    s.leader.approve(s.transcript).unwrap();
    s.follower.approve(s.transcript).unwrap();
    let (ls, fs) = (SwapSeed::from_wallet_seed(b"leader"), SwapSeed::from_wallet_seed(b"follower"));
    for _ in 0..400 {
        // Rebuild both drivers from their journals before every round
        let (lj, fj) = (s.leader.journal().clone(), s.follower.journal().clone());
        let chain = |leg, me: u8| FakeChain { world: s.world.clone(), leg, me: account(me).address() };
        s.leader = DirectDriver::resume(&ls, &account(1), chain(0, 1), chain(1, 1), lj).unwrap();
        s.follower = DirectDriver::resume(&fs, &account(2), chain(0, 2), chain(1, 2), fj).unwrap();
        let l = s.leader.tick().await.unwrap();
        let f = s.follower.tick().await.unwrap();
        if l.is_done() && f.is_done() {
            break;
        }
        advance(&s.world, 120);
    }
    assert_eq!(s.leader.journal().load().unwrap().state.stage(), "Done(Swapped)");
    assert_eq!(s.follower.journal().load().unwrap().state.stage(), "Done(Swapped)");
    assert_eq!(s.world.borrow().calls.len(), 6, "lock A, ready A, lock B, ready B, claim B, claim A: each once");
}
