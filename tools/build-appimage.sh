#!/usr/bin/env bash
# build-appimage.sh — constrói Samba-Manager-NG-<ver>-x86_64.AppImage
#
# Uso: tools/build-appimage.sh [--no-build] [--output DIR] [--keep-appdir]
#
#   1. Compila e testa no servidor de build (tools/build-remote.sh), salvo
#      com --no-build (usa os binários que já estão em ./out/).
#   2. Monta o AppDir (binários sem símbolos de depuração, AppRun, .desktop,
#      ícones) e empacota com o appimagetool (versão fixa, SHA-256 conferido).
#
# O AppImage usa o Qt 6 do sistema (ver packaging/appimage/AppRun).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="$(uname -m)"
OUTPUT="$ROOT"
BUILD=1
KEEP=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)    BUILD=0; shift;;
        --output)      OUTPUT="$2"; shift 2;;
        --keep-appdir) KEEP=1; shift;;
        -h|--help)     grep '^#' "$0" | cut -c3-; exit 0;;
        *) echo "Opção desconhecida: $1" >&2; exit 1;;
    esac
done

msg() { printf '\033[1;34m[*]\033[0m %s\n' "$*"; }
ok()  { printf '\033[1;32m[+]\033[0m %s\n' "$*"; }
err() { printf '\033[1;31m[!]\033[0m %s\n' "$*" >&2; }

VERSION="$(sed -nE 's/^project\(samba-manager-ng VERSION ([0-9.]+).*/\1/p' "$ROOT/CMakeLists.txt")"
[[ -n "$VERSION" ]] || { err "Versão não encontrada no CMakeLists.txt."; exit 1; }
WORK="$ROOT/build-appimage"
APPDIR="$WORK/Samba-Manager-NG.AppDir"
OUTFILE="$OUTPUT/Samba-Manager-NG-${VERSION}-${ARCH}.AppImage"

if [[ $BUILD -eq 1 ]]; then
    "$ROOT/tools/build-remote.sh"
fi
for b in samba-manager-ng samba-manager-ng-helper; do
    [[ -x "$ROOT/out/$b" ]] || { err "out/$b ausente; rode sem --no-build."; exit 1; }
done

msg "Montando AppDir..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/libexec/samba-manager-ng" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$APPDIR/usr/share/icons/hicolor/scalable/apps"
strip -o "$APPDIR/usr/bin/samba-manager-ng" "$ROOT/out/samba-manager-ng"
strip -o "$APPDIR/usr/libexec/samba-manager-ng/samba-manager-ng-helper" "$ROOT/out/samba-manager-ng-helper"
install -m 0755 "$ROOT/packaging/appimage/AppRun" "$APPDIR/AppRun"
install -m 0644 "$ROOT/packaging/appimage/samba-manager-ng.desktop" "$APPDIR/"
install -m 0644 "$ROOT/packaging/appimage/samba-manager-ng.desktop" "$APPDIR/usr/share/applications/"
install -m 0644 "$ROOT/packaging/icons/samba-manager-ng.png" "$APPDIR/"
install -m 0644 "$ROOT/packaging/icons/samba-manager-ng.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/"
install -m 0644 "$ROOT/packaging/icons/samba-manager-ng.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/"
ln -sfn samba-manager-ng.png "$APPDIR/.DirIcon"
if command -v desktop-file-validate >/dev/null; then
    desktop-file-validate "$APPDIR/samba-manager-ng.desktop"
fi

# appimagetool: versão fixa + SHA-256 publicado pelo GitHub para o asset.
AIT_VERSION="1.9.1"
AIT_SHA256_x86_64="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
sha_var="AIT_SHA256_${ARCH}"
AIT_SHA256="${!sha_var:-}"
[[ -n "$AIT_SHA256" ]] || { err "Sem checksum do appimagetool para $ARCH."; exit 1; }
AIT="$WORK/appimagetool-$AIT_VERSION-$ARCH.AppImage"
mkdir -p "$WORK"
if [[ ! -f "$AIT" ]]; then
    msg "Baixando appimagetool $AIT_VERSION..."
    curl -fL -o "$AIT.part" \
        "https://github.com/AppImage/appimagetool/releases/download/$AIT_VERSION/appimagetool-$ARCH.AppImage"
    mv "$AIT.part" "$AIT"
fi
if ! echo "$AIT_SHA256  $AIT" | sha256sum -c --quiet -; then
    err "Checksum do appimagetool não confere; arquivo removido: $AIT"
    rm -f "$AIT"
    exit 1
fi
chmod +x "$AIT"
# Extraído: não depende de FUSE na hora de empacotar.
AIT_DIR="$WORK/appimagetool-$AIT_VERSION"
if [[ ! -x "$AIT_DIR/squashfs-root/AppRun" ]]; then
    mkdir -p "$AIT_DIR"
    (cd "$AIT_DIR" && "$AIT" --appimage-extract >/dev/null)
fi

msg "Construindo $OUTFILE ..."
mkdir -p "$OUTPUT"
rm -f "$OUTFILE"
ARCH="$ARCH" "$AIT_DIR/squashfs-root/AppRun" --no-appstream "$APPDIR" "$OUTFILE" >/dev/null
chmod +x "$OUTFILE"
ok "AppImage criado: $OUTFILE ($(du -h "$OUTFILE" | cut -f1))"
(cd "$OUTPUT" && sha256sum "$(basename "$OUTFILE")" | tee "$(basename "$OUTFILE").sha256")
[[ $KEEP -eq 1 ]] && msg "AppDir preservado em: $APPDIR" || rm -rf "$APPDIR"
