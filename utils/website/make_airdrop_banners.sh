#!/usr/bin/env bash
# Builds website/airdrop/promo/: airdrop banners in the common social and web ad sizes, from
# the official artwork, plus thumbs/ for the moderator panel. Needs ImageMagick 7 and Noto Sans.
# Social formats keep a clear corner where the moderator panel stamps a personal referral QR.
# Run from the repository root:  bash utils/website/make_airdrop_banners.sh
set -euo pipefail
W=website
O=$W/airdrop/promo
rm -rf "$O"; mkdir -p "$O/thumbs"
community=$W/press/monzero-community-banner-20261026-1672.jpg
emblem=$W/press/monzero-emblem-20261026-512.png
coin=$W/press/monzero-xmz-logo-512.png
B=Noto-Sans-Bold; R=Noto-Sans-Regular; K=Noto-Sans-Black
INK='#edf3f7'; MINT='#a6f3f7'; CYAN='#3fdfe8'; GOLD='#ffd36b'
CAP=${AIRDROP_CAP:-25}; ENDS=${AIRDROP_ENDS:-31 Jan 2027}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

bg() {  # bg W H OUT: the landscape only (no lettering), darkened, with a soft cyan glow
  magick "$community" -gravity south -crop 100%x28%+0+0 +repage -resize "$1x$2^" -gravity center -extent "$1x$2" \
    -blur 0x10 -fill '#06121a' -colorize 60% \
    \( -size "$1x$2" radial-gradient:'rgba(63,223,232,0.28)-none' \) -gravity northeast -compose screen -composite "$3"
}
txt() {  # txt FONT SIZE COLOR WIDTH TEXT [gravity]: a transparent caption layer on stdout as MIFF
  magick -background none -fill "$3" -font "$1" -pointsize "$2" -size "${4}x" -interline-spacing 2 -gravity "${6:-west}" caption:"$5" miff:-
}
pill() {  # pill _ H TEXT SIZE OUT: rounded cyan badge sized to its text
  local tw w
  tw=$(magick -background none -font "$B" -pointsize "$4" label:"$3" -trim -format %w info:)
  w=$(( tw + $2 ))
  magick -size "${w}x${2}" xc:none -fill "$CYAN" -draw "roundrectangle 0,0,$(( w - 1 )),$(( $2 - 1 )),$(( $2 / 2 )),$(( $2 / 2 ))" \
    \( -background none -fill '#06121a' -font "$B" -pointsize "$4" label:"$3" -trim +repage \) -gravity center -composite "$5"
}
save() { magick "$1" -strip -quality 88 -interlace Plane "$O/$2.jpg"; }

# --- Landscape post 1600x900 (X, Telegram, Discord, Facebook). QR corner: bottom-right 300x300.
bg 1600 900 "$tmp/l.png"; pill 360 64 "COMMUNITY AIRDROP" 30 "$tmp/pill.png"
magick "$tmp/l.png" \
  \( "$emblem" -resize 96x96 \) -gravity northwest -geometry +90+80 -composite \
  -fill "$INK" -font "$B" -pointsize 40 -gravity northwest -annotate +206+104 'MONZERO' \
  \( "$coin" -resize 400x400 \) -gravity northeast -geometry +110+70 -composite \
  "$tmp/pill.png" -gravity northwest -geometry +90+250 -composite \
  \( <(txt "$K" 104 "$INK" 1000 "Earn up to $CAP XMZ") \) -gravity northwest -geometry +86+340 -composite \
  \( <(txt "$R" 38 "$MINT" 900 "Mine · Join the Telegram · Post on the forum · Share Monzero") \) -gravity northwest -geometry +92+500 -composite \
  -fill "$GOLD" -font "$B" -pointsize 34 -gravity southwest -annotate +92+150 "Ends $ENDS" \
  -fill "$CYAN" -font "$B" -pointsize 46 -gravity southwest -annotate +90+80 'monzero.org/airdrop' \
  "$tmp/post.png"; save "$tmp/post.png" airdrop-post-1600x900

