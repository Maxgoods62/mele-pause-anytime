#pragma once

#include "Common/Base.hpp"

#include <atomic>
#include <span>
#include <string>

// What Pause.cpp, Overlay.cpp, Bink.cpp and MovieSound.cpp ask of each other. No game class here: the last three include
// only this file, so they build the same against every game's SDK.
namespace MelePause
{
    /// Where a game's fullscreen movies keep their sound (game/LE<n>.hpp's Game::Movies). Bink: in the .bik, which BinkPause
    /// stops (LE1). BinkAndEvent: mostly in a Wwise event that a Kismet BioSeqAct_MovieBink posts beside the picture, which
    /// MovieSound pauses beside BinkPause; only a few movies carry Bink audio (LE2).
    enum class MoviePolicy : unsigned char { Bink, BinkAndEvent };

    // ! Pause.cpp
    // ========================================

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

    /// After a press we took, the skips Esc is bound to are dropped until its release, for 2 s at most (a release lost to a
    /// focus change must not keep Space's SkipMovie away). Bink.cpp keeps the guard armed as long for a press it read.
    inline constexpr ULONGLONG GuardMs = 2000;

    // ! Overlay.cpp
    // ========================================

    namespace Overlay
    {
        /// Hooks the swap chain's Present, Present1 and ResizeBuffers: the pause screen is drawn into the back buffer. Without
        /// them (logged) Pause.cpp draws its Canvas banner.
        void Install(::LESDK::Initializer& Init);

        /// Game thread, each frame and at once when photo mode starts: the world pause screen shows (the HUD drawing), with
        /// photo mode's key where it opens; or it goes at once, with no fade (photo mode, the console).
        void Publish(bool worldPause, bool offerPhoto, bool hideNow);

        /// The keys named are a gamepad's (A, Y) or the keyboard's (Space, F7): whichever pressed one of ours last. Any thread.
        void UsePadPrompts(bool pad);

        /// A movie's thread, after a BinkWait that kept the movie paused: a paused movie presents nothing, so the pause
        /// screen is presented from here over its last frame (60 a second at most). Only on the thread that presents.
        void MovieIdle();

        /// The pause screen started on the game's swap chain and hasn't failed: Pause.cpp's Canvas banner stays off.
        bool Drawing();
    }

    // ! Bink.cpp
    // ========================================

    namespace Bink
    {
        /// Hooks the Bink 2 exports. A missing module or export is logged and turns the movie pause off; the world pause works
        /// without it. Under BinkAndEvent a fullscreen movie takes the pause keys only if we know its sound: the Wwise event
        /// of the Kismet movie action latched for it (MovieSound), or its own Bink audio (<paramref name="binkAudio"/>: base
        /// names, no folder or .bik). Any other keeps vanilla Esc and Start. A localized movie's file adds one of
        /// <paramref name="languages"/> to the action's name (Opening_SunMid_INT.bik for Opening_SunMid).
        void Install(::LESDK::Initializer& Init, MoviePolicy policy, std::span<char const* const> binkAudio,
                     std::span<char const* const> languages);

        /// Movies opened (or first seen) since Install and not closed yet.
        int OpenMovies();

        /// Of those, the ones opened from a file: the only ones the movie pause takes (loading screens play from memory).
        int FileMovies();

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

        /// The same for the space bar, which the movie's thread reads too but only to resume our movie pause.
        ULONGLONG SpaceSeenDown();

        /// Game thread (SkipMovie, the HUD drawing again, a fault): every movie we paused resumes at its next call.
        void RequestResume(char const* why);
    }

    // ! MovieInput.cpp
    // ========================================

    namespace MovieInput
    {
        /// Hooks the engine's movie input (the movie player's native InputKey, at <paramref name="rva"/> of the game's exe,
        /// once its first bytes are <paramref name="start"/>), which takes a press ahead of Console.InputKey and skips a
        /// movie the game lets skip on it. Esc and Start then reach Console.InputKey while the pause keys are the movies'
        /// (Bink::OwnsKeys), no press skips a movie under our pause, and Space and X skip as Enter does. Nothing for an rva
        /// of 0; another build of the game gets a warning, not a patch.
        void Install(::LESDK::Initializer& Init, unsigned long long rva, std::span<unsigned char const> start);
    }

    // ! MovieSound.cpp
    // ========================================

    namespace MovieSound
    {
        /// Under BinkAndEvent only (Bink::Install): finds AK::SoundEngine::ExecuteActionOnEvent and GetIDFromString among the
        /// game exe's exports. A missing one is logged; without ExecuteActionOnEvent no movie has a known sound.
        void Install();

        /// ExecuteActionOnEvent was found.
        bool Ready();

        /// What the game thread latched at the latest BioSeqAct_MovieBink.Activated: the movie that action plays (its
        /// m_sMovieName), the id of the Wwise event it posts beside it (0: none), and whether its Kismet runs more beside
        /// the movie than that event (an op on its Out or HasStarted output, or the Matinee that starts it): then its sound
        /// is not known. An empty Movie: nothing latched.
        struct Latched
        {
            std::string Movie;
            unsigned Id = 0;
            bool Beside = false;
        };

        /// Game thread, at BioSeqAct_MovieBink.Activated.
        void Latch(std::string movie, unsigned id, bool beside);

        /// Game thread, at BioSeqAct_MovieBink.Deactivated: clears the latch if it is that movie's. False when it was not
        /// (another movie's latch is kept, or nothing was latched).
        bool Unlatch(std::string const& movie);

        /// Any thread: a copy of the latch.
        Latched Current();

        /// Any thread, for the logs: GetIDFromString of the name (Wwise's ids are the FNV-1 hash of the lowercased name),
        /// 0 without the export.
        unsigned Hash(char const* name);

        /// Any thread: pauses or resumes, at once (no fade), what the event's Play actions play, on every game object. Wwise
        /// applies it at its next audio frame. Returns the AKRESULT (1 is AK_Success), or 0 without the export. Calls no
        /// ProcessEvent: safe on a movie's thread.
        int Set(unsigned id, bool pause);
    }
}
