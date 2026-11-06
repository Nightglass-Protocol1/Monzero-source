#!/usr/bin/env bash
# Builds website/marketing/graphics/ (platform-sized images and quote cards) from the
# official artwork, plus monzero-marketing-graphics.zip. Needs ImageMagick 7 and zip
# (run from the repository root; the ZIP is written with Python): bash utils/website/make_marketing_graphics.sh
set -euo pipefail
W=website
O=$W/marketing/graphics
rm -rf "$O"; mkdir -p "$O"
banner=$W/press/monzero-banner-20261026-2172.jpg          # 3:1, logo + "private · open · community driven"
community=$W/press/monzero-community-banner-20261026-1672.jpg  # 16:9, "Join the Monzero community"
emblem=$W/press/monzero-emblem-20261026-512.png           # transparent MZ emblem
emblem_dark=$W/press/monzero-emblem-dark-20261026-1254.jpg
coin_dark=$W/press/monzero-xmz-logo-dark-1024.jpg
coin=$W/press/monzero-xmz-logo-512.png                    # transparent coin
font_bold=Noto-Sans-Bold
font=Noto-Sans-Regular
jpg() { local out="${@: -1}"; magick "${@:1:$#-1}" -strip -quality 86 -interlace Plane "$out"; }  # jpg INPUT... OUTPUT

# --- Platform sizes from the official banners
jpg "$banner" -resize 1500x500^ -gravity center -extent 1500x500 "$O/x-header-1500x500.jpg"
jpg "$community" -resize 1600x900^ -gravity center -extent 1600x900 "$O/post-community-1600x900.jpg"
jpg "$community" -resize 1200x675^ -gravity center -extent 1200x675 "$O/post-community-1200x675.jpg"
jpg "$community" -resize 960x540^ -gravity center -extent 960x540 "$O/discord-banner-960x540.jpg"
jpg "$community" -resize 1920x1080^ -gravity center -extent 1920x1080 "$O/discord-splash-1920x1080.jpg"
jpg "$banner" -resize 1500x500^ -gravity center -extent 1500x500 -resize 1584x528 -gravity center -background '#0b1720' -extent 1584x396 "$O/linkedin-banner-1584x396.jpg"
cp "$W/assets/monzero-share-20261026-1200x630.jpg" "$O/link-preview-1200x630.jpg"
# YouTube channel art: the banner over a soft, darkened copy of itself (logo stays in the safe area)
magick "$banner" -resize 2560x1440^ -gravity center -extent 2560x1440 -blur 0x24 -fill '#0b1720' -colorize 45% \
  \( "$banner" -resize 2560x853 \) -gravity center -composite -strip -quality 86 "$O/youtube-banner-2560x1440.jpg"

# --- Profile pictures and squares
jpg "$coin_dark" -resize 400x400 "$O/avatar-coin-400.jpg"
jpg "$coin_dark" -resize 1000x1000 "$O/avatar-coin-1000.jpg"
jpg "$emblem_dark" -resize 400x400 "$O/avatar-emblem-400.jpg"
jpg "$emblem_dark" -resize 1080x1080 "$O/square-emblem-1080.jpg"
magick "$coin" -resize 512x512 "$O/coin-transparent-512.png"
magick "$emblem" -resize 512x512 "$O/emblem-transparent-512.png"