# --- Square 1080x1080 (Instagram, Facebook, LinkedIn). QR corner: bottom-right 280x280.
bg 1080 1080 "$tmp/s.png"; pill 330 58 "COMMUNITY AIRDROP" 27 "$tmp/pill.png"
magick "$tmp/s.png" \
  \( "$emblem" -resize 84x84 \) -gravity northwest -geometry +70+70 -composite \
  -fill "$INK" -font "$B" -pointsize 34 -gravity northwest -annotate +168+92 'MONZERO' \
  \( "$coin" -resize 300x300 \) -gravity northeast -geometry +60+60 -composite \
  "$tmp/pill.png" -gravity northwest -geometry +70+330 -composite \
  \( <(txt "$K" 96 "$INK" 940 "Earn up to\n$CAP XMZ") \) -gravity northwest -geometry +66+410 -composite \
  \( <(txt "$R" 34 "$MINT" 600 "Mine · Join the Telegram · Post on the forum · Share") \) -gravity northwest -geometry +72+700 -composite \
  -fill "$GOLD" -font "$B" -pointsize 30 -gravity southwest -annotate +72+140 "Ends $ENDS" \
  -fill "$CYAN" -font "$B" -pointsize 40 -gravity southwest -annotate +70+76 'monzero.org/airdrop' \
  "$tmp/sq.png"; save "$tmp/sq.png" airdrop-square-1080x1080

# --- Story 1080x1920 (Instagram/TikTok/WhatsApp status). QR slot: centred, y 1300-1660.
bg 1080 1920 "$tmp/v.png"; pill 380 66 "COMMUNITY AIRDROP" 31 "$tmp/pill.png"
magick "$tmp/v.png" \
  \( "$coin" -resize 440x440 \) -gravity north -geometry +0+170 -composite \
  "$tmp/pill.png" -gravity north -geometry +0+680 -composite \
  \( <(txt "$K" 112 "$INK" 960 "Earn up to\n$CAP XMZ" center) \) -gravity north -geometry +0+790 -composite \
  \( <(txt "$R" 40 "$MINT" 860 "Mine · Join the Telegram\nPost on the forum · Share" center) \) -gravity north -geometry +0+1120 -composite \
  -fill "$GOLD" -font "$B" -pointsize 38 -gravity south -annotate +0+190 "Ends $ENDS" \
  -fill "$CYAN" -font "$B" -pointsize 52 -gravity south -annotate +0+110 'monzero.org/airdrop' \
  "$tmp/st.png"; save "$tmp/st.png" airdrop-story-1080x1920

# --- Profile header 1500x500 (X/Twitter). Text kept right of the avatar overlap area.
bg 1500 500 "$tmp/h.png"
magick "$tmp/h.png" \
  \( "$coin" -resize 250x250 \) -gravity east -geometry +80+0 -composite \
  -fill "$CYAN" -font "$B" -pointsize 28 -gravity northwest -annotate +470+120 'MONZERO COMMUNITY AIRDROP' \
  -fill "$INK" -font "$K" -pointsize 70 -gravity northwest -annotate +466+165 "Earn up to $CAP XMZ" \
  -fill "$MINT" -font "$R" -pointsize 30 -gravity northwest -annotate +470+285 "Ends $ENDS · monzero.org/airdrop" \
  "$tmp/hd.png"; save "$tmp/hd.png" airdrop-x-header-1500x500

