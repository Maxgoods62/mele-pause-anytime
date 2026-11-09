#!/usr/bin/env bash
# Build LE<n>PauseAnytime.asi for each game picked with the Visual Studio Build Tools, inside this repo's own checkout
# of ME3Tweaks/LExASIs on the Windows disk (it provides the SDK, the ASI loader interface and the CMake helpers), build
# and run the checks of the rules the threads race for (src/tests), then stage it in MELE_STAGE.
#   ./build.sh                  build and stage the games in MELE_GAMES, else the released ones (scripts/games.sh)
#   ./build.sh --game LE2 ...   build and stage these games instead: LE1, LE2, LE3 or all (the ones src/CMakeLists.txt
#                               lists in GAMES), one --game for each
#   ./build.sh ... --install    also copy each into its game's ASI folder (the games must be closed)
#   ./build.sh ... --uninstall  remove each from its game's ASI folder (the games must be closed)
# Runs in WSL, or in Git Bash on Windows, as the GitHub workflow does. Settings come from the environment or from build.env
# (not in git, see build.env.example); a variable the caller set wins over build.env. All are optional:
#   MELE_GAMES    the games to build when no --game is given, separated by spaces, e.g. 'LE1 LE2' or all
#                 (default: RELEASED_GAMES)
#   MELE_LE_DIR   the Legendary Edition folder, as a Windows path; only --install and --uninstall use it
#                 (default: the install folder in the registry)
#   MELE_VS       the Visual Studio or Build Tools folder with the C++ tools and CMake (default: found with vswhere)
#   MELE_LEXASIS  the LExASIs checkout, cloned on the first build; Dear ImGui goes next to it in third_party. Keep it on
#                 the Windows disk: CMake and MSBuild do not work well on \\wsl.localhost paths
#                 (default: %LOCALAPPDATA%\mele-asi-build\PauseAnytime\LExASIs)
#   MELE_STAGE    where LE<n>PauseAnytime.asi and its .pdb are copied (default: dist/ next to this script)
# Inside the game install, this tool writes and deletes only LE<n>PauseAnytime.asi in Game\ME<n>\Binaries\Win64\ASI. It
# never writes the ASI loader.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The caller's values are put back after build.env, so a test can override one of them.
if [[ -f "$HERE/build.env" ]]; then
    saved="$(declare -p MELE_GAMES MELE_LE_DIR MELE_VS MELE_LEXASIS MELE_STAGE 2>/dev/null || true)"
    . "$HERE/build.env"
    eval "$saved"
fi

LEXASIS_COMMIT=aaad03ab6e5112e27ff5528f7b08c3bcf3870c31
IMGUI_COMMIT=c1aaac65e896a45f81f47a4d68f9959e0f86c859      # v1.93.0

die() { echo "build.sh: $*" >&2; exit 1; }
. "$HERE/scripts/games.sh"

# Git Bash has cygpath in place of wslpath, and would turn MSBuild's /m into a path without MSYS_NO_PATHCONV.
if ! command -v wslpath > /dev/null; then
    wslpath() { cygpath "$@"; }
    export MSYS_NO_PATHCONV=1
fi

