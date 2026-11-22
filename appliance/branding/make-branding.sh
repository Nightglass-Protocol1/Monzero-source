#!/bin/bash
# Makes the Monzero Box brand images in rootfs/usr/share/monzero/branding from the website's
# artwork (the monzero.org banner, wordmark and coin logo). Needs ImageMagick 7 and ffmpeg.
#   bash appliance/branding/make-branding.sh [path/to/website]
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
SITE=${1:-$HERE/../../monzero-app-airdrop/website}
OUT=$HERE/rootfs/usr/share/monzero/branding
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$OUT"

ffmpeg -v error -y -i "$SITE/assets/monzero-banner-20261026-2172.webp" "$T/banner.png"
ffmpeg -v error -y -i "$SITE/assets/monzero-wordmark-20261026-1000.webp" "$T/wordmark.png"

# Wallpaper (screen, wallet desktop): the banner across a 16:9 black screen, its top and bottom
# edges faded into black.
magick "$T/banner.png" -resize 1920x640! "$T/b.png"
magick -size 1920x110 gradient:black-white \( -size 1920x420 xc:white \) \( -size 1920x110 gradient:white-black \) -append "$T/mask.png"
magick "$T/b.png" "$T/mask.png" -alpha off -compose copy_opacity -composite "$T/faded.png"
magick -size 1920x1080 xc:black "$T/faded.png" -gravity center -compose over -composite -type truecolor -quality 88 "$OUT/wallpaper-1920x1080.jpg"

# Wordmark on transparent (setup page, screen), coin logo, favicon
magick "$T/wordmark.png" -fuzz 8% -transparent black -trim +repage -resize 600x "$OUT/wordmark-600.png"
magick "$SITE/press/monzero-xmz-logo-512.png" -resize 256x256 "$OUT/logo-256.png"
magick "$SITE/press/monzero-xmz-logo-512.png" -resize 32x32 "$OUT/favicon-32.png"
ls -la "$OUT"
