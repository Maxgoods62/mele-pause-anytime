#pragma once

#include "Common/Base.hpp"

#include <atomic>
#include <span>
#include <string>
#include <string_view>

// What Pause.cpp, Overlay.cpp, Bink.cpp and MovieSound.cpp ask of each other. No game class here: the last three include
// only this file, so they build the same against every game's SDK.
namespace PauseAnytime
{
    /// Where a game's fullscreen movies keep their sound (game/LE<n>.hpp's Game::Movies). Bink: in the .bik, which BinkPause
    /// stops (LE1). BinkAndEvent: mostly in a Wwise event that a Kismet BioSeqAct_MovieBink posts beside the picture, which
    /// MovieSound pauses beside BinkPause; only a few movies carry Bink audio (LE2, LE3).
    enum class MoviePolicy : unsigned char { Bink, BinkAndEvent };

    /// How a game's pause screen looks (game/LE<n>.hpp's Game::Look): the colours of that game's menus, the title on a band,
    /// blue or brown, each key left of its label as the game draws its buttons, the blur a little cooler or warmer.
    enum class OverlayLook : unsigned char { LE1, LE2, LE3 };

    /// The pause screen's words in a game's string table (game/LE<n>.hpp's PauseTexts). 0: that word stays ours.
    struct PauseTextIds
    {
        int Title;
        int Resume;
        int Photo;
    };

    /// The textures of a gamepad's A and Y, right under a package (game/LE<n>.hpp's PauseIcons).
    struct PauseIconNames
    {
        wchar_t const* Package;
        wchar_t const* PadA;
        wchar_t const* PadY;
    };

    /// How to get from a texture to its D3D11 view in one build of a game's exe (game/LE<n>.hpp's TextureView), read from a
    /// memory dump of the running game. UTexture.Resource (its method table at ResourceTableRva) holds at Rhi, unaligned, a
    /// pointer into the RHI texture: Base past the start of that object (its method table at RhiTableRva), and View before
    /// the view.
    struct TextureViewFields
    {
        unsigned long long ResourceTableRva;
        unsigned Rhi;
        unsigned long long RhiTableRva;
        unsigned Base;
        unsigned View;
    };

    /// A game's two GUI fonts, the light one and the bold one, as GFx movies right under a package hold them
    /// (game/LE<n>.hpp's PauseFonts): each font's movie, and its name in that movie. LoadPath: the ObjectReferencer that brings
    /// the movies back through the game's loader, where the game doesn't keep them in memory, or null.
    struct PauseFontNames
    {
        wchar_t const* Package;
        wchar_t const* LightMovie;
        char const* LightFont;
        wchar_t const* BoldMovie;
        char const* BoldFont;
        wchar_t const* LoadPath = nullptr;
    };

    /// One of those fonts for Overlay::UseGameFonts: the bytes of its movie, and its name.
    struct GameFontSource
    {
        std::span<unsigned char const> Movie;
        std::string_view Name;
    };

    /// Where a game's fullscreen movie player keeps what Bink.cpp's movie lines show of it (game/LE<n>.hpp's MoviePlayer),
    /// read raw, for the logs only: the exe's RVA of the pointer to the player (GFullScreenMovie), and the offsets in the
    /// player of its flags and of its skippable flag. Rva 0: nothing read.
    struct MoviePlayerFields
    {
        unsigned long long Rva = 0;
        unsigned Flags = 0;
        unsigned Skippable = 0;
    };

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

    /// The skips stay dropped this long after the release of a press we took (GuardMs before it): the OnRelease half of a
    /// bind may run after Console.InputKey has seen the release.
    inline constexpr ULONGLONG ReleaseGraceMs = 250;

    /// The press behind <paramref name="tick"/> (Bink::EscSeenDown, Bink::SpaceSeenDown) is down, or was a moment ago, or
    /// the movie's thread acted on it while the game thread read no input: a gap of over 250 ms in its ProcessEvent calls,
    /// still on or ended a moment ago, after which the key's queued events reach the game thread late. The tick comes from
    /// the movie's thread, so it can be newer than <paramref name="now"/>. Game thread only: it reads Pause.cpp's state.
    bool TakenLately(ULONGLONG tick, ULONGLONG now);

    /// The Steam overlay (PlayerController.bIsExternalUIOpen) or the console is open, as the game thread read it at its
    /// latest tick: keys typed there are theirs, though the foreground window is still the game's. False while the game
    /// thread has been silent for over 250 ms, since what it read may be stale. For Bink.cpp's polls, which read no game
    /// object.
    bool KeysElsewhere();

    // ! Engine.cpp: faults
    // ========================================

    namespace Fault
    {
        /// The __except filter of our guards: takes an access violation, noting where it was raised for Log, and lets any
        /// other exception go on up. Use as __except (Fault::Filter(GetExceptionInformation())). Any thread.
        int Filter(EXCEPTION_POINTERS const* info);

        /// From the __except block of a fault Filter took on this thread: one line, "<what>: access violation reading 0x...,
        /// raised at <module>+0x<offset>", naming whose code it was (Pause Anytime's, the game's or another module's).
        void Log(char const* what);
    }

    // ! Overlay.cpp
    // ========================================

    namespace Overlay
    {
        /// Hooks the swap chain's Present, Present1, ResizeBuffers and ResizeBuffers1: the pause screen is drawn into the back
        /// buffer, in <paramref name="look"/>. Without them (logged) Pause.cpp draws its Canvas banner.
        void Install(::LESDK::Initializer& Init, OverlayLook look);

