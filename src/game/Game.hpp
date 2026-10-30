#pragma once

#include <LESDK/Headers.hpp>
#include "MelePause/Api.hpp"

#include <array>

// What differs from one game to the next, picked by the SDK target this build is for. Each game's header holds the same
// names (namespace MelePause::Game) and two switches: MELEPAUSE_PHOTO (photo mode under the pause) and MELEPAUSE_MOVIES
// (the Bink movie pause).
namespace MelePause::Game
{
    /// What Game::MovieActionOf reads of a Kismet movie action (BioSeqAct_MovieBink), for MovieSound's latch.
    struct MovieAction
    {
        /// The movie it plays (m_sMovieName).
        FString const* Movie = nullptr;
        /// The Wwise event it posts beside the picture, and that event's id: null and 0 when none.
        UObject* Event = nullptr;
        unsigned EventId = 0;
        /// What else its Kismet runs beside the movie, which the event's pause would not hold: an op that may make a sound,
        /// started from its Out or HasStarted output (Complete and Failed fire at the end) or at the moment the movie is
        /// started, or the Matinee or conversation line whose event starts it. Null when nothing; Via says which: the
        /// output's name ("Out", "HasStarted"), "Beside", "Matinee" or "Conversation" (game/LE<n>.hpp MovieActionOf).
        UObject* Beside = nullptr;
        std::wstring Via;
    };
}

#if defined(SDK_TARGET_LE1)
    #include "MelePause/game/LE1.hpp"
#elif defined(SDK_TARGET_LE2)
    #include "MelePause/game/LE2.hpp"
#elif defined(SDK_TARGET_LE3)
    #include "MelePause/game/LE3.hpp"
#else
    #error "MelePause: no game/LE<n>.hpp for this SDK target"
#endif

#if !defined(MELEPAUSE_PHOTO) || !defined(MELEPAUSE_PHOTO_GUI_SOUNDS) || !defined(MELEPAUSE_MOVIES)
    #error "MelePause: game/LE<n>.hpp must define MELEPAUSE_PHOTO, MELEPAUSE_PHOTO_GUI_SOUNDS and MELEPAUSE_MOVIES"
#endif
