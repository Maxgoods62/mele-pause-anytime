// The checks of KeyRules.hpp, run by PauseAnytimeTests (Main.cpp) after the ASIs: one failed check fails the build. Each
// case is a press as one thread or the other sees it, with ticks in an order a game run can't choose. They can't show the
// real timing of the threads, nor what the engine and the game do with a key: those stay checks in the game
// (docs/testing.md). The movie's thread's side of a case is what Bink.cpp's Poll does with the edges PollEdge returns
// (the down tick first, then the end of the pause, in ResumeOnPress's order, which PollResumesInOrder checks); the rest of
// Poll is not under test.
#include "PauseAnytime/KeyRules.hpp"
#include "PauseAnytime/tests/Check.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>

namespace
{
    using namespace PauseAnytime::KeyRules;
    using PauseAnytime::Tests::Check;

    /// A tick well past 0, which the rules read as "none".
    constexpr std::uint64_t T = 1000000;

    /// What the game thread reads of the movie code at one press, and the order it reads it in: W Bink::PauseWanted, O
    /// Bink::OwnsKeys, S Bink::SpaceSeenDown (through TakenLately), N Bink::OpenMovies.
    struct Seen
    {
        bool Wanted = false;
        bool Owns = false;
        int Open = 0;
        std::uint64_t SpaceTick = 0;
        /// The game thread's last ProcessEvent, and its last stall.
        std::uint64_t Beat = 0;
        Stall LastStall;
        std::string Reads;
    };

    bool SpaceTaken(Seen& s, std::uint64_t now)
    {
        s.Reads += 'S';
        return TakenLately(s.SpaceTick, now, s.Beat, s.LastStall).Taken;
    }

    /// MovieInput.cpp's hook, with skipKeyAs (LE2: Enter) or without (LE3).
    MovieKeyGoes Hook(Seen& s, MovieKey key, bool skipKeyAs, std::uint64_t now)
    {
        return MovieInputPress(
            key, skipKeyAs, [&] { s.Reads += 'W'; return s.Wanted; }, [&] { s.Reads += 'O'; return s.Owns; }, [&] { return SpaceTaken(s, now); },
            [&] { s.Reads += 'N'; return s.Open; });
    }

    /// Pause.cpp's TakeResumeKey, for Space or A.
    MoviePress Console(Seen& s, bool space, std::uint64_t now)
    {
        return MeetMoviePause(space, [&] { s.Reads += 'W'; return s.Wanted; }, [&] { s.Reads += 'O'; return s.Owns; }, [&] { return SpaceTaken(s, now); });
    }

    /// A key polled up every 4 ms until <paramref name="until"/>, the movie playing.
    void PollUp(Polled& k, std::uint64_t from, std::uint64_t until, bool wanted)
    {
        for (auto t = from; t < until; t += 4) PollEdge(k, false, t, wanted);
    }

    // ! v18: Space resumes a paused movie
    // ========================================

    /// The movie's thread reads the press first and resumes; the game thread gets it <paramref name="late"/> ms later. In LE2
    /// v18's logs had this order in 19 of 20 resumes, 4 to 5 ms apart: before the fix, the hook gave the press to the movie
    /// input as Enter and CitHub_Arrival closed.
    void MovieThreadFirst(bool skipKeyAs, std::uint64_t late)
    {
        Seen s;
        s.Wanted = true;
        s.Owns = true;
        s.Open = 1;
        Polled space;
        PollUp(space, T - 100, T, true);
        auto const edge = PollEdge(space, true, T, true);
        Check(edge.Pressed, "the movie's poll takes Space's press under the pause");
        // Poll: the tick, then the end of the pause.
        space.Taken = true;
        s.SpaceTick = T;
        s.Wanted = false;
        s.Beat = T + late - 1;

        auto const goes = Hook(s, MovieKey::Space, skipKeyAs, T + late);
        Check(goes == MovieKeyGoes::KeptResumed, skipKeyAs ? "LE2, the movie's thread first: Space is kept from the movie input, not given as Enter"
                                                          : "LE3, the movie's thread first: Space is kept from the movie input, which would skip on it");
        Check(s.Reads == "WS", "the hook reads the pause, then Space's tick, and no OwnsKeys once the pause is over");
        s.Reads.clear();
        Check(Console(s, true, T + late) == MoviePress::Resumed, "Console.InputKey takes the same press: the movie's thread resumed on it");
        Check(s.Reads == "WS", "TakeResumeKey reads the pause before Space's tick");

        // Held: each poll keeps the tick fresh, so the game thread's repeats stay ours too.
        for (std::uint64_t t = T + 4; t <= T + 600; t += 4)
        {
            if (PollEdge(space, true, t, false).KeepFresh) s.SpaceTick = t;
        }
        Check(s.SpaceTick == T + 600, "a held Space keeps its tick fresh");
        // The game thread runs each frame meanwhile: no stall.
        s.Beat = T + 601;
        Check(Console(s, true, T + 602) == MoviePress::Resumed, "a repeat 600 ms into the hold is still the press the movie's thread took");
        // Released: the grace, then the key is the game's again.
        PollEdge(space, false, T + 604, false);
        Check(!space.Taken, "the release clears Taken");
        s.Beat = T + 600 + ReleaseGraceMs - 2;
        Check(Console(s, true, T + 600 + ReleaseGraceMs - 1) == MoviePress::Resumed, "just inside the grace after the last fresh tick");
        s.Beat = T + 600 + ReleaseGraceMs - 1;
        Check(Console(s, true, T + 600 + ReleaseGraceMs) == MoviePress::None, "after the grace the key is the game's");
    }