btn() {  # btn W H TEXT SIZE: rounded call-to-action button layer on stdout
  magick -size "${1}x${2}" xc:none -fill "$CYAN" -draw "roundrectangle 0,0,$(( $1 - 1 )),$(( $2 - 1 )),$(( $2 / 2 )),$(( $2 / 2 ))" \
    -fill '#06121a' -font "$B" -pointsize "$4" -gravity center -annotate +0+0 "$3" miff:-
}
# --- Web ad sizes (IAB): leaderboard 728x90, medium rectangle 300x250, skyscraper 160x600, billboard 970x250
bg 728 90 "$tmp/a.png"
magick "$tmp/a.png" \( "$coin" -resize 64x64 \) -gravity west -geometry +14+0 -composite \
  -fill "$INK" -font "$K" -pointsize 26 -gravity west -annotate +94-13 "Monzero airdrop · up to $CAP XMZ" \
  -fill "$MINT" -font "$R" -pointsize 16 -gravity west -annotate +96+20 "Mine, join the Telegram, share. Ends $ENDS." \
  \( <(btn 124 40 'Join now' 18) \) -gravity east -geometry +16+0 -composite "$tmp/lb.png"; save "$tmp/lb.png" airdrop-web-728x90
bg 300 250 "$tmp/r.png"
magick "$tmp/r.png" \( "$coin" -resize 66x66 \) -gravity north -geometry +0+14 -composite \
  -fill "$CYAN" -font "$B" -pointsize 13 -gravity north -annotate +0+88 'MONZERO COMMUNITY AIRDROP' \
  -fill "$INK" -font "$K" -pointsize 30 -gravity north -annotate +0+106 "Up to $CAP XMZ" \
  -fill "$MINT" -font "$R" -pointsize 14 -gravity north -annotate +0+150 "Mine · Telegram · Share · Ends $ENDS" \
  \( <(btn 170 38 'Join the airdrop' 16) \) -gravity south -geometry +0+16 -composite "$tmp/mr.png"; save "$tmp/mr.png" airdrop-web-300x250
bg 160 600 "$tmp/k.png"
magick "$tmp/k.png" \( "$coin" -resize 110x110 \) -gravity north -geometry +0+40 -composite \
  -fill "$CYAN" -font "$B" -pointsize 15 -gravity north -annotate +0+172 'MONZERO' \
  -fill "$INK" -font "$K" -pointsize 22 -gravity north -annotate +0+204 'Community' \
  -fill "$INK" -font "$K" -pointsize 22 -gravity north -annotate +0+232 'airdrop' \
  -fill "$GOLD" -font "$B" -pointsize 22 -gravity north -annotate +0+300 'Earn up to' \
  -fill "$GOLD" -font "$K" -pointsize 34 -gravity north -annotate +0+328 "$CAP XMZ" \
  -fill "$MINT" -font "$R" -pointsize 15 -gravity north -annotate +0+400 'Mine · Telegram' \
  -fill "$MINT" -font "$R" -pointsize 15 -gravity north -annotate +0+422 'Forum · Share' \
  -fill "$MINT" -font "$R" -pointsize 14 -gravity north -annotate +0+462 "Ends $ENDS" \
  \( <(btn 130 38 'Join now' 17) \) -gravity south -geometry +0+30 -composite "$tmp/sk.png"; save "$tmp/sk.png" airdrop-web-160x600
bg 970 250 "$tmp/b.png"
magick "$tmp/b.png" \( "$coin" -resize 180x180 \) -gravity west -geometry +40+0 -composite \
  -fill "$CYAN" -font "$B" -pointsize 20 -gravity northwest -annotate +250+46 'MONZERO COMMUNITY AIRDROP' \
  -fill "$INK" -font "$K" -pointsize 54 -gravity northwest -annotate +246+76 "Earn up to $CAP XMZ" \
  -fill "$MINT" -font "$R" -pointsize 21 -gravity northwest -annotate +250+164 "Mine · Join the Telegram · Share · Ends $ENDS" \
  \( <(btn 180 54 'Join now' 22) \) -gravity southeast -geometry +36+36 -composite "$tmp/bb.png"; save "$tmp/bb.png" airdrop-web-970x250

for f in "$O"/*.jpg; do magick "$f" -resize 520x520 -strip -quality 80 "$O/thumbs/$(basename "$f")"; done
ls -la "$O" | tail -n +2; du -sh "$O"
