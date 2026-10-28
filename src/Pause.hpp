#pragma once

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>
#include "Common/Base.hpp"

#include <atomic>

namespace MelePause
{
    // ! Pause.cpp
    // ========================================

    /// The SDK's globals, and GEngine: its first LocalPlayer is how a key finds the player's controller.
    void InitializeGlobals(::LESDK::Initializer& Init);

    /// ProcessEvent: Esc and a gamepad's Start in Console.InputKey, the banner after BioHUD.PostRender, and the skips Esc
    /// is bound to while our key is down.
    void InstallHooks(::LESDK::Initializer& Init);

    /// The thread that runs BioHUD.PostRender and Console.InputKey (0 until one ran): Bink.cpp's logs say whether a Bink call
    /// is on it.
    DWORD GameThread();

    /// GetTickCount64 at the game thread's latest ProcessEvent (0 until GameThread is known). For Bink.cpp's movie reports
    /// only: does the game thread still run under a fullscreen movie, paused or not?
    ULONGLONG GameThreadSeen();

    /// GetTickCount64 at the end of the latest BioHUD.PostRender (0 until one ran). The HUD draws every frame in play and
    /// not at all over a fullscreen movie: Bink.cpp only gives Esc to a movie while it is silent.
    ULONGLONG HudSeen();

    /// The HUD is silent when BioHUD.PostRender hasn't run for this long: in play it runs every frame, hitches included.
    /// A longer gap also starts a new run of HUD frames (Pause.cpp lifts a movie pause once the HUD draws again).
    inline constexpr ULONGLONG HudSilentMs = 250;

    // ! Bink.cpp
    // ========================================

    namespace Bink
    {
        /// Hooks the Bink 2 exports. A missing module or export is logged and turns the movie pause off; the world pause works
        /// without it.
        void Install(::LESDK::Initializer& Init);

        /// Movies opened (or first seen) since Install and not closed yet.
        int OpenMovies();

        /// Bumped at each BinkOpen: Pause.cpp logs once per movie whether the game thread still runs input and the HUD.
        unsigned MovieSerial();

        /// Esc or Start asked for the movie pause and nothing ended it since (a pause key again, close, SkipMovie, the HUD drawing again, its
        /// movie gone silent). Whether the movie's thread has applied it yet is in the log.
        bool PauseWanted();

        /// The pause keys (Esc, a gamepad's Start) belong to the movies now: a fullscreen movie plays (opened from a file,
        /// calling BinkWait, the HUD silent) or our movie pause is on and its movie made a Bink call in the last 2 s (an
        /// older one is dropped here). The movie's thread reads Esc itself and toggles the pause; Console.InputKey only keeps
        /// Esc from the game then, and toggles through Toggle on what reaches it of Start. False without BinkPause and the
        /// hooks.
        bool OwnsKeys();

        /// Game thread: a press of Start during a fullscreen movie, as Esc's press is on the movie's thread. Resumes what we
        /// paused, else asks the pause of the newest fullscreen movie, applied at its next BinkWait. False (logged, with
        /// <paramref name="what"/>) when there was nothing to toggle.
        bool Toggle(char const* what);

        /// GetTickCount64 at the latest BinkWait that saw down a press of Esc the movie's thread acted on (0 if none). Kept
        /// fresh while that press is down, 2 s at most: Pause.cpp drops the skips Esc is bound to until a moment after.
        ULONGLONG EscSeenDown();

        /// Game thread (SkipMovie, the HUD drawing again, a fault): every movie we paused resumes at its next call.
        void RequestResume(char const* why);
    }
}
