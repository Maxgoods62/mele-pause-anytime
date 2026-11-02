#!/usr/bin/env bash
# Build LE<n>PauseAnytime.asi for each game picked with build.sh and pack it for a release: dist/LE<n>PauseAnytime-<label>.zip,
# where the label is v<N> and N is ASI_VERSION in src/SharedVersion.h, and dist/SHA256SUMS over every
# LE<n>PauseAnytime-<label>.zip in dist/. A zip holds LE<n>PauseAnytime.asi, README.txt, LICENSE, THIRD_PARTY.md, OFL.txt and the
# MIT license texts of spdlog and Dear ImGui. No .pdb: it stays in dist/stage with the build's ASI.
#
#   ./package.sh [--game LE1|LE2|LE3|all]...   the games as build.sh picks them (default: MELE_GAMES, else the released
#                                              games)
#   ./package.sh ... --label latest            another label for the file names; a label like v18 must be this version
# Runs where build.sh runs, with python and the same settings (the environment, then build.env); it sets MELE_STAGE to
# dist/stage itself. It builds only: nothing is installed into the game.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST="$HERE/dist"
STAGE="$DIST/stage"
PKG="$DIST/pkg"

die() { echo "package.sh: $*" >&2; exit 1; }
. "$HERE/scripts/games.sh"

