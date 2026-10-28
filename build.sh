#!/usr/bin/env bash
# Build MelePause.asi with the Visual Studio Build Tools, inside the checkout of ME3Tweaks/LExASIs that mele-bridge builds in
# (it provides the SDK, the ASI loader interface and the CMake helpers), then stage it in D:\MELE\tools\mele-pause.
#   ./build.sh              build and stage
#   ./build.sh --install    also copy it to the game's ASI folder (the game must be closed)
#   ./build.sh --uninstall  remove it from the ASI folder (the game must be closed)
# MelePause.asi in Game\ME1\Binaries\Win64\ASI is the only file this tool writes inside the game install.
# The tree is shared with mele-bridge: both scripts take the same lock, so one waits for the other.
set -euo pipefail

LEXASIS="${MELE_LEXASIS:-/mnt/d/MELE/asi-build/LExASIs}"
LEXASIS_COMMIT=aaad03a
GAME="${MELE_GAME:-/mnt/d/SteamLibrary/steamapps/common/Mass Effect Legendary Edition}"
ASI_DIR="$GAME/Game/ME1/Binaries/Win64/ASI"
STAGE=/mnt/d/MELE/tools/mele-pause
VS='/mnt/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools'
CMAKE="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Closed only when tasklist answers and lists no MassEffect1.exe: a failed tasklist counts as running.
game_closed() {
    local out
    out="$(tasklist.exe /FO CSV /NH /FI "IMAGENAME eq MassEffect1.exe" 2>/dev/null)" || return 1
    ! grep -qi 'MassEffect1.exe' <<< "$out"
}

if [[ "${1:-}" == "--uninstall" ]]; then
    game_closed || { echo "MassEffect1.exe is running: close the game first." >&2; exit 1; }
    rm -f "$ASI_DIR/MelePause.asi"
    echo "removed $ASI_DIR/MelePause.asi"
    exit 0
fi

if [[ ! -d "$LEXASIS/.git" ]]; then
    mkdir -p "$(dirname "$LEXASIS")"
    git.exe clone --recurse-submodules https://github.com/ME3Tweaks/LExASIs.git "$(wslpath -w "$LEXASIS")"
    git.exe -C "$(wslpath -w "$LEXASIS")" checkout --recurse-submodules "$LEXASIS_COMMIT"
fi

# One build of the tree at a time (mele-bridge's build.sh takes the same lock): two configures and two MSBuilds would
# write the same Common-LE1/LESDK-LE1 objects and PDB. Held until the script exits.
exec 9> "$(dirname "$LEXASIS")/.lexasis-build.lock"
flock -n 9 || { echo "another build of $LEXASIS is running (mele-bridge or mele-pause): waiting for it" >&2; flock 9; }

# Our sources become one more ASI project of that tree. The project line is guarded, so the tree (and mele-bridge's
# build) still configures if LExASIs/MelePause is ever deleted; an unguarded line from an earlier build is replaced.
mkdir -p "$LEXASIS/MelePause"
rsync -a --delete "$HERE/src/" "$LEXASIS/MelePause/"
CML="$LEXASIS/CMakeLists.txt"
if grep -qx 'add_subdirectory ("MelePause")' "$CML"; then sed -i '/^add_subdirectory ("MelePause")$/d' "$CML"; fi
grep -qF 'EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MelePause"' "$CML" \
    || printf '%s\n' 'if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MelePause")' '  add_subdirectory ("MelePause")' 'endif ()' >> "$CML"

cd "$LEXASIS"
# LEASI_GAME1_WIN64 would make every ASI build copy itself into the game: keep it unset. MELE_THIRD_PARTY (mele-bridge's
# ImGui) keeps the value its build cached.
env -u LEASI_GAME1_WIN64 "$CMAKE" --preset VisualStudio -DLEASI_BUILD_LE2=OFF -DLEASI_BUILD_LE3=OFF -DLEASI_BUILD_LEL=OFF > /dev/null 2>&1 \
    || { echo "cmake configure failed: run it by hand in $LEXASIS" >&2; exit 1; }
log="$(mktemp)"
if ! "$CMAKE" --build Build --config RELEASE --target MelePause-LE1 -- /m /v:minimal /nologo > "$log" 2>&1; then
    tr -d '\r' < "$log" | grep -E 'error|warning' | sed -E 's|^.*LExASIs\\||; s| \[[^]]*vcxproj\]||' | sort -u
    rm -f "$log"
    exit 1
fi
rm -f "$log"

mkdir -p "$STAGE"
cp Build/RELEASE/MelePause.asi Build/RELEASE/MelePause.pdb "$STAGE/"
echo "built $STAGE/MelePause.asi"

if [[ "${1:-}" == "--install" ]]; then
    game_closed || { echo "MassEffect1.exe is running: close the game first (built, not installed)." >&2; exit 1; }
    cp "$STAGE/MelePause.asi" "$ASI_DIR/MelePause.asi"
    echo "installed $ASI_DIR/MelePause.asi"
fi
