# shellcheck shell=bash
# The games MelePause is built for, and what each one's files are called. Sourced by build.sh and package.sh, which set
# HERE (the repo) and die().

# What a build or a package covers when neither --game nor MELE_GAMES names a game.
RELEASED_GAMES=(LE1 LE2 LE3)

# game_info LE<n> sets GAME_ME (the game's folder under Game), GAME_EXE (its executable), GAME_ASI (our file in its
# Binaries\Win64\ASI folder) and GAME_LEGACY (the file name of v6 and older there, which build.sh deletes; or empty).
game_info() {
    case "$1" in
        LE1|LE2|LE3) ;;
        *) die "unknown game '$1': LE1, LE2, LE3 or all" ;;
    esac
    GAME_ME="ME${1#LE}"
    GAME_EXE="MassEffect${1#LE}.exe"
    GAME_ASI="$1MelePause.asi"
    GAME_LEGACY=""
    if [[ "$1" == LE1 ]]; then GAME_LEGACY="MelePause.asi"; fi
}

# pick_games [NAME...] sets GAMES to the games named (LE1, LE2, LE3, or all: the ones asi_target's GAMES line in
# src/CMakeLists.txt lists), each once and in that order. With no name: MELE_GAMES (names separated by spaces), else
# RELEASED_GAMES.
pick_games() {
    local names=("$@") name g picked=" "
    if (( ${#names[@]} == 0 )); then read -ra names <<< "${MELE_GAMES:-}"; fi
    if (( ${#names[@]} == 0 )); then names=("${RELEASED_GAMES[@]}"); fi
    for name in "${names[@]}"; do
        case "${name^^}" in
            LE1|LE2|LE3) picked+="${name^^} " ;;
            ALL)
                g="$(sed -n 's/^ *GAMES\(\( *"LE[123]"\)*\) *$/\1/p' "$HERE/src/CMakeLists.txt" | tr -d '"')"
                [[ -n "${g// /}" ]] || die "no GAMES line in src/CMakeLists.txt, so no games for 'all'"
                picked+="$g " ;;
            *) die "unknown game '$name': LE1, LE2, LE3 or all" ;;
        esac
    done
    GAMES=()
    for g in LE1 LE2 LE3; do
        if [[ "$picked" == *" $g "* ]]; then GAMES+=("$g"); fi
    done
}

# game_readme LE<n> prints what README.txt in that game's zip says the ASI does (package.sh). A game gets its text when
# it is released.
game_readme() {
    case "$1" in
        LE1) cat <<'EOF'
LE1MelePause.asi is an ASI plugin for Mass Effect 1 in the Legendary Edition (LE1). Esc, or
Start on a gamepad, pauses a conversation, an in-engine cutscene or a fullscreen movie such as
the galaxy map's travel movie, and Esc, Start, Space or A resumes it. While the game is paused,
the picture is blurred and darkened under a pause screen. In a paused conversation or cutscene,
a gamepad's Y or F7 opens the game's photo mode on the paused shot.
EOF
            ;;
        LE2) cat <<'EOF'
LE2MelePause.asi is an ASI plugin for Mass Effect 2 in the Legendary Edition (LE2). Esc, or
Start on a gamepad, pauses a conversation, an in-engine cutscene, or a fullscreen movie whose
sound it knows, such as the mass relay jump, and Esc, Start, Space or A resumes it. While the
game is paused, the picture is blurred and darkened under a pause screen. In a paused
conversation or cutscene, a gamepad's Y or F7 opens the game's photo mode on the paused shot.
In a skippable cutscene or movie, Esc pauses instead of skipping it: Space, or X on a gamepad,
skips.
EOF
            ;;
        LE3) cat <<'EOF'
LE3MelePause.asi is an ASI plugin for Mass Effect 3 in the Legendary Edition (LE3). Esc, or
Start on a gamepad, pauses a conversation or an in-engine cutscene, and Esc, Start, Space or A
resumes it. While the game is paused, the picture is blurred and darkened under a pause screen.
In a skippable cutscene, Esc pauses instead of skipping it: Space, or X on a gamepad, skips.
Fullscreen movies, Y and F7 keep their vanilla jobs.
EOF
            ;;
        *) return 1 ;;
    esac
}
