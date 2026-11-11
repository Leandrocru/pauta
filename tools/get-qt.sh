#!/usr/bin/env bash
# Baixa os pacotes oficiais do Qt para Windows (MSVC 64-bit) direto de download.qt.io,
# confere o SHA-256 de cada um e extrai em $DEST/<versão>/msvc2022_64.
# (O aqtinstall 3.3 ainda não entende a organização nova do repositório do Qt.)
# Uso: tools/get-qt.sh [DEST]   — requer curl, sha256sum e python com py7zr.
set -euo pipefail

QT_VER="6.11.3"
QT_BUILD="202610220312"
DEST="${1:-C:/Qt}"
NODOT="${QT_VER//./}"
BASE="https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_${NODOT}/qt6_${NODOT}_msvc2022_64/qt.qt6.${NODOT}.win64_msvc2022_64"
OUT="${DEST}/${QT_VER}/msvc2022_64"
DL="$(mktemp -d)"

mkdir -p "$OUT"
for m in qtbase qtsvg qttranslations; do
  f="${QT_VER}-0-${QT_BUILD}${m}-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z"
  echo "baixando $m"
  curl -sSL --retry 5 --max-time 1800 -o "$DL/$f" "$BASE/$f"
  expected="$(curl -sSL --retry 5 "$BASE/$f.sha256" | cut -d' ' -f1)"
  actual="$(sha256sum "$DL/$f" | cut -d' ' -f1)"
  if [ -z "$expected" ] || [ "$expected" != "$actual" ]; then
    echo "checksum diferente em $m" >&2
    exit 1
  fi
  python -m py7zr x "$DL/$f" "$OUT"
done
echo "Qt pronto em $OUT"
