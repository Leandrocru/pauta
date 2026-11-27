#!/usr/bin/env bash
# Baixa o Qt para compilar o Pauta para Android (arm64) num Linux x86-64, direto de download.qt.io,
# conferindo o SHA-256 de cada pacote:
#   $DEST/<versão>/gcc_64              — Qt do Linux (ferramentas: moc, androiddeployqt…)
#   $DEST/<versão>/android_arm64_v8a   — Qt para Android
# Uso: tools/get-qt-android.sh [DEST]   — requer curl, sha256sum e python com py7zr.
set -euo pipefail

QT_VER="6.11.3"
QT_BUILD="202610220312"
DEST="${1:-/opt/qt}"
NODOT="${QT_VER//./}"
REPO="https://download.qt.io/online/qtsdkrepository"
DL="$(mktemp -d)"

fetch() { # base-url file out-dir
  local base="$1" f="$2" out="$3"
  echo "baixando $f"
  curl -sSL --retry 5 --max-time 1800 -o "$DL/$f" "$base/$f"
  local expected actual
  expected="$(curl -sSL --retry 5 "$base/$f.sha256" | cut -d' ' -f1)"
  actual="$(sha256sum "$DL/$f" | cut -d' ' -f1)"
  if [ -z "$expected" ] || [ "$expected" != "$actual" ]; then
    echo "checksum diferente em $f" >&2
    exit 1
  fi
  mkdir -p "$out"
  python3 -m py7zr x "$DL/$f" "$out"
  rm -f "$DL/$f"
  # Some archives carry <version>/<kit>/ inside: flatten them into $out.
  if [ -d "$out/$QT_VER" ]; then
    for kit in "$out/$QT_VER"/*/; do cp -a "$kit". "$out"/; done
    rm -rf "${out:?}/$QT_VER"
  fi
}

HOST_BASE="$REPO/linux_x64/desktop/qt6_${NODOT}/qt6_${NODOT}/qt.qt6.${NODOT}.linux_gcc_64"
HOST_OUT="$DEST/$QT_VER/gcc_64"
fetch "$HOST_BASE" "${QT_VER}-0-${QT_BUILD}qtbase-Linux-RHEL_9_6-GCC-Linux-RHEL_9_6-X86_64.7z" "$HOST_OUT"
fetch "$HOST_BASE" "${QT_VER}-0-${QT_BUILD}icu-linux-Rhel8.6-x86_64.7z" "$HOST_OUT/lib" # ICU files sit at the archive root

ANDROID_BASE="$REPO/all_os/android/qt6_${NODOT}/qt6_${NODOT}_arm64_v8a/qt.qt6.${NODOT}.android_arm64_v8a"
ANDROID_OUT="$DEST/$QT_VER/android_arm64_v8a"
for m in qtbase qttranslations; do
  fetch "$ANDROID_BASE" "${QT_VER}-0-${QT_BUILD}${m}-MacOS-MacOS_14-Clang-Android-Android_ANY-ARM64.7z" "$ANDROID_OUT"
done

# The archives keep the paths of the machine that built them in a few text files; point them here.
for f in "$ANDROID_OUT/bin/qt-cmake" "$ANDROID_OUT/bin/target_qt.conf" "$ANDROID_OUT/bin/qmake"; do
  [ -f "$f" ] || continue
  if grep -q "HostPrefix" "$f" 2>/dev/null; then
    sed -i "s|^HostPrefix=.*|HostPrefix=../../gcc_64|; s|^HostData=.*|HostData=../android_arm64_v8a|" "$f"
  fi
done
chmod +x "$HOST_OUT"/bin/* "$HOST_OUT"/libexec/* 2>/dev/null || true
echo "Qt pronto em $DEST/$QT_VER"
ls "$DEST/$QT_VER"
