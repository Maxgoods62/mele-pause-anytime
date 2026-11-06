#!/usr/bin/env bash
# Build LE<n>PauseAnytime.asi for each game picked with build.sh and pack them for a release in one zip,
# dist/PauseAnytime-<label>.zip, where the label is v<N> and N is ASI_VERSION in src/SharedVersion.h, and write
# dist/SHA256SUMS over it. The zip holds a single README.txt that covers all the games, a folder per game
# (LE1\LE1PauseAnytime.asi, ...) and a LICENSE folder: LICENSE, THIRD_PARTY.md, OFL.txt and the MIT license texts of spdlog
# and Dear ImGui. No .pdb: it stays in dist/stage with the build's ASI.
#
#   ./package.sh [--game LE1|LE2|LE3|all]...   the games as build.sh picks them (default: MELE_GAMES, else the released
#                                              games); a v<N> label needs every released game
#   ./package.sh ... --label latest            another label for the file name; a label like v18 must be this version
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

# The games, passed to build.sh as --game. Each one needs its README.txt text. A release is one zip, so it holds them all.
pick_games "${names[@]}"
for g in "${GAMES[@]}"; do
    game_readme "$g" > /dev/null || die "no README.txt text for $g in scripts/games.sh (game_readme)"
done
[[ ! "$LABEL" =~ ^v[0-9]+$ || "${GAMES[*]}" == "${RELEASED_GAMES[*]}" ]] \
    || die "label $LABEL packs ${GAMES[*]}: a release zip needs every released game (${RELEASED_GAMES[*]})"

# Git Bash has cygpath in place of wslpath, as in build.sh.
if ! command -v wslpath > /dev/null; then
    wslpath() { cygpath "$@"; }
fi

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

ZIP="PauseAnytime-$LABEL.zip"
# A fresh stage, so a failed build can never leave an older ASI to pack. This label's zip and SHA256SUMS go, and so do
# the per-game zips an earlier v18 package.sh wrote under this label (LE<n>PauseAnytime-<label>.zip). Zips of other labels
# stay: SHA256SUMS lists only this label's zip.
rm -rf "$STAGE" "$PKG"
rm -f "$DIST/$ZIP" "$DIST"/LE[0-9]PauseAnytime-"$LABEL".zip "$DIST/SHA256SUMS"
trap 'rm -rf "$PKG"' EXIT
build_args=()
for g in "${GAMES[@]}"; do build_args+=(--game "$g"); done
"$HERE/build.sh" "${build_args[@]}"

# and_list A B C prints "A, B and C".
and_list() {
    local out="$1"
    shift
    while (( $# > 1 )); do out+=", $1"; shift; done
    if (( $# == 1 )); then out+=" and $1"; fi
    printf '%s' "$out"
}

add() { [[ -f "$1" ]] || die "missing $1"; cp "$1" "$PKG/$2"; }
mkdir -p "$PKG/LICENSE"
for g in "${GAMES[@]}"; do
    game_info "$g"
    mkdir -p "$PKG/$g"
    add "$STAGE/$GAME_ASI" "$g/$GAME_ASI"
done
add "$HERE/LICENSE"                         LICENSE/LICENSE
add "$HERE/THIRD_PARTY.md"                  LICENSE/THIRD_PARTY.md
add "$HERE/src/fonts/OFL.txt"               LICENSE/OFL.txt
add "$MELE_LEXASIS/External/spdlog/LICENSE" LICENSE/LICENSE-spdlog.txt
add "$THIRD_PARTY/imgui/LICENSE.txt"        LICENSE/LICENSE-imgui.txt

{
    printf 'Pause Anytime %s\nBuilt from commit %s\n\n' "$VERSION" "$COMMIT"
    if (( ${#GAMES[@]} > 1 )); then
        printf 'Each game has its own file, in the folder named after the game: %s.\n' "$(and_list "${GAMES[@]}")"
    else
        printf 'This zip holds the file for %s only, in the folder named after the game.\n' "${GAMES[0]}"
    fi
    printf 'The LICENSE folder holds the license texts.\n'
    cat <<'EOF'

Pause Anytime lets you pause Mass Effect Legendary Edition where the game does not: during
conversations, in-engine cutscenes and some fullscreen movies. Press Esc, or Start on a
gamepad, to pause. Press Esc, Start, Space or A to resume at the same word. While the game is
paused, the picture is blurred and darkened under a pause screen. In a paused conversation or
cutscene, press Y on a gamepad or F7 to open the game's photo mode on the paused shot.
Loading-screen movies do not pause. Everywhere else the keys work as in the game without mods.

EOF
    for g in "${GAMES[@]}"; do
        game_readme "$g"
    done
    cat <<'EOF'

Every key in each game: https://github.com/Maxgoods62/mele-pause-anytime/blob/main/docs/using.md

Requirements

- Mass Effect Legendary Edition. Each file works only in its own game (see Install below).
- An ASI loader: LEBinkProxy (https://github.com/ME3Tweaks/LEBinkProxy). ME3Tweaks Mod Manager
  installs it with any mod. The loader is there when <LE folder>\Game\ME<n>\Binaries\Win64
  holds bink2w64_original.dll and an ASI folder.
- The latest Microsoft Visual C++ x64 Redistributable:
  https://aka.ms/vc14/vc_redist.x64.exe

Install

Close the game, then copy the file of each game you play into that game's ASI folder:

EOF
    for g in "${GAMES[@]}"; do
        game_info "$g"
        printf '  %s\\%s  into  <LE folder>\\Game\\%s\\Binaries\\Win64\\ASI\n' "$g" "$GAME_ASI" "$GAME_ME"
    done
    cat <<'EOF'

<LE folder> is the folder where Mass Effect Legendary Edition is installed. It contains a
folder named Game. Copy the .asi file itself, not the folder it is in. To update, copy the new
file over the old one.

To check that it loaded, start the game and open
<LE folder>\Game\ME<n>\Binaries\Win64\Logs\PauseAnytime.log, where <n> is the game's number.
EOF
    cat <<EOF
It has a line that ends in "LE<n>PauseAnytime $VERSION_A ready".

Uninstall

Close the game and delete LE<n>PauseAnytime.asi from that game's ASI folder. You can delete
Logs\\PauseAnytime.log too.

Problems

Common problems and known issues:
https://github.com/Maxgoods62/mele-pause-anytime/blob/main/docs/troubleshooting.md
The full guide to install, update and uninstall:
https://github.com/Maxgoods62/mele-pause-anytime/blob/main/docs/install.md

To report a bug, open an issue at https://github.com/Maxgoods62/mele-pause-anytime/issues and
attach Logs\\PauseAnytime.log. Pause Anytime rewrites the log each time the game starts, so copy
it before you start the game again.

License

Pause Anytime is GPL-3.0 (LICENSE\\LICENSE). Third-party code and fonts: LICENSE\\THIRD_PARTY.md.
Source code: https://github.com/Maxgoods62/mele-pause-anytime

All of the code was written by an LLM, Claude Opus 5.5, from my instructions.
EOF
} > "$PKG/README.txt"

# Relative paths: Windows' python can't read Git Bash's /d/... paths. A folder goes in with all its files.
(cd "$PKG" && "$PYTHON" -I -m zipfile -c "../$ZIP" README.txt "${GAMES[@]}" LICENSE)
(cd "$DIST" && "$PYTHON" -I -m zipfile -l "$ZIP")
echo "packed $DIST/$ZIP"

(cd "$DIST" && sha256sum "$ZIP" > SHA256SUMS)
echo "wrote $DIST/SHA256SUMS"
