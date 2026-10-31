#!/bin/bash
# XMZ<->BNB swap on BNB testnet, hard-killed (SIGKILL to the runner and its wallet RPCs) the
# first time either trader reaches each stage and at random moments in between, then resumed
# from the journals.
#   bsc-testnet-killrun.sh <run-dir>        (MODE=resume KILLS=n to take over a run)
set -u
R=$1; ENGINE=/home/tripo/projects/Monzero-App/gui-wt-evm/dex-contracts/engine
BIN=$ENGINE/target/debug/examples/bsc_testnet; UNIT=dex-bsc-testnet-kill
LOG=$R.log; kills=${KILLS:-0}; mode=${MODE:-swap}
[ "$mode" = swap ] && : > "$LOG"
say() { echo "$(date -u +%H:%M:%S) SUPERVISOR $*" >> "$LOG"; }
start() {
  systemctl --user reset-failed $UNIT 2>/dev/null
  systemd-run --user --unit=$UNIT --working-directory=$ENGINE -E ACCOUNTS_BASE=2 -p StandardOutput=append:$LOG -p StandardError=append:$R.err "$BIN" $mode "$R" >/dev/null
  mode=resume
}
# Stages reached so far, as "owner -> Locked" (a resume reprints the current one: not new)
stages() { grep -vF SUPERVISOR "$LOG" | grep -oE '(owner|claimer) +-> [A-Za-z]+(\([A-Za-z]+\))?' | tr -s ' ' | sort -u; }
systemctl --user is-active -q $UNIT || start
mode=resume
seen=$(stages | wc -l)
next_random=$(( $(date +%s) + 120 + RANDOM % 180 ))
while :; do
  sleep 2
  if ! systemctl --user is-active -q $UNIT; then
    if [ -f "$R/summary.json" ]; then say "run finished after $kills kills"; exit 0; fi
    say "runner stopped by itself: $(systemctl --user show -p Result --value $UNIT)"; exit 1
  fi
  now_seen=$(stages | wc -l)
  reason=""
  if [ "$now_seen" -gt "$seen" ]; then
    reason="new stage: $(grep -vF SUPERVISOR "$LOG" | grep -E '(owner|claimer) +-> ' | tail -1 | cut -c10- | tr -s ' ')"; seen=$now_seen
  elif [ "$(date +%s)" -ge "$next_random" ] && [ -f "$R/run.json" ]; then
    reason="random"; next_random=$(( $(date +%s) + 120 + RANDOM % 180 ))
  fi
  if [ -n "$reason" ]; then
    # Random delay so some kills land between a saved state and its action
    sleep $(( RANDOM % 6 ))
    systemctl --user kill -s KILL $UNIT 2>/dev/null; kills=$((kills+1))
    for i in $(seq 1 20); do systemctl --user is-active -q $UNIT || break; sleep 0.5; done
    say "KILL #$kills ($reason)"
    sleep 3; start
    # The resumed runner reprints the current stages; give it time before counting again
    sleep 20; seen=$(stages | wc -l)
  fi
done
