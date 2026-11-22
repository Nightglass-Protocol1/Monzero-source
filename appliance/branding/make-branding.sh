#!/bin/bash
# Makes the Monzero Box brand images in rootfs/usr/share/monzero/branding from the website's
# artwork (the monzero.org banner, wordmark and coin logo). Needs ImageMagick 7 and ffmpeg.
#   bash appliance/branding/make-branding.sh [path/to/website]
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
SITE=${1:-$HERE/../../monzero-app-airdrop/website}
OUT=$HERE/rootfs/usr/share/monzero/branding
WP=$HERE/rootfs/usr/share/monzero/wallpapers
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$OUT/icons" "$WP"

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

# Desktop wallpapers (Personalise picks from these; the first is the screen's own artwork)
ln -sfn ../branding/wallpaper-1920x1080.jpg "$WP/01-monzero.jpg"
magick "$SITE/assets/monzero-community-20261026-1672.webp" -resize 1920x1080^ -gravity center -extent 1920x1080 -quality 88 "$WP/02-community.jpg"
# the emblem on black with a soft cyan glow behind it
magick -size 1920x1080 radial-gradient:'#0c3a44'-black "$T/glow.png"
magick "$T/glow.png" \( "$SITE/press/monzero-emblem-dark-20261026-1254.jpg" -resize 560x560 \) -gravity center -compose lighten -composite -quality 88 "$WP/03-emblem.jpg"
# the silver coin on deep teal
magick -size 1920x1080 gradient:'#0d2a33'-'#02070a' \( "$SITE/assets/monzero-coin-transparent-20261018.png" -resize 520x520 \) -gravity center -compose over -composite -quality 88 "$WP/04-coin.jpg"
# quiet: dark gradient, small wordmark in the corner
magick -size 1920x1080 gradient:'#0a161b'-'#030608' \( "$OUT/wordmark-600.png" -resize 320x -channel A -evaluate multiply 0.55 +channel \) -gravity southeast -geometry +60+80 -compose over -composite -quality 88 "$WP/05-midnight.jpg"

# Desktop and panel icons (128 px)
magick "$SITE/press/monzero-xmz-logo-512.png" -resize 128x128 "$OUT/icons/wallet.png"
tile() { magick -size 128x128 xc:none -fill '#0e1a20' -stroke '#19d6e6' -strokewidth 4 -draw 'roundrectangle 6,6 121,121 26,26' "$@"; }
tile -stroke none -fill '#19d6e6' -draw 'circle 44,48 44,34' -fill '#9b7bff' -draw 'circle 84,48 84,34' \
     -fill '#ffb020' -draw 'circle 44,86 44,72' -fill '#2fd57a' -draw 'circle 84,86 84,72' "$OUT/icons/personalise.png"
tile -stroke none -fill '#19d6e6' -draw 'circle 64,64 64,24' -fill '#0e1a20' -font DejaVu-Sans-Bold -pointsize 58 -gravity center -annotate +0+2 'i' "$OUT/icons/about.png"
ls -la "$OUT" "$OUT/icons" "$WP"
