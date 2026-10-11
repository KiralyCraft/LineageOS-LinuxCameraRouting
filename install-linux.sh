#!/usr/bin/env bash
set -Eeuo pipefail
CAMERA_PACKAGE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
install -d "$HOME/.local/bin"
install -m 0755 "$CAMERA_PACKAGE/linux-cameractl" "$HOME/.local/bin/linux-cameractl"
printf 'Installed linux-cameractl. No Linux service or desktop restart is required.\n'
