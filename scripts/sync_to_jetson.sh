#!/usr/bin/env bash
# Copy jetson-imx708-dual to a Jetson Orin Nano over SSH (key-only).
#
# Usage:
#   ./scripts/sync_to_jetson.sh HOST [DEST]
#
# Example:
#   ./scripts/sync_to_jetson.sh jetson
#   ./scripts/sync_to_jetson.sh jetson ~/jetson-imx708-dual

set -euo pipefail

usage() {
  echo "Usage: $0 HOST [DEST]" >&2
  echo "  HOST  SSH config Host alias (for example jetson). Required." >&2
  echo "  DEST  Remote directory. Default: jetson-imx708-dual" >&2
  exit 2
}

if [[ $# -lt 1 || $# -gt 2 || -z ${1:-} ]]; then
  usage
fi

HOST=$1
DEST=${2:-jetson-imx708-dual}

if [[ $HOST == -* || $HOST == *" "* || $HOST == *$'\t'* ]]; then
  echo "HOST must be an SSH config Host alias." >&2
  exit 2
fi

case $DEST in
  "" | -* | *" "* | *$'\t'* | / | /home | /root)
    echo "Refusing destination: ${DEST}" >&2
    exit 2
    ;;
  *[\'\"\;\|\&\$\(\)\`]*)
    echo "DEST contains an unsafe shell character." >&2
    exit 2
    ;;
esac

for tool in ssh rsync; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "$tool is not on PATH." >&2
    exit 127
  fi
done

DRIVER_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

echo "Syncing ${DRIVER_DIR} -> ${HOST}:${DEST}"
ssh -o BatchMode=yes "$HOST" "mkdir -p -- '${DEST}'"
rsync -a \
  -e "ssh -o BatchMode=yes" \
  --exclude '*.o' \
  --exclude '*.ko' \
  --exclude '*.mod*' \
  --exclude '*.dtbo' \
  --exclude '.tmp_versions/' \
  --exclude 'Module.symvers' \
  --exclude 'modules.order' \
  "${DRIVER_DIR}/" \
  "${HOST}:${DEST}/"

echo "Done. On the Jetson, run:"
echo "  cd ${DEST} && sudo ./scripts/install.sh"
