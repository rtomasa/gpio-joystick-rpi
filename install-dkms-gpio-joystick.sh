#!/usr/bin/env bash
set -euo pipefail

NAME="gpio-joystick-rpi"
VER="${1:-1.0}"     # version = folder and dkms.conf
SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
DST="/usr/src/${NAME}-${VER}"

need() { command -v "$1" >/dev/null || { echo "Falta $1"; exit 1; }; }

# Deps
need dkms
test -d /lib/modules/$(uname -r)/build || {
  echo "Headers no encontrados. Instala: sudo apt install raspberrypi-kernel-headers"; exit 1; }
need dtc
need cpp

# Minimal copy for DKMS
sudo rm -rf "$DST"
sudo install -d "$DST"
sudo install -m 0644 "$SRC_DIR/gpio-joystick.c" "$DST/"
sudo install -m 0644 "$SRC_DIR/Makefile"        "$DST/"
# dkms.conf: sustituye @VERSION@ si existe
if grep -q '1.0' "$SRC_DIR/dkms.conf" 2>/dev/null; then
  sed "s/1.0/${VER}/g" "$SRC_DIR/dkms.conf" | sudo tee "$DST/dkms.conf" >/dev/null
else
  sudo install -m 0644 "$SRC_DIR/dkms.conf" "$DST/dkms.conf"
fi

# DKMS: re-add/build/install
sudo dkms remove  -m "$NAME" -v "$VER" --all >/dev/null 2>&1 || true
sudo dkms add     -m "$NAME" -v "$VER"
sudo dkms build   -m "$NAME" -v "$VER"
sudo dkms install -m "$NAME" -v "$VER"

echo "DKMS status:"
dkms status | grep -E "^${NAME}/|^${NAME}:" || true

# Overlays
DT_OUT_DIR=""
for d in /boot/firmware/overlays /boot/overlays; do
  if [[ -d "$d" ]]; then DT_OUT_DIR="$d"; break; fi
done
[[ -n "$DT_OUT_DIR" ]] || { echo "Cannot find overlays folder in /boot"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CPPFLAGS_DTS="-x assembler-with-cpp -nostdinc -undef -D__DTS__ -Wno-trigraphs -P"
DTC_FLAGS="-I dts -O dtb -@"

# Compile pull-up
cpp $CPPFLAGS_DTS -DBIAS_PULL_UP=1 "$SRC_DIR/gpio-joystick.dts" > "$TMP/pullup.tmp.dts"
dtc $DTC_FLAGS -o "$TMP/gpio-joystick-pullup.dtbo" "$TMP/pullup.tmp.dts"

# Compile bias-disable
cpp $CPPFLAGS_DTS "$SRC_DIR/gpio-joystick.dts" > "$TMP/bias.tmp.dts"
dtc $DTC_FLAGS -o "$TMP/gpio-joystick-bias-disable.dtbo" "$TMP/bias.tmp.dts"

# Install
sudo install -m 0644 "$TMP/gpio-joystick-pullup.dtbo"       "$DT_OUT_DIR/"
sudo install -m 0644 "$TMP/gpio-joystick-bias-disable.dtbo" "$DT_OUT_DIR/"
echo "Overlays installed in $DT_OUT_DIR:"
echo "  - gpio-joystick-pullup.dtbo"
echo "  - gpio-joystick-bias-disable.dtbo"
