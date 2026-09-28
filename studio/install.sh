#!/bin/bash
# Copyright (C) 2026 Ryan Arthur. Part of Astrolog Studio. Licensed under the
# GNU General Public License, version 2 or (at your option) any later version.
# Install a desktop launcher for Astrolog Studio for the current user, so it
# appears in the Omarchy app launcher (Super+Space). The binary stays in the
# source tree next to its data files; ~/.local/bin gets a symlink to it.
#
#   studio/install.sh [source-dir]     install
#   studio/install.sh --uninstall      remove launcher, icon and symlink

set -e

APP=astrolog-studio
BIN_DIR="$HOME/.local/bin"
APPS_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICON_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"

if [[ $1 == --uninstall ]]; then
  rm -f "$BIN_DIR/$APP" "$APPS_DIR/$APP.desktop" "$ICON_DIR/$APP.svg"
  update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
  echo "Removed Astrolog Studio launcher."
  exit 0
fi

SRC="$(cd "${1:-$(dirname "$0")/..}" && pwd)"
if [[ ! -x $SRC/$APP ]]; then
  echo "Build first: make studio" >&2
  exit 1
fi

mkdir -p "$BIN_DIR" "$APPS_DIR" "$ICON_DIR"
ln -sf "$SRC/$APP" "$BIN_DIR/$APP"
cp "$SRC/studio/$APP.svg" "$ICON_DIR/$APP.svg"

cat >"$APPS_DIR/$APP.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Astrolog Studio
GenericName=Astrology Charts
Comment=Cast and explore astrological charts with the Astrolog engine
Exec=$SRC/$APP
Icon=$APP
Terminal=false
Categories=Education;Science;Astronomy;
Keywords=astrology;horoscope;chart;natal;transits;astrolog;
StartupWMClass=org.astrolog.Studio
EOF

update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
echo "Installed: launcher '$APPS_DIR/$APP.desktop', command '$BIN_DIR/$APP'."
