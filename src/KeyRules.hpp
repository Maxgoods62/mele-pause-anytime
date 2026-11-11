#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

// The key decisions that two threads make about one press, as plain functions of what each thread read. The movie's
// thread reads Esc and Space in its BinkWait polls (Bink.cpp). The game thread gets the same press later, in the engine's
// movie input (MovieInput.cpp), then in Console.InputKey and the skip guard (Pause.cpp). A game run can't choose which
// thread sees a press first: v18's logs had the movie's thread 4 to 5 ms ahead in 19 of 20 Space resumes in LE2, and
// behind in all 15 in LE3 (docs/testing.md). So tests/KeyRulesTest.cpp runs these with the ticks of each order, on every
// build (../build.sh).
// No game, SDK or Windows type here, and no state of its own. The callers read their atomics and pass the values in.
// Bink.cpp's queries come in as callables, called only as far as a rule needs them and in the order its comment gives:
// Bink::OwnsKeys takes Bink.cpp's lock and drops a stale pause, so it is never read ahead, and Space's tick is read after
// the pause (MeetMoviePause), which the movie's thread ends after it stores the tick (ResumeOnPress). Logging stays with
// the callers.
namespace PauseAnytime::KeyRules
{
    // ! Times
    // ========================================

    /// Api.hpp's GuardMs and ReleaseGraceMs (Pause.cpp checks that they agree): the skip guard holds while a press we took
    /// is down, this long at most...
    inline constexpr std::uint64_t GuardMs = 2000;
    /// ...and this long after its release.
    inline constexpr std::uint64_t ReleaseGraceMs = 250;

    /// A gap of more than this in the game thread's ProcessEvent calls is a stall: Pause.cpp notes the last call before the
    /// gap and the first after it (Stall).
    inline constexpr std::uint64_t GameStallMs = 250;

    /// A press of Start with no event for this long is over, its release lost (a focus change). A held pad button repeats
    /// after 0.2 s, then every 0.1 s: InitialButtonRepeatDelay and ButtonRepeatDelay under [Engine.Client] in LE1's and
    /// LE2's BIOEngine.ini (LE3's settings were not read).
    inline constexpr std::uint64_t StartStaleMs = 500;

    /// Bink.cpp's WaitStaleMs, DebounceMs and EscHeldMaxMs (Bink.cpp checks that they agree): polls further apart than this
    /// start over...
    inline constexpr std::uint64_t PollGapMs = 500;
    /// ...a press this soon after the last one acted on is a bounce...
    inline constexpr std::uint64_t DebounceMs = 150;
    /// ...and a press acted on keeps its down tick fresh while it is down, this long at most.
    inline constexpr std::uint64_t HeldMaxMs = GuardMs;

    /// A tick written on another thread may be a little ahead of ours.
    constexpr std::uint64_t Since(std::uint64_t now, std::uint64_t then) { return now > then ? now - then : 0; }

    // ! A key on the game thread (Pause.cpp)
    // ========================================

    /// Whose press of a key is down, as Console.InputKey saw it.
    enum class Hold : unsigned char
    {
        /// No press we track: the key is up, or its press is not ours (Start alone tracks one it left to the game).
        None,
        /// A press we acted on: its repeats and its release are ours too.
        Ours,
        /// Start only: a press seen here and left to the game. Its repeats and its release stay the game's, also once a
        /// fullscreen movie has started under it.
        Vanilla,
    };

    /// A key's press as Console.InputKey sees it. Game thread only.
    struct KeyState
    {
        /// Changed by SetHold only.
        Hold Held = Hold::None;
        /// When the press became ours: the skip guard counts from it.
        std::uint64_t PressedAt = 0;
        std::uint64_t ReleasedAt = 0;
        /// Start only: its latest event, of any kind.
        std::uint64_t LastEventAt = 0;
    };

    /// The one change of KeyState::Held. A press that becomes ours is stamped with <paramref name="now"/>.
    constexpr void SetHold(KeyState& k, Hold held, std::uint64_t now)
    {
        k.Held = held;
        if (held == Hold::Ours) k.PressedAt = now;
    }

    /// A press of that key we acted on is down (GuardMs at most), or was released a moment ago (ReleaseGraceMs).
    constexpr bool Guarding(KeyState const& k, std::uint64_t now)
    {
        if (k.Held == Hold::Ours) return now - k.PressedAt < GuardMs;
        return k.ReleasedAt != 0 && now - k.ReleasedAt < ReleaseGraceMs;
    }