    /// The game thread gets the press while the pause is still on and resumes it; the movie's poll reads the same press
    /// <paramref name="late"/> ms later. LE3's 15 resumes of v18 all came in this order.
    void GameThreadFirst(bool skipKeyAs, std::uint64_t late)
    {
        Seen s;
        s.Wanted = true;
        s.Owns = true;
        s.Open = 1;
        s.Beat = T - 1;
        Polled space;
        PollUp(space, T - 100, T + late, true);

        Check(Hook(s, MovieKey::Space, skipKeyAs, T) == MovieKeyGoes::KeptPaused, "the game thread first: the hook keeps Space under the pause");
        Check(s.Reads == "WO", "under the pause the hook reads no tick");
        s.Reads.clear();
        Check(Console(s, true, T) == MoviePress::Paused, "Console.InputKey resumes the pause on Space");
        Check(s.Reads == "WO", "TakeResumeKey reads no tick under the pause");
        // RequestResume: the pause is over before the movie's thread reads the key.
        s.Wanted = false;

        auto const edge = PollEdge(space, true, T + late, false);
        // Poll acts on Space's press only while its pause is on: with it over, no tick is stored and nothing resumes again.
        Check(edge.Pressed && !edge.KeepFresh, "the movie's poll sees the press, and has no taken press to keep fresh");
        Check(!PollEdge(space, true, T + late + 4, false).KeepFresh, "nor at the next poll: Space was never taken there");
    }

    // ! The movie's resume lands between the game thread's reads
    // ========================================

    /// The movie's thread resumes on the same Space press right before the game thread's read number At (0: before the first,
    /// past the last: after them all), as Poll does it: the tick, then the end of the pause by a release store, so a read
    /// that sees the pause over sees the tick too. OwnsKeys stays true: the movie plays on, fullscreen. Before v18's fix,
    /// TakeResumeKey read the tick ahead of the pause, and a resume landing between the two reads left the press to the game.
    struct Race
    {
        int At = 0;
        int Reads = 0;
        bool Wanted = true;
        std::uint64_t SpaceTick = 0;
        std::string Order;

        void Read(char what)
        {
            if (Reads++ == At)
            {
                SpaceTick = T;
                Wanted = false;
            }
            Order += what;
        }
    };

    void ResumeLandsAnywhere()
    {
        auto const now = T + 4;
        for (int at = 0; at <= 4; at++)
        {
            Race r;
            r.At = at;
            auto const wanted = [&] { r.Read('W'); return r.Wanted; };
            auto const owns = [&] { r.Read('O'); return true; };
            auto const taken = [&] { r.Read('S'); return TakenLately(r.SpaceTick, now, now - 1, {}).Taken; };
            auto const open = [&] { r.Read('N'); return 1; };
            Check(MeetMoviePause(true, wanted, owns, taken) != MoviePress::None, "TakeResumeKey: wherever the movie's resume lands, Space stays ours");
            Check(r.Order[0] == 'W', "TakeResumeKey reads the pause first");
            for (auto const skipKeyAs : { true, false })
            {
                r = Race{};
                r.At = at;
                auto const goes = MovieInputPress(MovieKey::Space, skipKeyAs, wanted, owns, taken, open);
                Check(goes == MovieKeyGoes::KeptPaused || goes == MovieKeyGoes::KeptResumed,
                      "the hook: wherever the movie's resume lands, Space never reaches the movie input");
            }
        }
    }