        /// The title and the two labels in the game's own words, read from its string table (UTF-8). Each one replaces ours
        /// only if the font it is set in has every letter of it (Exo 2 has no Japanese); an empty one keeps ours. The first
        /// call counts, later ones are ignored. Any thread.
        void UseGameTexts(std::string title, std::string resume, std::string photo);

        /// The game's own gamepad buttons, as the views its renderer holds of their textures (ID3D11ShaderResourceView):
        /// pointers read raw from the game, which the render thread checks before it calls them. Both show or neither, and
        /// the drawn buttons stay without them. The first call counts, later ones are ignored. Any thread.
        void UseGameIcons(void* padA, void* padY);

        /// A game's look: its own fonts in place of Exo 2, from the GFx movies that hold them. They are turned into TrueType
        /// here, in memory, with only the letters of the pause screen: call it after UseGameTexts. Both show or neither, and
        /// Exo 2 stays with a movie that isn't one we know (logged). The first good call counts. Game thread, under a guard: the
        /// bytes are read in this call, and not after it.
        void UseGameFonts(GameFontSource light, GameFontSource bold);

        /// Game thread, each frame and at once when photo mode starts: the world pause screen shows (the HUD drawing), with
        /// photo mode's key where it opens; or it goes at once, with no fade (photo mode, the console).
        void Publish(bool worldPause, bool offerPhoto, bool hideNow);

        /// The keys named are a gamepad's (A, Y) or the keyboard's (Space, F7): whichever pressed one of ours last. Any thread.
        void UsePadPrompts(bool pad);

        /// A movie's thread, after a BinkWait that kept the movie paused: a paused movie presents nothing, so the pause
        /// screen is presented from here over its last frame (60 a second at most). Only on the thread that presents.
        void MovieIdle();

        /// The pause screen started on the game's swap chain, hasn't failed, and no other swap chain waits to replace it (one
        /// that presents while ours has been silent for 250 ms): Pause.cpp's Canvas banner stays off.
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
        /// <paramref name="languages"/> to the action's name (Opening_SunMid_INT.bik for Opening_SunMid). The movie lines add
        /// the movie player's fields that <paramref name="player"/> names.
        void Install(::LESDK::Initializer& Init, MoviePolicy policy, std::span<char const* const> binkAudio,
                     std::span<char const* const> languages, MoviePlayerFields player);

        /// Movies opened (or first seen) since Install and not closed yet.
        int OpenMovies();

        /// Of those, the ones Esc could pause once the HUD goes silent: opened from a file (LE1's and LE2's loading screens
        /// play from memory) and, under BinkAndEvent, with a known sound (LE3's loading screens have none); and any our
        /// pause holds. Overlay.cpp keeps a frame only for these. 0
        /// without BinkPause and the hooks.
        int PausableMovies();

        /// Bumped at each BinkOpen: Pause.cpp logs once per movie whether the game thread still runs input and the HUD.
        unsigned MovieSerial();

        /// Esc or Start asked for the movie pause and nothing ended it since (a pause key again, close, SkipMovie, the HUD drawing again, its
        /// movie gone silent, Wwise refusing to pause its sound). Whether the movie's thread has applied it yet is in the log.
        bool PauseWanted();

        /// The pause keys (Esc, a gamepad's Start) belong to the movies now: a fullscreen movie plays (opened from a file,
        /// calling BinkWait, the HUD silent) or our movie pause is on and its movie made a Bink call in the last 2 s (an
        /// older one is dropped here). The movie's thread reads Esc itself and toggles the pause; Console.InputKey only keeps
        /// Esc from the game then, and toggles through Toggle on what reaches it of Start. False without BinkPause and the
        /// hooks.
        bool OwnsKeys();

        /// Game thread: a press of Start during a fullscreen movie, as Esc's press is on the movie's thread. Resumes what we
        /// paused, else asks the pause of the newest fullscreen movie, applied at its next BinkWait. False (logged, with
        /// <paramref name="what"/>) when there was nothing to toggle. With <paramref name="pressedBy"/> (a GetTickCount64 the
        /// press came by), a newest movie opened after it is not paused (logged): the press may be older than that movie.
        bool Toggle(char const* what, ULONGLONG pressedBy = 0);

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
        /// (Bink::OwnsKeys), and no press skips a movie under our pause. Space and X reach it as the key named
        /// <paramref name="skipKeyAs"/> (LE2: Enter, so they skip what Esc skipped), or as they are when it is null (LE3,
        /// where they are its keys already). Nothing for an rva of 0; another build of the game gets a warning, not a patch.
        void Install(::LESDK::Initializer& Init, unsigned long long rva, std::span<unsigned char const> start, wchar_t const* skipKeyAs);
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

        /// Any thread: bumped at each change of the latch, so a movie's thread can tell that a movie's sound may have changed.
        unsigned Changes();

        /// Any thread, for the logs: GetIDFromString of the name (Wwise's ids are the FNV-1 hash of the lowercased name),
        /// 0 without the export.
        unsigned Hash(char const* name);

        /// Any thread: pauses or resumes, at once (no fade), what the event's Play actions play, on every game object. Wwise
        /// applies it at its next audio frame. Returns the AKRESULT (1 is AK_Success), or 0 without the export. Calls no
        /// ProcessEvent: safe on a movie's thread.
        int Set(unsigned id, bool pause);
    }
}