    /// A key whose press arms the skip guard (Pause.cpp's KeyGuard): a key of ours that is down or was released a moment
    /// ago, or an Esc or Space press the movie's thread acted on. Each is one bit of Guards.
    enum class Guard : unsigned char { Esc = 1, Start = 2, Space = 4, A = 8 };

    /// The keys that arm the guard now, as bits of Guard: 0 when none does. Several can: Start still down, or just
    /// released, when A resumes.
    using Guards = unsigned char;

    constexpr Guards Bit(Guard guard) { return static_cast<Guards>(guard); }

    constexpr bool Has(Guards guards, Guard guard) { return (guards & Bit(guard)) != 0; }

    /// For the logs: "Esc", "Start+A", or "none".
    inline std::string GuardsText(Guards guards)
    {
        std::string text;
        for (auto const [guard, name] : { std::pair{ Guard::Esc, "Esc" }, std::pair{ Guard::Start, "Start" }, std::pair{ Guard::Space, "Space" },
                                          std::pair{ Guard::A, "A" } })
        {
            if (!Has(guards, guard)) continue;
            if (!text.empty()) text += '+';
            text += name;
        }
        return text.empty() ? "none" : text;
    }

    /// A GuiKey event a game does not have.
    inline constexpr unsigned char NoGuiEvent = 0xFF;

    /// The GuiKey events the skip guard knows, per game (game/LE<n>.hpp): the always-on Escape and Start binds', and the
    /// events a resume key's own press sends to a minigame's board (NoGuiEvent where the game sends none).
    struct GuiEvents
    {
        unsigned char Start = NoGuiEvent;
        unsigned char Escape = NoGuiEvent;
        unsigned char StartRelease = NoGuiEvent;
        unsigned char EscapeRelease = NoGuiEvent;
        unsigned char A = NoGuiEvent;
        unsigned char ARelease = NoGuiEvent;
        unsigned char Space = NoGuiEvent;
        unsigned char SpaceRelease = NoGuiEvent;
    };

    /// Pause.cpp's DropSkip, for a GuiKey <paramref name="event"/> on a game mode while <paramref name="guards"/> hold. Any
    /// press of ours drops the Escape and Start binds' events, as before. A minigame's board (<paramref name="inMinigame"/>:
    /// the GUI mode with a minigame panel open) also loses the events of A and Space while that key's own press arms the
    /// guard: should the press that resumed get past Console.InputKey's TRUE, it would pick a node or a code, or move a
    /// ring, the moment the world runs again. Another key's guard keeps them: an A after an Esc resume is the player's
    /// move. Anywhere else those events stay the game's: an A in a conversation picks a reply.
    constexpr bool DropGuiKey(unsigned char event, Guards guards, bool inMinigame, GuiEvents const& e)
    {
        if (guards == 0 || event == NoGuiEvent) return false;
        if (event == e.Start || event == e.Escape || event == e.StartRelease || event == e.EscapeRelease) return true;
        if (!inMinigame) return false;
        if (Has(guards, Guard::A) && (event == e.A || event == e.ARelease)) return true;
        return Has(guards, Guard::Space) && (event == e.Space || event == e.SpaceRelease);
    }

    /// Pause.cpp's DropSkip for an exec function the space bar's bind runs on a minigame's board (game/LE<n>.hpp's
    /// MinigameSpaceExecs, <paramref name="spaceExec"/>): dropped as DropGuiKey drops Space's GuiKey.
    constexpr bool DropSpaceExec(bool spaceExec, Guards guards, bool inMinigame)
    {
        return spaceExec && Has(guards, Guard::Space) && inMinigame;
    }

    /// Start's press, ours or the game's, is over with its release lost: no event of it for StartStaleMs, counted from the
    /// end of a game-thread stall (<paramref name="stallEndedAt"/>) when one ended after its last event. The pad is read
    /// once a frame, so a stall holds its repeats back too.
    constexpr bool StartStale(KeyState const& k, std::uint64_t now, std::uint64_t stallEndedAt)
    {
        auto const from = k.LastEventAt > stallEndedAt ? k.LastEventAt : stallEndedAt;
        return k.Held != Hold::None && now - from > StartStaleMs;
    }