    /// The movie's thread resumes on a Space press through ResumeOnPress, the tick then the end of the pause, and the game
    /// thread's reads land after none, one or both of the two: wherever they land, the press is ours. With the end of the
    /// pause first, a read between the two would see the pause over with no tick of this press (v18, before its fix).
    void PollResumesInOrder()
    {
        auto const now = T + 4;
        for (int after = 0; after <= 2; after++)
        {
            auto wanted = true;
            std::uint64_t tick = 0;
            auto steps = 0;
            auto hook = MovieKeyGoes::AsItCame;
            auto console = MoviePress::None;
            auto const gameThread = [&] {
                auto const pauseWanted = [&] { return wanted; };
                auto const owns = [] { return true; };
                auto const taken = [&] { return TakenLately(tick, now, now - 1, {}).Taken; };
                hook = MovieInputPress(MovieKey::Space, true, pauseWanted, owns, taken, [] { return 1; });
                console = MeetMoviePause(true, pauseWanted, owns, taken);
            };
            if (after == 0) gameThread();
            ResumeOnPress(
                [&] {
                    tick = T;
                    if (++steps == after) gameThread();
                },
                [&] {
                    wanted = false;
                    if (++steps == after) gameThread();
                });
            Check(hook == MovieKeyGoes::KeptPaused || hook == MovieKeyGoes::KeptResumed, "Poll's resume: Space never reaches LE2's movie input as Enter");
            Check(console != MoviePress::None, "Poll's resume: TakeResumeKey keeps Space ours");
        }
    }

    // ! Space and X with nothing paused
    // ========================================

    void NothingPaused()
    {
        for (auto const skipKeyAs : { true, false })
        {
            Seen s;
            s.Owns = true;
            s.Open = 1;
            s.Beat = T - 1;
            auto const space = Hook(s, MovieKey::Space, skipKeyAs, T);
            Check(space == (skipKeyAs ? MovieKeyGoes::SkipAs : MovieKeyGoes::Skip), "Space with nothing paused and no tick skips as in the game");
            Check(s.Reads == "WSN", "Space with nothing paused: the pause, the tick, the open movies");
            s.Reads.clear();
            auto const x = Hook(s, MovieKey::X, skipKeyAs, T);
            Check(x == (skipKeyAs ? MovieKeyGoes::SkipAs : MovieKeyGoes::Skip), "X with nothing paused skips as in the game");
            Check(s.Reads == "WN", "X reads no Space tick");
            // A press of long ago is no reason to keep this one.
            s.SpaceTick = T - 1000;
            Check(Hook(s, MovieKey::Space, skipKeyAs, T) == (skipKeyAs ? MovieKeyGoes::SkipAs : MovieKeyGoes::Skip), "an old Space tick keeps nothing");
            s.Open = 0;
            Check(Hook(s, MovieKey::Space, skipKeyAs, T) == MovieKeyGoes::AsItCame, "with no movie open, Space goes as it came, unlogged");
        }

        Seen s;
        s.Owns = true;
        Check(Hook(s, MovieKey::Pause, true, T) == MovieKeyGoes::KeptPauseKey, "Esc or Start while the movies own the keys goes to Console.InputKey");
        Check(s.Reads == "WO", "a pause key reads OwnsKeys after the pause, not ahead of it");
        s.Owns = false;
        s.Reads.clear();
        Check(Hook(s, MovieKey::Pause, true, T) == MovieKeyGoes::AsItCame, "Esc or Start with no movie of ours is the game's");
        s.Reads.clear();
        Check(Hook(s, MovieKey::Other, true, T) == MovieKeyGoes::AsItCame && s.Reads == "W", "another key with nothing paused: one read, as it came");
        s.Wanted = true;
        s.Owns = true;
        s.Reads.clear();
        Check(Hook(s, MovieKey::Other, true, T) == MovieKeyGoes::KeptPaused, "under our pause any key is kept from the movie input");
        Check(s.Reads == "WWO", "another key under the pause: the pause twice, then OwnsKeys");
        s.Owns = false;
        s.Reads.clear();
        Check(Hook(s, MovieKey::Other, true, T) == MovieKeyGoes::AsItCame, "a pause whose movies no longer own the keys keeps nothing");

        // A: no tick of its own.
        Seen a;
        a.SpaceTick = T;
        Check(Console(a, false, T + 4) == MoviePress::None && a.Reads == "W", "A never reads Space's tick");
    }

