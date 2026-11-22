#!/bin/bash
# Smoke test of a built VM image in QEMU (KVM + OVMF), as a user would set it up:
#   bash appliance/tests/vm-smoke.sh <image.qcow2>
# First start (setup code on the console), wrong code refused, setup with node + relay +
# relay shared over Tor + an SSH key, then: signed-in status page, node answering, relay
# answering from the "home network", onion address made, firewall per roles, a second setup
# refused, a change without the CSRF token refused, no session -> sign-in page.
set -uo pipefail
IMG=${1:?usage: vm-smoke.sh <image.qcow2>}
OVMF=${OVMF:-/usr/share/edk2/x64}
S=$(mktemp -d); trap 'kill $(cat $S/vm.pid 2>/dev/null) 2>/dev/null; rm -rf "$S"' EXIT
PASS=0; FAIL=0
ok() { echo "  ok   $1"; PASS=$((PASS+1)); }
no() { echo "  FAIL $1"; FAIL=$((FAIL+1)); }
check() { if eval "$2" >/dev/null 2>&1; then ok "$1"; else no "$1"; fi; }

cp "$IMG" "$S/disk.qcow2"; cp "$OVMF/OVMF_VARS.4m.fd" "$S/vars.fd"
qemu-system-x86_64 -enable-kvm -m 4096 -smp 2 -machine q35 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF/OVMF_CODE.4m.fd" -drive if=pflash,format=raw,file="$S/vars.fd" \
  -drive file="$S/disk.qcow2",if=virtio \
  -nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:28443-:8443,hostfwd=tcp:127.0.0.1:28022-:22,hostfwd=tcp:127.0.0.1:28790-:8790 \
  -display none -vga std -monitor unix:"$S/mon",server,nowait -serial file:"$S/serial.log" -daemonize -pidfile "$S/vm.pid" || exit 1