    // ! A press the movie's thread acted on
    // ========================================

    /// The game thread's latest stall: its last ProcessEvent before a gap of over GameStallMs, and its first after it
    /// (Pause.cpp's G.StallStartedAt and G.StallEndedAt). 0 before the first.
    struct Stall
    {
        std::uint64_t StartedAt = 0;
        std::uint64_t EndedAt = 0;
    };

    struct Lately
    {
        bool Taken = false;
        /// Taken only because the game thread stalled since the press: that stall's start, for the caller's log line. 0
        /// otherwise.
        std::uint64_t StallStart = 0;
        /// That stall's length, so far when it is still on.
        std::uint64_t StallLength = 0;
    };

    /// The press behind <paramref name="tick"/> (Bink.cpp's down tick of Esc or Space, written on the movie's thread, so it
    /// can be newer than <paramref name="now"/>) is down, or was a moment ago. Or the movie's thread acted on it while the
    /// game thread read no input: then the key's queued events reach the game thread only after the gap, and they are still
    /// that press. The gap may still be on, since a native (the movie input) runs before the first ProcessEvent after it:
    /// its start is the game thread's last ProcessEvent, <paramref name="beat"/>. Or it ended a moment ago (<paramref
    /// name="stall"/>). A tick from before the gap began is an earlier press. The logs read so far show no such case: their
    /// stalls were all under loading movies, which never take the keys.
    constexpr Lately TakenLately(std::uint64_t tick, std::uint64_t now, std::uint64_t beat, Stall stall)
    {
        if (tick == 0) return {};
        if (tick >= now || now - tick < ReleaseGraceMs) return { true, 0, 0 };
        auto const ongoing = beat != 0 && now > beat && now - beat > GameStallMs;
        auto const ended = stall.EndedAt != 0 && stall.EndedAt <= now && now - stall.EndedAt < ReleaseGraceMs;
        auto const start = ongoing ? beat : ended ? stall.StartedAt : 0;
        if (start == 0 || tick < start) return {};
        return { true, start, (ongoing ? now : stall.EndedAt) - start };
    }

    // ! A key in the movie's polls (Bink.cpp)
    // ========================================

    /// A key as the BinkWait polls see it, on whichever movie's thread: under Bink.cpp's Lock.
    struct Polled
    {
        bool Down = false;
        std::uint64_t PolledAt = 0;
        /// The last press acted on (or refused): the debounce counts from it.
        std::uint64_t PressedAt = 0;
        /// The press that is down toggled or resumed the movie pause: Bink.cpp sets it once it acts.
        bool Taken = false;
    };

    struct Edge
    {
        /// A press to act on.
        bool Pressed = false;
        /// A press acted on (Taken) is still down, HeldMaxMs at most: the caller stores the poll's tick as the key's down
        /// tick, which TakenLately reads on the game thread.
        bool KeepFresh = false;
    };

    /// One poll of a key. A press to act on is the up->down edge, within DebounceMs of no other. Not after a gap of PollGapMs
    /// in the polls, since no poll saw a key already down then go down, unless our movie pause is on (<paramref
    /// name="wanted"/>): a paused player may call BinkWait seldom, and a press it saw late can only resume (at worst a
    /// resume not asked).
    constexpr Edge PollEdge(Polled& k, bool down, std::uint64_t now, bool wanted)
    {
        auto const edge = down && !k.Down && k.PolledAt != 0 && (wanted || Since(now, k.PolledAt) < PollGapMs);
        k.Down = down;
        k.PolledAt = now;
        Edge e;
        if (!down) k.Taken = false;
        else if (k.Taken && Since(now, k.PressedAt) < HeldMaxMs) e.KeepFresh = true;
        if (!edge || (k.PressedAt != 0 && Since(now, k.PressedAt) < DebounceMs)) return e;
        k.PressedAt = now;
        e.Pressed = true;
        return e;
    }

    /// Bink.cpp's resume of our movie pause on a press the poll or the toggle took: <paramref name="takePress"/> marks it
    /// Taken and stores its down tick, then <paramref name="resume"/> ends the pause (ResumeLocked, whose release store of
    /// Wanted publishes the tick). The game thread reads the pause, then the tick (MeetMoviePause), so a pause it sees over
    /// comes with this press's tick. The other way round it could see the pause over with the tick of an earlier press, and
    /// give this one to the game: v18's Space resume in LE2, before its fix, reached the movie input as Enter.
    template <class TakePress, class Resume>
    void ResumeOnPress(TakePress takePress, Resume resume)
    {
        takePress();
        resume();
    }