    // ! The polls
    // ========================================

    /// An Esc press in the poll that Space resumes in is part of that resume: both edges come from that poll, and Esc's
    /// press is not left to pause again at the next one.
    void EscAndSpaceInOnePoll()
    {
        Polled space;
        Polled esc;
        PollUp(space, T - 100, T, true);
        PollUp(esc, T - 100, T, true);
        auto const s = PollEdge(space, true, T, true);
        auto const e = PollEdge(esc, true, T, true);
        Check(s.Pressed && e.Pressed, "Space and Esc pressed in one poll: both edges");
        space.Taken = true;
        esc.Taken = true;
        auto const s2 = PollEdge(space, true, T + 4, false);
        auto const e2 = PollEdge(esc, true, T + 4, false);
        Check(!s2.Pressed && !e2.Pressed, "at the next poll neither is a press again");
        Check(s2.KeepFresh && e2.KeepFresh, "and both keep their ticks fresh");
        // Had the poll not read Esc's edge with Space's, the next poll would take it as a press, and pause again.
        Polled late;
        PollUp(late, T - 100, T, true);
        Check(PollEdge(late, true, T + 4, false).Pressed, "an Esc edge left for the next poll is a press there");
    }

    void Debounce()
    {
        Polled k;
        PollUp(k, T - 100, T, false);
        Check(PollEdge(k, true, T, false).Pressed, "the first press");
        PollEdge(k, false, T + 40, false);
        Check(!PollEdge(k, true, T + 100, false).Pressed, "a second press 100 ms after is a bounce");
        PollEdge(k, false, T + 120, false);
        Check(!PollEdge(k, true, T + DebounceMs - 1, false).Pressed, "still a bounce just inside the debounce");
        PollEdge(k, false, T + DebounceMs, false);
        Check(PollEdge(k, true, T + DebounceMs + 4, false).Pressed, "a press past the debounce counts from the first, not the bounce");
    }

    void PollGap()
    {
        for (auto const wanted : { false, true })
        {
            Polled k;
            PollEdge(k, false, T, wanted);
            auto const late = PollEdge(k, true, T + 600, wanted);
            Check(late.Pressed == wanted, wanted ? "after a 600 ms gap, a press under our pause still resumes"
                                                 : "after a 600 ms gap in the polls a key down is no press");
        }
        Polled k;
        PollEdge(k, false, T, false);
        Check(PollEdge(k, true, T + PollGapMs - 1, false).Pressed, "a gap just under 500 ms still makes a press");
        Polled j;
        PollEdge(j, false, T, false);
        Check(!PollEdge(j, true, T + PollGapMs, false).Pressed, "a gap of 500 ms does not");
        Polled first;
        Check(!PollEdge(first, true, T, true).Pressed, "a key down at the very first poll is no press, even under our pause");
    }

    void TakenStaysFresh()
    {
        Polled k;
        PollUp(k, T - 100, T, false);
        Check(PollEdge(k, true, T, false).Pressed, "the press");
        Check(!PollEdge(k, true, T + 4, false).KeepFresh, "a press nobody took keeps no tick");
        k.Taken = true;
        Check(PollEdge(k, true, T + 8, false).KeepFresh, "a taken press keeps its tick fresh");
        Check(PollEdge(k, true, T + HeldMaxMs - 1, false).KeepFresh, "up to its limit");
        Check(!PollEdge(k, true, T + HeldMaxMs, false).KeepFresh, "and no further: a lost release must not hold the guard");
        PollEdge(k, false, T + HeldMaxMs + 4, false);
        Check(!k.Taken, "the release ends Taken");
    }

    // ! TakenLately
    // ========================================