USAGE="usage: ./build.sh [--game LE1|LE2|LE3|all]... [--install | --uninstall]"
names=()
action=build
while (( $# > 0 )); do
    case "$1" in
        --game)
            (( $# > 1 )) || die "--game needs LE1, LE2, LE3 or all ($USAGE)"
            names+=("$2")
            shift 2 ;;
        --install|--uninstall)
            [[ "$action" == build ]] || die "--install or --uninstall, not both ($USAGE)"
            action="${1#--}"
            shift ;;
        *) die "unknown argument '$1' ($USAGE)" ;;
    esac
done
pick_games "${names[@]}"

# A Windows environment variable as a WSL path; empty if it is not set. PowerShell prints it in UTF-8 (cmd.exe would use
# the OEM code page and break a non-ASCII user name).
win_env() {
    local v
    v="$(powershell.exe -NoProfile -NonInteractive -Command \
        "[Console]::OutputEncoding = [Text.Encoding]::UTF8; [Environment]::GetEnvironmentVariable('$1')" 2>/dev/null \
        | tr -d '\r' || true)"
    if [[ -n "$v" ]]; then wslpath -u "$v" 2>/dev/null || true; fi
}

# The Legendary Edition folder as a WSL path: MELE_LE_DIR, else the two registry entries LegendaryExplorerCore reads
# (LEDirectory.LookupDefaultPath): "Install Dir" of HKLM\SOFTWARE\BioWare\*Legendary Edition, then Steam's uninstall entry.
le_dir() {
    local win="${MELE_LE_DIR:-}" dir
    if [[ -z "$win" ]]; then
        win="$(reg.exe query 'HKLM\SOFTWARE\BioWare' /s /v 'Install Dir' 2>/dev/null | tr -d '\r' \
            | awk '/^HKEY_/ { le = /Legendary Edition$/ } le && sub(/^ +Install Dir +REG_SZ +/, "") { print; exit }' || true)"
    fi
    if [[ -z "$win" ]]; then
        win="$(reg.exe query 'HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 1328670' /v InstallLocation \
            2>/dev/null | tr -d '\r' | sed -n 's/^ *InstallLocation *REG_SZ *//p' || true)"
    fi
    [[ -n "$win" ]] || die "Legendary Edition not found in the registry: set MELE_LE_DIR to its folder, as a Windows path"
    win="${win%\\}"
    dir="$(wslpath -u "$win" 2>/dev/null || true)"
    [[ -n "$dir" && -d "$dir/Game" ]] || die "no Game folder in '$win': set MELE_LE_DIR to the Legendary Edition folder"
    echo "$dir"
}

# Closed only when tasklist answers and does not list the executable $1: a failed tasklist counts as running.
game_closed() {
    local out
    out="$(tasklist.exe /FO CSV /NH /FI "IMAGENAME eq $1" 2>/dev/null)" || return 1
    ! grep -qiF "$1" <<< "$out"
}

# Every check for every game picked, before anything in the game install changes: the ASI loader and the ASI folder
# (--install only: this tool never writes the loader, and --uninstall has nothing to remove without the folder), and the
# game closed. $1 ends a refusal.
check_games() {
    local g win64
    for g in "${GAMES[@]}"; do
        game_info "$g"
        win64="$LE_DIR/Game/$GAME_ME/Binaries/Win64"
        [[ "$action" != install || -f "$win64/bink2w64_original.dll" ]] \
            || die "no ASI loader in Game\\$GAME_ME: install one with ME3Tweaks Mod Manager first$1"
        [[ "$action" != install || -d "$win64/ASI" ]] || die "no ASI folder in Game\\$GAME_ME\\Binaries\\Win64$1"
        game_closed "$GAME_EXE" || die "$GAME_EXE is running: close the game first$1"
    done
}

# Deletes the file $1 if it is there, and says so.
remove() {
    if [[ -f "$1" ]]; then
        rm -f "$1"
        echo "removed $1"
    fi
}

# The checks come before the build too, so a missing loader or a running game costs no build.
if [[ "$action" != build ]]; then
    LE_DIR="$(le_dir)"
    check_games ""
fi

if [[ "$action" == uninstall ]]; then
    for g in "${GAMES[@]}"; do
        game_info "$g"
        ASI_DIR="$LE_DIR/Game/$GAME_ME/Binaries/Win64/ASI"
        if [[ ! -d "$ASI_DIR" ]]; then echo "no ASI folder in Game\\$GAME_ME\\Binaries\\Win64: nothing to remove"; continue; fi
        [[ -f "$ASI_DIR/$GAME_ASI" ]] || echo "no $GAME_ASI in $ASI_DIR"
        remove "$ASI_DIR/$GAME_ASI"
    done
    exit 0
fi

# Visual Studio 18 (2026) with the C++ x64 tools: LExASIs' preset uses the "Visual Studio 18 2026" generator, which the
# CMake of older versions does not know.
VS="${MELE_VS:-}"
if [[ -z "$VS" ]]; then
    NEED_VS="the Visual Studio 18 (2026) Build Tools with C++ are needed (or set MELE_VS)"
    VSWHERE="$(win_env 'ProgramFiles(x86)')/Microsoft Visual Studio/Installer/vswhere.exe"
    [[ -f "$VSWHERE" ]] || die "no vswhere.exe: $NEED_VS"
    VS="$("$VSWHERE" -latest -products '*' -version '[18.0,19.0)' \
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | tr -d '\r')"
    [[ -n "$VS" ]] || die "vswhere found no Visual Studio 18 (2026) with the C++ x64 tools: $NEED_VS"
    VS="$(wslpath -u "$VS")"
fi
CMAKE="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
[[ -f "$CMAKE" ]] || die "no CMake in $VS: add its \"C++ CMake tools for Windows\" component, or set MELE_VS"

LEXASIS="${MELE_LEXASIS:-}"
if [[ -z "$LEXASIS" ]]; then
    LOCAL="$(win_env LOCALAPPDATA)"
    [[ -n "$LOCAL" ]] || die "no %LOCALAPPDATA% from PowerShell: set MELE_LEXASIS to a folder on the Windows disk"
    LEXASIS="$LOCAL/mele-asi-build/PauseAnytime/LExASIs"
fi
# Dear ImGui (MIT, see THIRD_PARTY.md) for the pause screen, next to the LExASIs checkout.
THIRD_PARTY="$(dirname "$LEXASIS")/third_party"
STAGE="${MELE_STAGE:-$HERE/dist}"

# One build of the tree at a time (two runs, or two worktrees with the same MELE_LEXASIS, would write the same objects
# and PDB). Held until the script exits. Git Bash has no flock, and a GitHub runner builds alone.
mkdir -p "$(dirname "$LEXASIS")"
if command -v flock > /dev/null; then
    exec 9> "$(dirname "$LEXASIS")/.lexasis-build.lock"
    flock -n 9 || { echo "another build of $LEXASIS is running: waiting for it" >&2; flock 9; }
fi

# LExASIs at the pinned commit with all its submodules, checked on every run: an interrupted first clone or a new pin is
# repaired here.
lexasis_git() { git.exe -C "$(wslpath -w "$LEXASIS")" "$@"; }
if [[ ! -d "$LEXASIS/.git" ]]; then
    git.exe clone https://github.com/ME3Tweaks/LExASIs.git "$(wslpath -w "$LEXASIS")"
fi
pinned="$(lexasis_git rev-parse -q --verify "$LEXASIS_COMMIT^{commit}" 2>/dev/null | tr -d '\r' || true)"
current="$(lexasis_git rev-parse -q --verify HEAD 2>/dev/null | tr -d '\r' || true)"
# A submodule line that starts with - is not checked out, + is at another commit, U has a conflict.
subs="$(lexasis_git submodule status --recursive 2>/dev/null)" || subs=-
if [[ -z "$pinned" || "$current" != "$pinned" ]] || grep -q '^[-+U]' <<< "$subs"; then
    echo "checking out LExASIs $LEXASIS_COMMIT and its submodules in $LEXASIS" >&2
    # The project lines our builds add to CMakeLists.txt would block a checkout that changes the file: they go (this
    # script's line is added again below). Any other change is someone's own work: the checkout keeps it, or refuses.
    CML="$LEXASIS/CMakeLists.txt"
    if [[ -f "$CML" ]]; then
        sed -i -e '/^if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}\/\(Mele[A-Za-z]*\|PauseAnytime\)")$/,/^endif ()$/d' \
            -e '/^add_subdirectory ("\(Mele[A-Za-z]*\|PauseAnytime\)")$/d' "$CML"
        lexasis_git diff --quiet -- CMakeLists.txt \
            || die "CMakeLists.txt in $LEXASIS has changes of your own: commit or stash them, then build again"
    fi
    { [[ -n "$pinned" ]] || lexasis_git fetch -q origin; } \
        && lexasis_git checkout -q --recurse-submodules "$LEXASIS_COMMIT" \
        && lexasis_git submodule update -q --init --recursive \
        || die "could not check out LExASIs $LEXASIS_COMMIT in $LEXASIS (git says why above): commit or stash your own changes there; with none, delete that folder and build again"
fi

# A source checkout at an exact commit; fetched again if the pin moved.
fetch_pinned() {
    local url="$1" dir="$2" commit="$3"
    if [[ ! -d "$dir/.git" ]]; then
        mkdir -p "$dir"
        git.exe -C "$(wslpath -w "$dir")" init -q
        git.exe -C "$(wslpath -w "$dir")" remote add origin "$url"
    fi
    if [[ "$(git.exe -C "$(wslpath -w "$dir")" rev-parse HEAD 2>/dev/null | tr -d '\r')" != "$commit" ]]; then
        git.exe -C "$(wslpath -w "$dir")" fetch -q --depth 1 origin "$commit"
        git.exe -C "$(wslpath -w "$dir")" checkout -q --detach FETCH_HEAD
    fi
}
fetch_pinned https://github.com/ocornut/imgui.git "$THIRD_PARTY/imgui" "$IMGUI_COMMIT"

# Our sources become one more ASI project of that tree. The project line is guarded, so the tree still configures if
# LExASIs/PauseAnytime is ever deleted; an unguarded line from an earlier build is replaced.
# By content, not by time: a changed file gets the current time, so MSBuild never keeps an object compiled from another
# checkout's newer copy. Git Bash has no rsync, and a GitHub runner has no older objects to keep.
# The copy takes the working tree, files git doesn't track included, and GitHub's build has only what is committed: a
# local build can pass on a file a commit left out. Said, not refused, since a file is new before it is added.
untracked="$(git -C "$HERE" ls-files --others --exclude-standard -- src 2> /dev/null || true)"
if [[ -n "$untracked" ]]; then
    echo "build.sh: warning: these files in src are not in git, and GitHub's build won't have them (git add them before you commit):" >&2
    sed 's/^/  /' <<< "$untracked" >&2
fi
mkdir -p "$LEXASIS/PauseAnytime"
if command -v rsync > /dev/null; then
    rsync -rlc --delete "$HERE/src/" "$LEXASIS/PauseAnytime/"
else
    rm -rf "$LEXASIS/PauseAnytime"
    cp -r "$HERE/src" "$LEXASIS/PauseAnytime"
fi
CML="$LEXASIS/CMakeLists.txt"
if grep -qx 'add_subdirectory ("PauseAnytime")' "$CML"; then sed -i '/^add_subdirectory ("PauseAnytime")$/d' "$CML"; fi
grep -qF 'EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PauseAnytime"' "$CML" \
    || printf '%s\n' 'if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PauseAnytime")' '  add_subdirectory ("PauseAnytime")' 'endif ()' >> "$CML"

cd "$LEXASIS"
# A LEASI_GAME<n>_WIN64 variable makes the ASI builds of every game turned on copy themselves into that game. env -u
# removes one WSLENV would forward to cmake.exe; one set on the Windows side still reaches it, so the configure output
# is checked too. Every game flag is passed, so a value cached by an earlier configure never sticks.
flags=()
for g in LE1 LE2 LE3; do
    if [[ " ${GAMES[*]} " == *" $g "* ]]; then flags+=("-DLEASI_BUILD_$g=ON"); else flags+=("-DLEASI_BUILD_$g=OFF"); fi
done
log="$(mktemp)"
if ! env -u LEASI_GAME1_WIN64 -u LEASI_GAME2_WIN64 -u LEASI_GAME3_WIN64 -u LEASI_GAMEL_WIN64 "$CMAKE" \
    --preset VisualStudio "${flags[@]}" -DLEASI_BUILD_LEL=OFF \
    "-DMELE_THIRD_PARTY=$(wslpath -m "$THIRD_PARTY")" > "$log" 2>&1; then
    rm -f "$log"
    die "cmake configure failed: run it by hand in $LEXASIS"
fi
if grep -q 'User-provided Mass Effect' "$log"; then
    rm -f "$log"
    die "a LEASI_GAME*_WIN64 variable is set in the Windows environment, and it makes the build copy itself into the game: remove it (System Properties, Environment Variables), then open a new terminal and build again"
fi
# A game gets a project only once src/CMakeLists.txt lists it in GAMES. The solution is checked too: a project of a game
# listed before stays in Build.
for g in "${GAMES[@]}"; do
    if [[ ! -f "Build/PauseAnytime/PauseAnytime-$g.vcxproj" ]] || ! grep -q "PauseAnytime-$g\.vcxproj" Build/LExASIs.slnx; then
        rm -f "$log"
        die "no PauseAnytime-$g project: src/CMakeLists.txt does not build $g yet (asi_target's GAMES)"
    fi
done
# build_target <target> builds it, or prints its errors and warnings and stops.
build_target() {
    if ! "$CMAKE" --build Build --config RELEASE --target "$1" -- /m /v:minimal /nologo > "$log" 2>&1; then
        # The project's path ends each line: it goes first, so the source path's prefix can be cut up to its LExASIs\.
        tr -d '\r' < "$log" | grep -E 'error|warning' | sed -E 's| \[[^]]*vcxproj\]||; s|^.*LExASIs\\||' | sort -u
        rm -f "$log"
        die "$1 did not build"
    fi
}
for g in "${GAMES[@]}"; do build_target "PauseAnytime-$g"; done
# The checks of the rules the threads race for (src/tests: the keys, a movie's pause and its Wwise event, the overlay's
# gate and swap chain), on every build: a game run can't choose which thread gets there first. One failed check stops the
# build before anything is staged, here and in GitHub's build (package.sh).
build_target PauseAnytimeTests
rm -f "$log"
TESTS="Build/RELEASE/PauseAnytimeTests.exe"
[[ -f "$TESTS" ]] || die "no $TESTS after its build"
"./$TESTS" | tr -d '\r' || die "the rules' checks failed (the lines above): nothing staged"
# docs/strings.tsv lists every string id the ASI asks for, with its English text, for translation mods
# (docs/translating.md). It must say what the code says: the test program prints the code's list.
ids_code="$("./$TESTS" --strings | tr -d '\r')" || die "$TESTS --strings failed"
ids_doc="$(tr -d '\r' < "$HERE/docs/strings.tsv" | grep -v -e '^#' -e '^$')" || die "docs/strings.tsv lists no string"
if [[ "$ids_code" != "$ids_doc" ]]; then
    diff <(echo "$ids_doc") <(echo "$ids_code") | sed 's/^/  /'
    die "docs/strings.tsv differs from the code (< the file, > the code): fix it, nothing staged"
fi

mkdir -p "$STAGE"
for g in "${GAMES[@]}"; do
    game_info "$g"
    cp "Build/RELEASE/$GAME_ASI" "Build/RELEASE/${GAME_ASI%.asi}.pdb" "$STAGE/"
    echo "built $STAGE/$GAME_ASI"
done

if [[ "$action" == install ]]; then
    check_games " (built, not installed)"
    for g in "${GAMES[@]}"; do
        game_info "$g"
        ASI_DIR="$LE_DIR/Game/$GAME_ME/Binaries/Win64/ASI"
        cp "$STAGE/$GAME_ASI" "$ASI_DIR/$GAME_ASI"
        echo "installed $ASI_DIR/$GAME_ASI"
    done
fi
