#!/usr/bin/env bash
# Build MelePause.asi with build.sh and pack it for a release: dist/MelePause-v<N>.zip and dist/SHA256SUMS, where N is
# ASI_VERSION in src/SharedVersion.h. The zip holds MelePause.asi, README.txt, LICENSE, THIRD_PARTY.md, OFL.txt and the
# MIT license texts of spdlog and Dear ImGui. No .pdb: it stays in dist/stage with the build's MelePause.asi.
#
# DO NOT PUBLISH THE ZIP. MelePause.asi compiles in code from ME3Tweaks/LExASIs and llamathings/LExSDKv2, which publish
# no license (see THIRD_PARTY.md). The zip stays on your machine until their authors allow a prebuilt download.
#
#   ./package.sh
# Runs in WSL, with python3 and the same settings as build.sh (the environment, then build.env); it sets MELE_STAGE to
# dist/stage itself. It builds only: nothing is installed into the game.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST="$HERE/dist"
STAGE="$DIST/stage"
PKG="$DIST/pkg"

die() { echo "package.sh: $*" >&2; exit 1; }

# ASI_VERSION, VERSION_STRING_A (the number the log's "MelePause N ready" line prints) and VERSION_STRING_W (the one
# the ASI loader logs) must agree.
VER_H="$HERE/src/SharedVersion.h"
VERSION="$(sed -n 's/^#define ASI_VERSION \([0-9][0-9]*\)[[:space:]]*$/\1/p' "$VER_H")"
[[ -n "$VERSION" ]] || die "no ASI_VERSION in $VER_H"
VERSION_A="$(sed -n 's/^#define VERSION_STRING_A "\([^"]*\)"[[:space:]]*$/\1/p' "$VER_H")"
[[ "$VERSION_A" == "$VERSION" ]] || die "VERSION_STRING_A \"$VERSION_A\" differs from ASI_VERSION $VERSION in $VER_H"
VERSION_W="$(sed -n 's/^#define VERSION_STRING_W L"\([^"]*\)"[[:space:]]*$/\1/p' "$VER_H")"
[[ "$VERSION_W" == "$VERSION" ]] || die "VERSION_STRING_W L\"$VERSION_W\" differs from ASI_VERSION $VERSION in $VER_H"
ZIP="MelePause-v$VERSION.zip"

# The LExASIs checkout, found the way build.sh finds it (the environment, then build.env, then %LOCALAPPDATA%), and
# exported so build.sh builds in that same tree: the spdlog and Dear ImGui license texts come from there.
if [[ -z "${MELE_LEXASIS+set}" && -f "$HERE/build.env" ]]; then
    MELE_LEXASIS="$(. "$HERE/build.env" > /dev/null; printf '%s' "${MELE_LEXASIS:-}")"
fi
if [[ -z "${MELE_LEXASIS:-}" ]]; then
    LOCAL="$(powershell.exe -NoProfile -NonInteractive -Command \
        "[Console]::OutputEncoding = [Text.Encoding]::UTF8; [Environment]::GetEnvironmentVariable('LOCALAPPDATA')" \
        2>/dev/null | tr -d '\r' || true)"
    [[ -n "$LOCAL" ]] && LOCAL="$(wslpath -u "$LOCAL" 2>/dev/null || true)"
    [[ -n "$LOCAL" ]] || die "no %LOCALAPPDATA% from PowerShell: set MELE_LEXASIS to a folder on the Windows disk"
    MELE_LEXASIS="$LOCAL/mele-asi-build/MelePause/LExASIs"
fi
export MELE_LEXASIS MELE_STAGE="$STAGE"
THIRD_PARTY="$(dirname "$MELE_LEXASIS")/third_party"

# A fresh stage, so a failed build can never leave an older MelePause.asi to pack.
rm -rf "$STAGE" "$PKG"
rm -f "$DIST/$ZIP" "$DIST/SHA256SUMS"
trap 'rm -rf "$PKG"' EXIT
"$HERE/build.sh"

mkdir -p "$PKG"
add() { [[ -f "$1" ]] || die "missing $1"; cp "$1" "$PKG/$2"; }
add "$STAGE/MelePause.asi"                  MelePause.asi
add "$HERE/LICENSE"                         LICENSE
add "$HERE/THIRD_PARTY.md"                  THIRD_PARTY.md
add "$HERE/src/fonts/OFL.txt"               OFL.txt
add "$MELE_LEXASIS/External/spdlog/LICENSE" LICENSE-spdlog.txt
add "$THIRD_PARTY/imgui/LICENSE.txt"        LICENSE-imgui.txt

cat > "$PKG/README.txt" <<EOF
MelePause $VERSION

MelePause.asi is an ASI plugin for Mass Effect 1 in the Legendary Edition (LE1). Esc, or Start
on a gamepad, pauses a conversation, an in-engine cutscene or a fullscreen movie such as the
galaxy map's travel movie, and Esc, Start, Space or A resumes it. While the game is paused, the
picture is blurred and darkened under a pause screen. In a paused conversation or cutscene, a
gamepad's Y or F7 opens the game's photo mode on the paused shot.

Requirements

- Mass Effect Legendary Edition. MelePause is for ME1 only.
- An ASI loader: LEBinkProxy (https://github.com/ME3Tweaks/LEBinkProxy). ME3Tweaks Mod Manager
  installs it with any mod.
- The latest Microsoft Visual C++ x64 Redistributable (14.50 or newer):
  https://aka.ms/vc14/vc_redist.x64.exe

Install

Close the game, then copy MelePause.asi into <LE folder>\\Game\\ME1\\Binaries\\Win64\\ASI, where
<LE folder> is the Legendary Edition folder, the one that holds Game. To update, copy the new
MelePause.asi over the old one.

To check that it loaded, start the game and open
<LE folder>\\Game\\ME1\\Binaries\\Win64\\Logs\\MelePause.log. It has a line that ends in
"MelePause $VERSION_A ready".

Uninstall

Close the game and delete MelePause.asi from that ASI folder. You can delete Logs\\MelePause.log
too.

Problems

Open an issue at https://github.com/Maxgoods62/mele-pause/issues with Logs\\MelePause.log
attached. MelePause rewrites the log each time the game starts, so copy it before you start the
game again. The source code and the known issues: https://github.com/Maxgoods62/mele-pause

License

MelePause is GPL-3.0 (LICENSE). The Exo 2 font is under the SIL Open Font License 1.1 (OFL.txt),
spdlog and Dear ImGui under the MIT license (LICENSE-spdlog.txt, LICENSE-imgui.txt).
THIRD_PARTY.md lists all the third-party code and fonts.
EOF

FILES=(MelePause.asi README.txt LICENSE THIRD_PARTY.md OFL.txt LICENSE-spdlog.txt LICENSE-imgui.txt)
(cd "$PKG" && python3 -I -m zipfile -c "$DIST/$ZIP" "${FILES[@]}")
(cd "$DIST" && sha256sum "$ZIP" > SHA256SUMS)
python3 -I -m zipfile -l "$DIST/$ZIP"
echo "packed $DIST/$ZIP and $DIST/SHA256SUMS (local only: see the top of package.sh)"
