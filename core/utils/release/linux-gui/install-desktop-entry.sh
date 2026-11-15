#!/bin/sh
# Adds Monzero to this user's applications menu, pointing at the folder this script is in.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
icons="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"
mkdir -p "$apps" "$icons"
cp "$here/monzero.png" "$icons/monzero.png"
cat > "$apps/monzero-gui.desktop" <<ENTRY
[Desktop Entry]
Name=Monzero
GenericName=Monzero wallet
Comment=Monzero (XMZ) wallet
Keywords=Monzero;XMZ;
Exec="$here/monzero-wallet-gui" %u
Terminal=false
Type=Application
Icon=monzero
Categories=Network;Finance;Qt;
MimeType=x-scheme-handler/monzero;x-scheme-handler/monzeroseed
StartupNotify=true
StartupWMClass=monzero-wallet-gui
ENTRY
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$apps" >/dev/null 2>&1 || true
echo "Added: $apps/monzero-gui.desktop"