U=https://127.0.0.1:28443; J=$S/cookies
echo "== first start"
timeout 240 bash -c "until curl -sk -m 3 $U/ >/dev/null && tr -d '\r' < $S/serial.log | grep -aqE '[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}'; do sleep 5; done"
CODE=$(tr -d '\r' < "$S/serial.log" | grep -aoE '[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}' | tail -1)
check "setup code on the console" '[ -n "$CODE" ]'
check "console shows the box address" "tr -d '\r' < $S/serial.log | grep -aq 'https://[0-9]'"
check "hostname monzero-box" "tr -d '\r' < $S/serial.log | grep -aq 'monzero-box login'"
shot() { [ -n "${SCREENSHOT:-}" ] && python3 -c "import socket,sys,time; s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.sendall(('screendump '+sys.argv[2]+'\\n').encode()); time.sleep(2)" "$S/mon" "$1" && echo "  display saved to $1"; }
[ -n "${SCREENSHOT:-}" ] && { sleep 40; shot "${SCREENSHOT%.*}-setup.ppm"; }
check "setup page serves the Monzero artwork" "curl -sk -o $S/wordmark.png -w '%{http_code} %{content_type} %{size_download}' $U/brand/wordmark.png | grep -qE '^200 image/png [0-9]{4,}'"
check "setup page is branded" "curl -sk $U/ | grep -q 'brand/wordmark.png'"
echo "== language"
check "language is the first step (12 languages)" "[ \$(curl -sk $U/ | grep -o 'href=\"/?lang=' | wc -l) = 12 ] && curl -sk $U/ | grep -q '1 · Choose your language'"
check "page follows the browser's language" "curl -sk -H 'Accept-Language: fr-FR,fr;q=0.9' $U/ | grep -q 'Configurer ce boîtier'"
check "picked language wins (?lang=de)" "curl -sk -H 'Accept-Language: fr' '$U/?lang=de' | grep -q 'Diese Box einrichten'"
check "keyboard follows the language (ja -> jp)" "curl -sk '$U/?lang=ja' | grep -q 'option value=\"jp\" selected'"
check "errors in the picked language" "curl -sk $U/setup -d lang=es --data-urlencode code=AAAA-BBBB-CCCC -d pw=correcthorsebattery -d pw2=correcthorsebattery | grep -q 'no es correcto'"
echo "== setup"
check "browser form post accepted (real Origin)" "curl -sk -H 'Origin: $U' $U/setup --data-urlencode code=AAAA-BBBB-CCCC -d pw=correcthorsebattery -d pw2=correcthorsebattery | grep -q 'not right'"
check "browser form post accepted (Origin null, same-origin)" "curl -sk -H 'Origin: null' -H 'Sec-Fetch-Site: same-origin' $U/setup --data-urlencode code=AAAA-BBBB-CCCC -d pw=correcthorsebattery -d pw2=correcthorsebattery | grep -q 'not right'"
check "post from another site refused" "curl -sk -H 'Origin: https://evil.example' $U/setup --data-urlencode code=AAAA-BBBB-CCCC -d pw=x -d pw2=x | grep -q 'other site'"
check "Origin null without same-origin mark refused" "curl -sk -H 'Origin: null' -H 'Sec-Fetch-Site: cross-site' $U/setup --data-urlencode code=AAAA-BBBB-CCCC -d pw=x -d pw2=x | grep -q 'other site'"
check "wrong code refused" "curl -sk $U/setup --data-urlencode code=AAAA-BBBB-CCCC -d pw=correcthorsebattery -d pw2=correcthorsebattery | grep -q 'not right'"
ssh-keygen -q -t ed25519 -N '' -f "$S/key" -C smoke
code=$(curl -sk -c "$J" -b "$J" -o /dev/null -w '%{http_code}' $U/setup --data-urlencode code="$CODE" -d pw=correcthorsebattery -d pw2=correcthorsebattery -d role=node -d role=relay -d share=1 -d lang=en -d keyboard=gb --data-urlencode "sshkey=$(cat "$S/key.pub")")
check "setup accepted" '[ "$code" = 303 ]'
timeout 240 bash -c "until curl -sk -b $J $U/ | grep -q '\.onion'; do sleep 10; done"
PAGE=$(curl -sk -b "$J" $U/ | sed 's/<[^>]*>/ /g' | tr -s ' \n')
check "status page after sign-in" 'echo "$PAGE" | grep -q "Node running"'
check "relay running" 'echo "$PAGE" | grep -q "DEX relay running"'
check "node answering" 'echo "$PAGE" | grep -qE "Node: block [0-9]+"'
check "onion address made" 'echo "$PAGE" | grep -qE "[a-z2-7]{56}\.onion"'
check "relay answers the home network" "curl -s -m 5 http://127.0.0.1:28790/v1/info | grep -q chains"
echo "== guards"
check "second setup refused" "! curl -sk -o /dev/null -w '%{http_code}' $U/setup --data-urlencode code=$CODE -d pw=x -d pw2=x | grep -q 303"
check "change without CSRF token refused" "[ \$(curl -sk -b $J -o /dev/null -w '%{http_code}' $U/roles -d role=node) = 403 ]"
check "no session -> sign-in page" "curl -sk $U/ | grep -q 'Sign in'"
CSRF=$(curl -sk -b "$J" $U/ | grep -o 'name="csrf" value="[^"]*"' | head -1 | sed 's/.*value="//; s/"$//')
check "language change accepted" "[ \$(curl -sk -b $J -o /dev/null -w '%{http_code}' $U/language -d csrf=$CSRF -d lang=es -d keyboard=es) = 303 ]"
timeout 60 bash -c "until curl -sk -b $J $U/ | grep -q 'Estado'; do sleep 2; done"
check "status page now in Spanish" "curl -sk -b $J $U/ | grep -q '<html lang=\"es\"' && curl -sk -b $J $U/ | grep -q 'Funciones'"
check "sign-in page in Spanish" "curl -sk $U/ | grep -q 'Iniciar sesión'"
check "unknown language refused" "[ \$(curl -sk -b $J -o /dev/null -w '%{http_code}' $U/language -d csrf=$CSRF -d lang=xx -d keyboard=us) = 400 ]"
echo "== inside (SSH with the key from setup)"
SSHO=(-i "$S/key" -p 28022 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -o LogLevel=ERROR)
IN=$(ssh "${SSHO[@]}" monzero-admin@127.0.0.1 'sudo nft list ruleset; echo ===; systemctl is-active tor monzero-update.timer monzero-public-relay.timer; echo screen:$(systemctl is-active monzero-screen); sudo journalctl -u monzero-screen -o cat --no-pager | grep "^screen:" | tail -1; echo ===; ls /etc/ssh/ssh_host_ed25519_key; cat /etc/monzero/appliance.json; cat /etc/default/locale; locale -a; echo ===; sudo -n true && echo sudo-ok' 2>/dev/null)
check "SSH with the setup key" '[ -n "$IN" ]'
check "firewall: 6174 open, rate-limited" 'echo "$IN" | grep -q "tcp dport 6174 ct state new limit"'
check "firewall: 8790/8443 home network only" 'echo "$IN" | grep -qE "saddr @lan4 tcp dport \{.*8443.*8790"'
check "firewall: relay not open to the internet (Tor mode)" '! echo "$IN" | grep -qE "dport \{[^}]*8790[^}]*\} ct state new limit"'
check "Tor and timers running" '[ $(echo "$IN" | grep -c "^active") -ge 3 ]'
check "Monzero screen on the display" 'echo "$IN" | grep -q "^screen:active"'
check "screen shows the status after setup" 'echo "$IN" | grep -q "^screen: status"'
[ -n "${SCREENSHOT:-}" ] && { sleep 20; shot "$SCREENSHOT"; }
check "settings saved (publicRelay onion)" 'echo "$IN" | grep -q "\"publicRelay\": \"onion\""'
check "language saved (es, keyboard es)" 'echo "$IN" | grep -q "\"language\": \"es\"" && echo "$IN" | grep -q "\"keyboard\": \"es\""'
check "system locale follows (es_ES.UTF-8)" 'echo "$IN" | grep -q "^LANG=es_ES.UTF-8"'
check "all 12 locales installed" '[ $(echo "$IN" | grep -ciE "^(en_US|es_ES|fr_FR|de_DE|it_IT|pt_BR|nl_NL|pl_PL|tr_TR|ru_RU|zh_CN|ja_JP)\.utf-?8$") = 12 ]'
echo
echo "passed $PASS, failed $FAIL"
[ "$FAIL" = 0 ]