# --- Card background: the community artwork, darkened and softened so text reads
bg() {  # bg WIDTH HEIGHT OUT
  # Only the lower landscape (no lettering), so no ghost text shows through.
  magick "$community" -gravity south -crop 100%x28%+0+0 +repage -resize "$1x$2^" -gravity center -extent "$1x$2" \
    -blur 0x10 -fill '#06121a' -colorize 58% "$3"
}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# Quote cards 1600x900: emblem left, message right
card() {  # card NAME HEADLINE SUBLINE
  bg 1600 900 "$tmp/bg.png"
  magick "$tmp/bg.png" \( "$emblem" -resize 420x420 \) -gravity west -geometry +110+0 -composite \
    \( -background none -fill '#edf3f7' -font "$font_bold" -size 860x -pointsize 72 -interline-spacing 4 caption:"$2" \) \
      -gravity northwest -geometry +620+230 -composite \
    \( -background none -fill '#a6f3f7' -font "$font" -size 860x -pointsize 36 -interline-spacing 6 caption:"$3" \) \
      -gravity northwest -geometry +620+520 -composite \
    -fill '#3fdfe8' -font "$font_bold" -pointsize 34 -gravity southeast -annotate +80+60 'monzero.org' \
    -strip -quality 88 "$O/card-$1-1600x900.jpg"
}
card privacy "Private by design.\nOpen by nature." "Amounts, senders and receivers hidden by default. Open source, signed releases."
card mining "Mine XMZ with the CPU you already have." "RandomX proof of work. Pool with hourly payouts, solo mining, even an Android miner."
card fair "No premine. No ICO.\nNo token sale." "Every XMZ was mined. The founder's early mining is disclosed publicly."
card dex "Swap without handing your coins to anyone." "Atomic swaps: both sides get paid, or both get refunded. No custodian, no bridge."
card wallet "Your keys never leave your device." "Desktop, Android and web wallets. wallet.monzero.org"
card scam "Never share your recovery phrase." "Admins never DM first. There is no live airdrop and no Monzero token on other chains."

# Story / vertical 1080x1920
bg 1080 1920 "$tmp/story.png"
magick "$tmp/story.png" \( "$coin" -resize 620x620 \) -gravity north -geometry +0+330 -composite \
  \( -background none -fill '#edf3f7' -font "$font_bold" -size 900x -pointsize 84 -gravity center caption:"Private by design.\nOpen by nature." \) \
    -gravity north -geometry +0+1050 -composite \
  \( -background none -fill '#a6f3f7' -font "$font" -size 860x -pointsize 40 -gravity center caption:"Private, mined, community-run digital cash" \) \
    -gravity north -geometry +0+1340 -composite \
  -fill '#3fdfe8' -font "$font_bold" -pointsize 48 -gravity south -annotate +0+170 'monzero.org' \
  -strip -quality 88 "$O/story-1080x1920.jpg"

# Thumbnails for the kit page
mkdir -p "$O/thumbs"
for f in "$O"/*.jpg "$O"/*.png; do
  name=$(basename "${f%.*}")
  magick "$f" -resize 480x480 -background '#10242d' -flatten -strip -quality 78 "$O/thumbs/$name.jpg"
done

# One download with everything: these graphics, the press kit logos/banners and the app screenshots
zip_dir="$tmp/monzero-marketing-graphics"
mkdir -p "$zip_dir/social" "$zip_dir/logos-and-banners" "$zip_dir/app-screenshots"
cp "$O"/*.jpg "$O"/*.png "$zip_dir/social/"
cp "$W"/press/*.png "$W"/press/*.jpg "$zip_dir/logos-and-banners/"
cp "$W"/marketing/screens/monzero-wallet-?-*[!b].jpg "$zip_dir/app-screenshots/"
cat > "$zip_dir/README.txt" <<'EOF'
Monzero (XMZ) marketing graphics
Official artwork for posts, profiles and banners. Do not recolour, stretch or add effects to
the logos. Never post an image that shows a real recovery phrase or private key.
Key messages, ready-to-post copy and rules: https://monzero.org/marketing/
Press and listing kit: https://monzero.org/press/
EOF
python3 -I - "$tmp" "$W/marketing/monzero-marketing-graphics.zip" <<'PY'
import os, sys, zipfile
root, out = sys.argv[1], sys.argv[2]
files = sorted(os.path.relpath(os.path.join(d, f), root) for d, _, fs in os.walk(os.path.join(root, "monzero-marketing-graphics")) for f in fs)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name in files:
        info = zipfile.ZipInfo(name, date_time=(2026, 11, 3, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        z.writestr(info, open(os.path.join(root, name), "rb").read())
PY
ls -la "$O" | wc -l; du -sh "$O" "$W/marketing/monzero-marketing-graphics.zip"
