#!/usr/bin/env bash
# build-remote.sh — compila e testa no servidor de build (contêiner Fedora via
# podman) e traz os binários para ./out/.
#
# Uso: tools/build-remote.sh [--no-tests] [--sanitize] [--core-only]
#
# Variáveis (com padrão):
#   SM_BUILD_HOST  usuario@servidor (obrigatória; precisa de podman)
#   SM_BUILD_KEY   chave SSH (opcional)
#   SM_BUILD_DIR   diretório remoto (~/build/samba_managerNG)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${SM_BUILD_HOST:?defina SM_BUILD_HOST=usuario@servidor (servidor com podman)}"
RDIR="${SM_BUILD_DIR:-build/samba_managerNG}"
IMAGE="samba-manager-ng-builder:f44"
TESTS=1
CORE=0
CMAKE_EXTRA=(-DSM_BUILD_UI=ON -DSM_BUILD_TESTS=ON)

for a in "$@"; do
    case "$a" in
        --no-tests) TESTS=0;;
        --core-only) CMAKE_EXTRA+=(-DSM_BUILD_UI=OFF -DSM_BUILD_TESTS=ON); CORE=1;;
        --sanitize) CMAKE_EXTRA+=(-DSM_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug);;
        *) echo "Opção desconhecida: $a" >&2; exit 1;;
    esac
done

SSH=(ssh -o BatchMode=yes)
[[ -n "${SM_BUILD_KEY:-}" ]] && SSH+=(-i "$SM_BUILD_KEY")

msg() { printf '\033[1;34m[*]\033[0m %s\n' "$*"; }

msg "Enviando fontes para $HOST:$RDIR ..."
"${SSH[@]}" "$HOST" "mkdir -p '$RDIR'"
rsync -az --delete -e "${SSH[*]}" \
    --exclude .git --exclude 'build/' --exclude 'out/' \
    "$HERE/" "$HOST:$RDIR/src/"

msg "Preparando imagem $IMAGE (só na primeira vez ou se o Containerfile mudar)..."
"${SSH[@]}" "$HOST" "cd '$RDIR/src' && podman build -q -t '$IMAGE' -f packaging/Containerfile packaging >/dev/null"

msg "Compilando no contêiner..."
"${SSH[@]}" "$HOST" bash -s -- "$RDIR" "$IMAGE" "$TESTS" "${CMAKE_EXTRA[@]}" <<'EOF'
set -euo pipefail
RDIR="$1"; IMAGE="$2"; TESTS="$3"; shift 3
cd "$RDIR"
mkdir -p build
podman run --rm --userns=keep-id -v "$PWD/src:/src:ro,Z" -v "$PWD/build:/build:Z" -w /build "$IMAGE" \
    bash -c "cmake -S /src -B /build -G Ninja $* >/dev/null && cmake --build /build && \
             if [ '$TESTS' = 1 ]; then ctest --test-dir /build --output-on-failure; fi"
EOF

[[ $CORE = 1 ]] && { msg "Core compilado (sem interface)."; exit 0; }

msg "Trazendo binários para $HERE/out/ ..."
mkdir -p "$HERE/out"
rsync -az -e "${SSH[*]}" \
    "$HOST:$RDIR/build/samba-manager-ng" "$HOST:$RDIR/build/samba-manager-ng-helper" "$HERE/out/"
printf '\033[1;32m[+]\033[0m Pronto: %s\n' "$HERE/out/samba-manager-ng"