    void TakenLatelyCases()
    {
        auto const beat = T - 1;
        Check(!TakenLately(0, T, beat, {}).Taken, "no tick");
        auto const ahead = TakenLately(T + 3, T, beat, {});
        Check(ahead.Taken && ahead.StallStart == 0, "a tick newer than now (the movie's thread is ahead) is taken");
        Check(TakenLately(T - (ReleaseGraceMs - 1), T, beat, {}).Taken, "a tick inside 250 ms");
        Check(!TakenLately(T - ReleaseGraceMs, T, beat, {}).Taken, "a tick 250 ms old, the game thread running");

        // The game thread has run no ProcessEvent for 400 ms (a load), and the hook (a native) runs before the first one.
        auto const running = TakenLately(T - 300, T, T - 400, {});
        Check(running.Taken && running.StallStart == T - 400 && running.StallLength == 400,
              "a press acted on during a stall still running is taken, and the line names the stall");
        Check(TakenLately(T - 400, T, T - 400, {}).Taken, "a press at the stall's start is taken");
        Check(!TakenLately(T - 450, T, T - 400, {}).Taken, "a press from before the stall began is not (the 2026-11-07 audit's B1)");
        Check(!TakenLately(T - 300, T, T - GameStallMs, {}).Taken, "a gap of exactly 250 ms is no stall");

        // The stall ended 100 ms ago, after 900 ms.
        Stall const ended{ T - 1000, T - 100 };
        auto const after = TakenLately(T - 900, T, T - 1, ended);
        Check(after.Taken && after.StallStart == T - 1000 && after.StallLength == 900, "a press acted on during a stall that ended under 250 ms ago");
        Check(!TakenLately(T - 1100, T, T - 1, ended).Taken, "a press from before that stall");
        Stall const old{ T - 1000, T - ReleaseGraceMs };
        Check(!TakenLately(T - 900, T, T - 1, old).Taken, "a stall that ended 250 ms ago no longer counts");
        Stall const future{ T - 1000, T + 5 };
        Check(!TakenLately(T - 900, T, T - 1, future).Taken, "a stall end later than now counts for nothing");
    }

    // ! Keys on the game thread
    // ========================================

    void StartGoesStale()
    {
        KeyState k;
        k.LastEventAt = T;
        Check(!StartStale(k, T + 10000, 0), "no press held: nothing to go stale");
        for (auto const held : { Hold::Ours, Hold::Vanilla })
        {
            SetHold(k, held, T);
            Check(!StartStale(k, T + StartStaleMs, 0), "500 ms without an event: not yet");
            Check(StartStale(k, T + StartStaleMs + 1, 0), "over 500 ms: the release was lost");
            Check(!StartStale(k, T + 600, T + 400), "a stall that ended after its last event: the 500 ms run from its end");
            Check(StartStale(k, T + 901, T + 400), "and run out there");
            Check(StartStale(k, T + 501, T - 100), "a stall that ended before its last event changes nothing");
        }
    }

    void Guard()
    {
        KeyState k;
        Check(!Guarding(k, T), "a key never pressed guards nothing");
        SetHold(k, Hold::Ours, T);
        Check(k.PressedAt == T, "a press that becomes ours is stamped");
        Check(Guarding(k, T + GuardMs - 1), "a press of ours guards while down");
        Check(!Guarding(k, T + GuardMs), "for 2 s at most");
        SetHold(k, Hold::None, T + 50);
        k.ReleasedAt = T + 50;
        Check(k.PressedAt == T, "leaving Ours keeps the press's tick");
        Check(Guarding(k, T + 50 + ReleaseGraceMs - 1), "the grace after its release");
        Check(!Guarding(k, T + 50 + ReleaseGraceMs), "and no longer");
        KeyState v;
        SetHold(v, Hold::Vanilla, T);
        Check(v.PressedAt == 0 && !Guarding(v, T + 1), "a press left to the game is not stamped and guards nothing");
    }
}

void PauseAnytime::Tests::RunKeyRules()
{
    for (auto const skipKeyAs : { true, false })
    {
        MovieThreadFirst(skipKeyAs, 4);
        MovieThreadFirst(skipKeyAs, 5);
        GameThreadFirst(skipKeyAs, 4);
    }
    ResumeLandsAnywhere();
    PollResumesInOrder();
    NothingPaused();
    EscAndSpaceInOnePoll();
    Debounce();
    PollGap();
    TakenStaysFresh();
    TakenLatelyCases();
    StartGoesStale();
    Guard();
}
