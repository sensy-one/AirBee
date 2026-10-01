#!/bin/bash
set -euo pipefail

INSTALL_DIR=$(cd "$(dirname "$0")" && pwd)
CACHE_DIR="$HOME/Library/Caches/SENSY-ONE/HomeyInstaller"
NODE_VERSION=24.21.0
TEMP_DIR=

finish() {
  result=$?
  trap - EXIT
  if [[ -n "$TEMP_DIR" ]]; then rm -rf "$TEMP_DIR"; fi
  if [[ $result -ne 0 ]]; then
    printf '\nInstallation stopped. See the error above. You can run this installer again.\n'
  fi
  if [[ -t 0 ]]; then read -r -p 'Press Enter to close this window.' _ || true; fi
  exit "$result"
}
trap finish EXIT

printf 'SENSY-ONE for Homey\nKeep this Mac and Homey on the same network.\n\n'
case "$(uname -m)" in
  arm64) NODE_ARCH=arm64; NODE_SHA=bed7eea5325e1108f32ce5228ddd6a5f0f08a499ee42aa7442aea583702f6057 ;;
  x86_64) NODE_ARCH=x64; NODE_SHA=1462cb3b3046b815cf8ea436d3da450ec1a9f11dac7e5a46b0ada5305d7e8097 ;;
  *) printf 'This installer requires an Intel or Apple Silicon Mac.\n' >&2; exit 1 ;;
esac

NODE_NAME="node-v$NODE_VERSION-darwin-$NODE_ARCH"
NODE_DIR="$CACHE_DIR/$NODE_NAME"
mkdir -p "$CACHE_DIR"
if [[ ! -x "$NODE_DIR/bin/node" ]]; then
  printf '1/4 - Downloading the installer tools...\n'
  TEMP_DIR=$(mktemp -d "$CACHE_DIR/download.XXXXXX")
  curl --silent --show-error --fail --location --retry 2 --proto '=https' --tlsv1.2 \
    "https://nodejs.org/dist/v$NODE_VERSION/$NODE_NAME.tar.gz" -o "$TEMP_DIR/node.tar.gz"
  ACTUAL_SHA=$(shasum -a 256 "$TEMP_DIR/node.tar.gz" | awk '{print $1}')
  if [[ "$ACTUAL_SHA" != "$NODE_SHA" ]]; then
    printf 'Download verification failed. Please try again.\n' >&2; exit 1
  fi
  tar -xzf "$TEMP_DIR/node.tar.gz" -C "$TEMP_DIR"
  if [[ -e "$NODE_DIR" ]]; then rm -rf "$NODE_DIR"; fi
  mv "$TEMP_DIR/$NODE_NAME" "$NODE_DIR"
else
  printf '1/4 - Installer tools are ready.\n'
fi

export PATH="$NODE_DIR/bin:$PATH"
"$NODE_DIR/bin/node" "$INSTALL_DIR/installer/install.cjs" "$CACHE_DIR" "$@"