USAGE="usage: ./package.sh [--game LE1|LE2|LE3|all]... [--label NAME]"
names=()
label=""
while (( $# > 0 )); do
    case "$1" in
        --game)
            (( $# > 1 )) || die "--game needs LE1, LE2, LE3 or all ($USAGE)"
            names+=("$2")
            shift 2 ;;
        --label)
            (( $# > 1 )) || die "--label needs a name ($USAGE)"
            label="$2"
            shift 2 ;;
        *) die "unknown argument '$1' ($USAGE)" ;;
    esac
done

# ASI_VERSION, VERSION_STRING_A (the number the log's "LE<n>PauseAnytime N ready" line prints) and VERSION_STRING_W (the
# one the ASI loader logs) must agree. PauseAnytime.rc's four-part version is made from ASI_VERSION.
VER_H="$HERE/src/SharedVersion.h"
VERSION="$(sed -n 's/^#define ASI_VERSION \([0-9][0-9]*\)[[:space:]]*$/\1/p' "$VER_H")"
[[ -n "$VERSION" ]] || die "no ASI_VERSION in $VER_H"
VERSION_A="$(sed -n 's/^#define VERSION_STRING_A "\([^"]*\)"[[:space:]]*$/\1/p' "$VER_H")"
[[ "$VERSION_A" == "$VERSION" ]] || die "VERSION_STRING_A \"$VERSION_A\" differs from ASI_VERSION $VERSION in $VER_H"
VERSION_W="$(sed -n 's/^#define VERSION_STRING_W L"\([^"]*\)"[[:space:]]*$/\1/p' "$VER_H")"
[[ "$VERSION_W" == "$VERSION" ]] || die "VERSION_STRING_W L\"$VERSION_W\" differs from ASI_VERSION $VERSION in $VER_H"

# A tag's build passes the tag as the label, so a v19 tag on v18's sources stops here.
LABEL="${label:-v$VERSION}"
[[ ! "$LABEL" =~ ^v[0-9]+$ || "$LABEL" == "v$VERSION" ]] || die "label $LABEL differs from ASI_VERSION $VERSION in $VER_H"
COMMIT="$(git -C "$HERE" rev-parse HEAD)" || die "no git commit in $HERE: the zip's README.txt names it"
# The first one that runs: on Windows, python3 can be the Microsoft Store's stub, which only prints a message.
PYTHON=""
for p in python3 python; do
    if "$p" -c '' > /dev/null 2>&1; then PYTHON="$p"; break; fi
done
[[ -n "$PYTHON" ]] || die "no python that runs: install python3"

# MELE_GAMES and MELE_LEXASIS, found the way build.sh finds them: the environment, then build.env.
for var in MELE_GAMES MELE_LEXASIS; do
    if [[ -z "${!var+set}" && -f "$HERE/build.env" ]]; then
        printf -v "$var" '%s' "$(. "$HERE/build.env" > /dev/null; printf '%s' "${!var:-}")"
    fi
done

# The games, passed to build.sh as --game. Each one needs its README.txt text.
pick_games "${names[@]}"
for g in "${GAMES[@]}"; do
    game_readme "$g" > /dev/null || die "no README.txt text for $g in scripts/games.sh (game_readme)"
done

# The LExASIs checkout (build.sh's default under %LOCALAPPDATA% when it is not set), exported so build.sh builds in that
# same tree: the spdlog and Dear ImGui license texts come from there.
if [[ -z "${MELE_LEXASIS:-}" ]]; then
    LOCAL="$(powershell.exe -NoProfile -NonInteractive -Command \
        "[Console]::OutputEncoding = [Text.Encoding]::UTF8; [Environment]::GetEnvironmentVariable('LOCALAPPDATA')" \
        2>/dev/null | tr -d '\r' || true)"
    [[ -n "$LOCAL" ]] && LOCAL="$(wslpath -u "$LOCAL" 2>/dev/null || true)"
    [[ -n "$LOCAL" ]] || die "no %LOCALAPPDATA% from PowerShell: set MELE_LEXASIS to a folder on the Windows disk"
    MELE_LEXASIS="$LOCAL/mele-asi-build/PauseAnytime/LExASIs"
fi
export MELE_LEXASIS MELE_STAGE="$STAGE"
THIRD_PARTY="$(dirname "$MELE_LEXASIS")/third_party"

# A fresh stage, so a failed build can never leave an older ASI to pack.
rm -rf "$STAGE" "$PKG"
for g in "${GAMES[@]}"; do rm -f "$DIST/${g}PauseAnytime-$LABEL.zip"; done
rm -f "$DIST/SHA256SUMS"
trap 'rm -rf "$PKG"' EXIT
build_args=()
for g in "${GAMES[@]}"; do build_args+=(--game "$g"); done
"$HERE/build.sh" "${build_args[@]}"

add() { [[ -f "$1" ]] || die "missing $1"; cp "$1" "$PKG/$2"; }
for g in "${GAMES[@]}"; do
    game_info "$g"
    ZIP="${g}PauseAnytime-$LABEL.zip"
    rm -rf "$PKG"
    mkdir -p "$PKG"
    add "$STAGE/$GAME_ASI"                      "$GAME_ASI"
    add "$HERE/LICENSE"                         LICENSE
    add "$HERE/THIRD_PARTY.md"                  THIRD_PARTY.md
    add "$HERE/src/fonts/OFL.txt"               OFL.txt
    add "$MELE_LEXASIS/External/spdlog/LICENSE" LICENSE-spdlog.txt
    add "$THIRD_PARTY/imgui/LICENSE.txt"        LICENSE-imgui.txt

    {
        printf '%s\nBuilt from commit %s\n\n' "${g}PauseAnytime $VERSION" "$COMMIT"
        game_readme "$g"
        cat <<EOF

Requirements

- Mass Effect Legendary Edition. $GAME_ASI is for $GAME_ME only.
- An ASI loader: LEBinkProxy (https://github.com/ME3Tweaks/LEBinkProxy). ME3Tweaks Mod Manager
  installs it with any mod.
- The latest Microsoft Visual C++ x64 Redistributable:
  https://aka.ms/vc14/vc_redist.x64.exe

Install

Close the game, then copy $GAME_ASI into <LE folder>\\Game\\$GAME_ME\\Binaries\\Win64\\ASI,
where <LE folder> is the Legendary Edition folder, the one that holds Game. To update, copy the
new $GAME_ASI over the old one.
EOF
        if (( ${#GAME_LEGACY[@]} > 0 )); then
            old="${GAME_LEGACY[*]}"
            cat <<EOF

Older versions were called ${old// / or }. Delete it from the ASI folder:
while an old copy is there, it runs, and $GAME_ASI stays off and writes no log.
EOF
        fi
        cat <<EOF

To check that it loaded, start the game and open
<LE folder>\\Game\\$GAME_ME\\Binaries\\Win64\\Logs\\PauseAnytime.log. It has a line that ends in
"${g}PauseAnytime $VERSION_A ready".

Uninstall

Close the game and delete $GAME_ASI from that ASI folder. You can delete
Logs\\PauseAnytime.log too.

Problems

Open an issue at https://github.com/Maxgoods62/mele-pause-anytime/issues with Logs\\PauseAnytime.log
attached. Pause Anytime rewrites the log each time the game starts, so copy it before you start the
game again. The source code and the known issues: https://github.com/Maxgoods62/mele-pause-anytime

License

Pause Anytime is GPL-3.0 (LICENSE). The Exo 2 font is under the SIL Open Font License 1.1 (OFL.txt),
spdlog and Dear ImGui under the MIT license (LICENSE-spdlog.txt, LICENSE-imgui.txt).
THIRD_PARTY.md lists all the third-party code and fonts.
EOF
    } > "$PKG/README.txt"

    FILES=("$GAME_ASI" README.txt LICENSE THIRD_PARTY.md OFL.txt LICENSE-spdlog.txt LICENSE-imgui.txt)
    # Relative paths: Windows' python can't read Git Bash's /d/... paths.
    (cd "$PKG" && "$PYTHON" -I -m zipfile -c "../$ZIP" "${FILES[@]}")
    (cd "$DIST" && "$PYTHON" -I -m zipfile -l "$ZIP")
    echo "packed $DIST/$ZIP"
done

# One SHA256SUMS for this label's zips of every game, the ones packed now and any packed before.
(cd "$DIST" && sha256sum LE[0-9]PauseAnytime-"$LABEL".zip > SHA256SUMS)
echo "wrote $DIST/SHA256SUMS"
