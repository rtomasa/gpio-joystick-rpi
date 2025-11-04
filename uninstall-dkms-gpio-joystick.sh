#!/usr/bin/env bash
set -euo pipefail
NAME="gpio-joystick-rpi"
VER="${1:-1.0}"

sudo dkms remove -m "$NAME" -v "$VER" --all || true
sudo rm -rf "/usr/src/${NAME}-${VER}"

# Quitar overlays instalados
for d in /boot/firmware/overlays /boot/overlays; do
  if [[ -d "$d" ]]; then
    sudo rm -f "$d/gpio-joystick-pullup.dtbo" \
              "$d/gpio-joystick-bias-disable.dtbo" || true
  fi
done

echo "DKMS status:"
dkms status | grep -E "^${NAME}/|^${NAME}:" || true
