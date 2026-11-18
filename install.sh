#!/bin/sh
# Karta installer for Linux and macOS.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/ajeetkbhardwaj/karta/main/install.sh | sh
#
# Environment:
#   KARTA_VERSION      Release tag to install (default: latest)
#   KARTA_INSTALL_DIR  Install directory (default: /usr/local/bin or ~/.local/bin)
#   KARTA_SKIP_CHECKSUM Set to 1 to skip SHA256 verification

set -eu

REPO="ajeetkbhardwaj/karta"
BINARY="karta"

info() { printf '%s\n' "$*"; }
err() { printf 'error: %s\n' "$*" >&2; exit 1; }

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || err "required command not found: $1"
}

need_cmd uname
need_cmd mktemp
if command -v curl >/dev/null 2>&1; then
  DOWNLOAD="curl -fsSL"
elif command -v wget >/dev/null 2>&1; then
  DOWNLOAD="wget -qO-"
else
  err "curl or wget is required"
fi

download() {
  # shellcheck disable=SC2086
  $DOWNLOAD "$1"
}

os="$(uname -s)"
case "$os" in
  Linux) os="linux" ;;
  Darwin) os="macos" ;;
  *) err "unsupported OS: $os (Windows users: download the release zip)" ;;
esac

arch="$(uname -m)"
case "$arch" in
  x86_64|amd64) arch="x86_64" ;;
  arm64|aarch64)
    if [ "$os" = "macos" ]; then arch="arm64"; else arch="aarch64"; fi
    ;;
  *) err "unsupported architecture: $arch" ;;
esac

if [ -z "${KARTA_VERSION:-}" ]; then
  KARTA_VERSION="$(download "https://api.github.com/repos/${REPO}/releases/latest" \
    | grep '"tag_name"' | sed -E 's/.*"tag_name": *"([^"]+)".*/\1/')"
  [ -n "$KARTA_VERSION" ] || err "could not determine the latest release"
fi

asset="${BINARY}-${KARTA_VERSION}-${os}-${arch}.tar.gz"
base="https://github.com/${REPO}/releases/download/${KARTA_VERSION}"

tmp="$(mktemp -d)"
cleanup() { rm -rf "$tmp"; }
trap cleanup EXIT INT TERM

info "Downloading ${asset} (${KARTA_VERSION})..."
download "${base}/${asset}" > "${tmp}/${asset}" || err "download failed: ${base}/${asset}"

if [ "${KARTA_SKIP_CHECKSUM:-0}" != "1" ]; then
  if download "${base}/SHA256SUMS" > "${tmp}/SHA256SUMS" 2>/dev/null; then
    expected="$(grep " ${asset}\$" "${tmp}/SHA256SUMS" | awk '{print $1}')"
    if [ -n "$expected" ]; then
      if command -v sha256sum >/dev/null 2>&1; then
        actual="$(sha256sum "${tmp}/${asset}" | awk '{print $1}')"
      elif command -v shasum >/dev/null 2>&1; then
        actual="$(shasum -a 256 "${tmp}/${asset}" | awk '{print $1}')"
      else
        err "sha256sum or shasum is required to verify the download"
      fi
      [ "$expected" = "$actual" ] || err "checksum verification failed"
      info "Checksum verified."
    else
      info "warning: no checksum entry for ${asset}; skipping verification" >&2
    fi
  else
    info "warning: SHA256SUMS unavailable; skipping verification" >&2
  fi
fi

tar -xzf "${tmp}/${asset}" -C "$tmp"

install_dir="${KARTA_INSTALL_DIR:-}"
if [ -z "$install_dir" ]; then
  if [ -d /usr/local/bin ] && [ -w /usr/local/bin ]; then
    install_dir="/usr/local/bin"
  else
    install_dir="${HOME}/.local/bin"
  fi
fi
mkdir -p "$install_dir"

if command -v install >/dev/null 2>&1; then
  install -m 0755 "${tmp}/${BINARY}" "${install_dir}/${BINARY}"
else
  cp "${tmp}/${BINARY}" "${install_dir}/${BINARY}"
  chmod 0755 "${install_dir}/${BINARY}"
fi

info "Installed ${BINARY} to ${install_dir}/${BINARY}"
case ":${PATH}:" in
  *":${install_dir}:"*) ;;
  *) info "Add it to your PATH: export PATH=\"${install_dir}:\$PATH\"" ;;
esac
info "Run: ${BINARY} --version"
