# shellcheck shell=bash
# The games Pause Anytime is built for, and what each one's files are called. Sourced by build.sh and package.sh, which
# set HERE (the repo) and die().

# What a build or a package covers when neither --game nor MELE_GAMES names a game.
RELEASED_GAMES=(LE1 LE2 LE3)

# game_info LE<n> sets GAME_ME (the game's folder under Game), GAME_EXE (its executable) and GAME_ASI (our file in its
# Binaries\Win64\ASI folder).
game_info() {
    case "$1" in
        LE1|LE2|LE3) ;;
        *) die "unknown game '$1': LE1, LE2, LE3 or all" ;;
    esac
    GAME_ME="ME${1#LE}"
    GAME_EXE="MassEffect${1#LE}.exe"
    GAME_ASI="$1PauseAnytime.asi"
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

# game_readme LE<n> prints the lines of the zip's README.txt that say what differs in that game. package.sh prints them
# under the paragraph that holds for every game. A game gets its lines when it is released.
game_readme() {
    case "$1" in
        LE1) cat <<'EOF'
- LE1: the movies that pause include the galaxy map travel movie. A movie that Esc skips in
  the game without mods is still skipped.
EOF
            ;;
        LE2) cat <<'EOF'
- LE2: the movies that pause include the mass relay jump. In a skippable cutscene, and in a
  movie that Pause Anytime can pause, Esc pauses instead of skipping. Space, or X on a
  gamepad, skips a cutscene or a movie when the game lets you skip it. In other movies Esc
  skips as usual.
EOF
            ;;
        LE3) cat <<'EOF'
- LE3: the movies that pause include the mass relay jump. In a skippable cutscene, Esc pauses
  instead of skipping. Space, or X on a gamepad, still skips the cutscene. In a test, Esc
  during a movie inside a conversation paused the conversation, and the movie played on.
  Other cases are not tested yet.
EOF
            ;;
        *) return 1 ;;
    esac
}