    // ! A press on the game thread under the movie pause
    // ========================================

    /// What our movie pause makes of a press on the game thread.
    enum class MoviePress : unsigned char
    {
        /// Nothing: the press goes on to the other rules.
        None,
        /// The pause is on and the movies own the pause keys: the press is ours, and a resume key resumes the pause.
        Paused,
        /// Space only: the movie's thread resumed the movie on this very press a moment ago, so here the pause can read as
        /// over already.
        Resumed,
    };

    /// Pause.cpp's TakeResumeKey and MovieInput.cpp's hook. The callables are called in this order, each only when needed:
    /// <paramref name="pauseWanted"/> (Bink::PauseWanted), <paramref name="ownsKeys"/> (Bink::OwnsKeys) and <paramref
    /// name="spaceTaken"/> (TakenLately of Bink::SpaceSeenDown). The tick after the pause: the movie's thread stores it
    /// before the release store that ends the pause on a Space press, so a pause that press ended is read here with its
    /// tick. Read before the pause, the tick could be from before that press, and the press could go to the game (v18, before
    /// its Space fix).
    template <class Wanted, class Owns, class SpaceTaken>
    MoviePress MeetMoviePause(bool space, Wanted pauseWanted, Owns ownsKeys, SpaceTaken spaceTaken)
    {
        if (pauseWanted() && ownsKeys()) return MoviePress::Paused;
        if (space && spaceTaken()) return MoviePress::Resumed;
        return MoviePress::None;
    }

    /// The keys the movie input hook tells apart: Pause is Esc or a gamepad's Start, X a gamepad's X.
    enum class MovieKey : unsigned char { Other, Pause, Space, X };

    /// Where the hook sends a press.
    enum class MovieKeyGoes : unsigned char
    {
        /// To the movie input as it came, with no log line.
        AsItCame,
        /// Kept from it: our movie pause is on (any key).
        KeptPaused,
        /// Kept from it: Space, which the movie's thread has just resumed the movie on (MoviePress::Resumed).
        KeptResumed,
        /// Kept from it: Esc or Start while the movies own the pause keys. Console.InputKey gets the press.
        KeptPauseKey,
        /// To it as it came, logged: Space or X during a movie, where they are its keys already (LE3).
        Skip,
        /// To it as the game's skip key, logged: Space or X during a movie (LE2: as Enter, so they skip what Esc skipped).
        SkipAs,
    };

    /// MovieInput.cpp's hook, for a press of <paramref name="key"/>. <paramref name="skipKeyAs"/>: the game names a key for
    /// Space and X to reach the movie input as. The callables as MeetMoviePause calls them, with <paramref
    /// name="pauseWanted"/> once more ahead of it for a key other than Esc, Start, Space and X, then <paramref
    /// name="ownsKeys"/> again for Esc and Start, and <paramref name="openMovies"/> (Bink::OpenMovies) last.
    template <class Wanted, class Owns, class SpaceTaken, class Open>
    MovieKeyGoes MovieInputPress(MovieKey key, bool skipKeyAs, Wanted pauseWanted, Owns ownsKeys, SpaceTaken spaceTaken, Open openMovies)
    {
        auto const pauseKey = key == MovieKey::Pause;
        auto const skipKey = key == MovieKey::Space || key == MovieKey::X;
        if (!pauseKey && !skipKey && !pauseWanted()) return MovieKeyGoes::AsItCame;
        switch (MeetMoviePause(key == MovieKey::Space, pauseWanted, ownsKeys, spaceTaken))
        {
        case MoviePress::Paused: return MovieKeyGoes::KeptPaused;
        case MoviePress::Resumed: return MovieKeyGoes::KeptResumed;
        case MoviePress::None: break;
        }
        if (pauseKey) return ownsKeys() ? MovieKeyGoes::KeptPauseKey : MovieKeyGoes::AsItCame;
        // With no movie open there is nothing to skip.
        if (!skipKey || openMovies() == 0) return MovieKeyGoes::AsItCame;
        return skipKeyAs ? MovieKeyGoes::SkipAs : MovieKeyGoes::Skip;
    }
}
