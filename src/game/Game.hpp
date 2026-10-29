#pragma once

#include <LESDK/Headers.hpp>

// What differs from one game to the next, picked by the SDK target this build is for. Each game's header holds the same
// names (namespace MelePause::Game) and two switches: MELEPAUSE_PHOTO (photo mode under the pause) and MELEPAUSE_MOVIES
// (the Bink movie pause).
#if defined(SDK_TARGET_LE1)
    #include "MelePause/game/LE1.hpp"
#elif defined(SDK_TARGET_LE2)
    #include "MelePause/game/LE2.hpp"
#else
    #error "MelePause: no game/LE<n>.hpp for this SDK target"
#endif

#if !defined(MELEPAUSE_PHOTO) || !defined(MELEPAUSE_MOVIES)
    #error "MelePause: game/LE<n>.hpp must define MELEPAUSE_PHOTO and MELEPAUSE_MOVIES"
#endif
