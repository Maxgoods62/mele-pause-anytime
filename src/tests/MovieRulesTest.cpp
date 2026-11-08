// The checks of MovieRules.hpp, run by PauseAnytimeTests (Main.cpp) after the ASIs: one failed check fails the build. A
// small model of one movie as Bink.cpp's threads share it: the movie's thread decides a step (Decide), its calls go out
// (Send) and come back with Wwise's answer (Settle), while the game thread asks a pause or a resume, the latch changes, the
// movie closes and time passes. EverySequence walks every state the model can reach and every step from each, the other
// threads' steps in every window of the calls, and checks after each step what must hold in any order (Fault): the model
// has a few thousand states, so that is every sequence of steps, of any length. The model is of the rules as
// Bink.cpp calls them, not of Bink, Wwise or the game: Wwise here answers as told and counts our pauses (MovieSound.cpp's
// assumption), BinkPause does what it is asked, the close's own resume is always queued, and a movie is pausable whenever
// the latch knows its sound (the HUD and BinkWait's timing, the rest of NotPausable, are not modelled).
#include "PauseAnytime/MovieRules.hpp"
#include "PauseAnytime/tests/Check.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    using namespace PauseAnytime::MovieRules;
    using PauseAnytime::Tests::Check;

    constexpr std::uint64_t T0 = 1000000;
    /// The movie's thread, which calls its BinkWait, and a second one that calls BinkWait too (the second walk).
    constexpr std::uint32_t Thread1 = 1;
    constexpr std::uint32_t Thread2 = 2;
    /// An AKRESULT other than AK_Success (AK_Fail).
    constexpr int Refused = 2;

    /// What the latch says of the movie's sound, which the game thread may change at any time: event 7, event 8, Bink audio,
    /// a sound we don't know (the movie is then not pausable).
    constexpr Latch Latches[] = { { true, 7 }, { true, 8 }, { true, 0 }, { false, 0 } };
    constexpr unsigned LatchCount = 4;

    /// The steps, by letter:
    ///   W  BinkWait on the movie's thread: Apply's Decide, its calls go out if it has any
    ///   F  BinkDoFrame on the movie's thread: the same, where a resume or a resend is applied, never a pause
    ///   K  that Apply's calls come back, Wwise queues (Send, then Settle)
    ///   R  they come back, Wwise refuses (only for an Apply with a Wwise call)
    ///   w k r  the same three for BinkWait on a second thread (the second walk only)
    ///   T  Toggle on the game thread (ToggleLocked: Esc or Start): the resume of a pause asked, else a pause
    ///   S  ResumeLocked (RequestResume, a Space press)
    ///   D  DropStale drops the pause asked
    ///   C  the movie closes (Closed)
    ///   L  the latch changes (Latches, in turn)
    ///   +  SoundResendMs pass
    enum class Op : unsigned char { Wait, Frame, Back, Refuse, Wait2, Back2, Refuse2, Toggle, Resume, Drop, Close, Relatch, Later, Count };
    constexpr std::string_view Letters = "WFKRwkrTSDCL+";
    static_assert(Letters.size() == static_cast<std::size_t>(Op::Count));

    /// What must hold whatever the order of the steps.
    enum Fault : unsigned
    {
        SecondPause,
        TwoOut,
        Unpaired,
        NoEvent,
        Order,
        Owed,
        Sync,
        Relatched,
        Resends,
        Leftover,
        Illegal,
        FaultCount,
    };
    constexpr char const* FaultText[] = {
        "never a Wwise pause while a pause of ours is queued on the event",
        "never two of our Wwise calls out on one event at once",
        "never a Wwise resume without a pause of ours queued on the event",
        "a Wwise call always names an event",
        "BinkPause(1) only once Wwise has queued the pause's sound",
        "settled: SoundState says what Wwise holds of ours, on SoundId only",
        "settled: the picture is as Paused says, and a picture paused with an event has it paused",
        "SoundId changes only with nothing of ours owed or out on the event it had",
        "an owed resume is sent again SoundResends times at most since the press's, while the picture plays",
        "closed and settled: no pause of ours left on any event",
        "SoundState takes no step outside LegalStep",
    };
    static_assert(sizeof FaultText / sizeof FaultText[0] == FaultCount);

    /// An Apply of one thread with its calls out.
    struct Calls
    {
        bool On = false;
        Decision D;
        /// The movie's event when the step was decided: a pause applied by it stops the picture with that event.
        std::uint32_t Event = 0;
    };

    struct Sim
    {
        State M;
        bool Open = true;
        unsigned LatchAt = 0;
        Calls Out[2];
        /// Wwise: the pauses of ours it queued, less the resumes, per event (7 and 8).
        int Ours[9] = {};
        /// The picture as BinkPause left it, and the event its pause went with (0: none).
        bool Picture = false;
        std::uint32_t PausedWith = 0;
        /// Sends of an owed resume on its own (no BinkPause(0) beside it) since the last resume that went with the picture's.
        unsigned Retries = 0;
        std::uint64_t Now = T0;
        /// The faults of the latest step (Fault's bits).
        unsigned Faults = 0;
        // For the named cases only: the walk doesn't compare them.
        unsigned PictureCalls = 0;
        unsigned CloseResumes = 0;
    };

    void Fail(Sim& s, Fault f) { s.Faults |= 1u << f; }

    /// One of our ExecuteActionOnEvent calls, by thread <paramref name="t"/> (2: the close), queued or refused.
    void Wwise(Sim& s, int t, std::uint32_t event, bool pause, bool queued)
    {
        if (event != 7 && event != 8)
        {
            Fail(s, NoEvent);
            return;
        }
        if (pause && s.Ours[event] != 0) Fail(s, SecondPause);
        if (!pause && s.Ours[event] != 1) Fail(s, Unpaired);
        for (int o = 0; o < 2; o++)
        {
            if (o != t && s.Out[o].On && s.Out[o].D.Sound == event) Fail(s, TwoOut);
        }
        if (queued) s.Ours[event] += pause ? 1 : -1;
    }

    /// Apply's part under Lock, on thread <paramref name="t"/>.
    void Decided(Sim& s, int t, bool inWait)
    {
        auto const d = Decide(s.M, inWait, t == 0 ? Thread1 : Thread2, s.Now, true);
        if (d.Bad.Any) Fail(s, Illegal);
        if (d.Do == Step::None) return;
        s.Out[t] = { true, d, s.M.SoundId };
    }

    /// Thread <paramref name="t"/>'s calls, Wwise answering <paramref name="ak"/>, then their Settle, and the resume owed by a
    /// movie closed meanwhile. <paramref name="pictures"/>: the picture is checked too (one thread calls BinkWait).
    void Back(Sim& s, int t, int ak, bool pictures)
    {
        auto& c = s.Out[t];
        auto wwiseAk = -1;
        auto withPicture = false;
        auto const a = Send(
            c.D,
            [&](int pause) {
                s.PictureCalls++;
                if (pause == 1 && c.D.Sound != 0 && wwiseAk != AkSuccess) Fail(s, Order);
                withPicture = true;
                if (s.Open)
                {
                    s.Picture = pause == 1;
                    s.PausedWith = pause == 1 ? c.Event : 0;
                }
                return pause;
            },
            [&](std::uint32_t event, bool pause) {
                wwiseAk = ak;
                if (!pause)
                {
                    if (withPicture) s.Retries = 0;
                    else if (++s.Retries > SoundResends || (pictures && s.Picture)) Fail(s, Resends);
                }
                Wwise(s, t, event, pause, ak == AkSuccess);
                return ak;
            });
        auto const r = Settle(s.Open ? &s.M : nullptr, c.D, a, s.Now, [&] { return Latches[s.LatchAt]; });
        if (r.Bad.Any) Fail(s, Illegal);
        auto const owed = c.D.Sound;
        c = {};
        if (r.OwedGone) Wwise(s, t, owed, false, true);
    }

    /// Closed: its part under Lock, the movie dropped, then its calls. BinkPause(0) and the close leave no picture.
    void Closed(Sim& s)
    {
        auto const c = Close(s.M);
        s.Open = false;
        if (c.Resume != 0)
        {
            s.CloseResumes++;
            Wwise(s, 2, c.Resume, false, true);
        }
        s.Picture = false;
    }

    /// ToggleLocked with one movie: Wanted is its Want, and a pause goes to it while it is pausable.
    void Toggled(Sim& s)
    {
        if (s.M.Want) AskResume(s.M);
        else if (Latches[s.LatchAt].Known) AskPause(s.M, [&] { return Latches[s.LatchAt]; });
    }

    /// One step. False when it can't happen now, or would only repeat another step from here.
    bool Take(Sim& s, Op op, bool two)
    {
        s.Faults = 0;
        auto const pictures = !two;
        switch (op)
        {
        case Op::Wait:
        case Op::Frame:
            if (!s.Open || s.Out[0].On) return false;
            Decided(s, 0, op == Op::Wait);
            return true;
        case Op::Back:
            if (!s.Out[0].On) return false;
            Back(s, 0, AkSuccess, pictures);
            return true;
        case Op::Refuse:
            if (!s.Out[0].On || s.Out[0].D.Sound == 0) return false;
            Back(s, 0, Refused, pictures);
            return true;
        case Op::Wait2:
            if (!two || !s.Open || s.Out[1].On) return false;
            Decided(s, 1, true);
            return true;
        case Op::Back2:
            if (!s.Out[1].On) return false;
            Back(s, 1, AkSuccess, pictures);
            return true;
        case Op::Refuse2:
            if (!s.Out[1].On || s.Out[1].D.Sound == 0) return false;
            Back(s, 1, Refused, pictures);
            return true;
        case Op::Toggle:
            if (!s.Open) return false;
            Toggled(s);
            return true;
        case Op::Resume:
            if (!s.Open) return false;
            AskResume(s.M);
            return true;
        case Op::Drop:
            // DropStale drops a pause asked through AskResume, on a movie whose Want is set.
            if (!s.Open || !s.M.Want) return false;
            AskResume(s.M);
            return true;
        case Op::Close:
            if (!s.Open) return false;
            Closed(s);
            return true;
        case Op::Relatch:
            if (!s.Open) return false;
            s.LatchAt = (s.LatchAt + 1) % LatchCount;
            return true;
        case Op::Later:
            s.Now += SoundResendMs;
            return true;
        case Op::Count: break;
        }
        return false;
    }

    /// What must hold after the step from <paramref name="before"/> to <paramref name="s"/>.
    void CheckState(Sim const& before, Sim& s, bool pictures)
    {
        if (before.Open && s.Open && before.M.SoundId != s.M.SoundId)
        {
            auto const old = before.M.SoundId;
            auto out = false;
            for (auto const& c : s.Out) out = out || (c.On && c.D.Sound != 0 && c.D.Sound == old);
            if (s.M.Sound != SoundState::None || (old != 0 && s.Ours[old] != 0) || out) Fail(s, Relatched);
        }
        if (s.Out[0].On || s.Out[1].On) return;
        if (!s.Open)
        {
            if (s.Ours[7] != 0 || s.Ours[8] != 0) Fail(s, Leftover);
            return;
        }
        auto const owed = s.M.Sound == SoundState::Paused;
        auto const holds = [&](std::uint32_t e) { return s.Ours[e] == (owed && s.M.SoundId == e ? 1 : 0); };
        if ((s.M.Sound != SoundState::None && !owed) || !holds(7) || !holds(8)) Fail(s, Owed);
        if (pictures && (s.Picture != s.M.Paused || (s.Picture && s.PausedWith != 0 && s.Ours[s.PausedWith] != 1))) Fail(s, Sync);
    }

    // ! Every sequence
    // ========================================

    unsigned IdBits(std::uint32_t id) { return id == 7 ? 1u : id == 8 ? 2u : id == 0 ? 0u : 3u; }

    /// Key keeps the counts in 6 bits: a state with more is one the rules should never reach, and is not compared.
    constexpr unsigned CountMax = 63;

    /// All that a later step or check reads of the model, packed: two states with the same key have the same steps ahead.
    /// The clock only as ResendDue reads it, and not the counts the named cases read.
    std::uint64_t Key(Sim const& s)
    {
        std::uint64_t k = 0;
        auto const put = [&](std::uint64_t v, int bits) { k = (k << bits) | (v & ((std::uint64_t{ 1 } << bits) - 1)); };
        put(s.M.Want, 1);
        put(s.M.Paused, 1);
        put(s.M.WaitThread, 2);
        put(IdBits(s.M.SoundId), 2);
        put(static_cast<unsigned>(s.M.Sound), 2);
        put(s.M.Resends, 6);
        put(Since(s.Now, s.M.SoundSentAt) >= SoundResendMs, 1);
        put(s.Open, 1);
        put(s.LatchAt, 2);
        for (auto const& c : s.Out)
        {
            put(c.On, 1);
            put(static_cast<unsigned>(c.D.Do), 2);
            put(IdBits(c.D.Sound), 2);
            put(IdBits(c.Event), 2);
        }
        put(static_cast<std::uint64_t>(s.Ours[7] + 2), 3);
        put(static_cast<std::uint64_t>(s.Ours[8] + 2), 3);
        put(s.Picture, 1);
        put(IdBits(s.PausedWith), 2);
        put(s.Retries, 6);
        return k;
    }

    /// The model's states are few: the walk stops at this many all the same. More, or a count past CountMax, means the rules
    /// or the model changed so that it never closes (a count that grows without end), which is a failure too.
    constexpr std::size_t MaxStates = 400000;

    /// Every state the model can reach from the start, and every step from each, checked once: two states with one Key have
    /// the same steps ahead of them, so once no step leads to a state not seen yet, every sequence of steps of any length has
    /// been checked. Breadth first, so the sequence named for a fault is one of the shortest that show it. With
    /// <paramref name="two"/>, BinkWait on a second thread too.
    void EverySequence(bool two)
    {
        /// How a state was first reached: from which one, by which step.
        struct Trail
        {
            std::size_t From = 0;
            char Step = 0;
            int Depth = 0;
        };
        std::vector<Trail> trail{ Trail{} };
        std::unordered_map<std::uint64_t, std::size_t> seen{ { Key(Sim{}), 0 } };
        std::deque<std::pair<std::size_t, Sim>> queue{ { 0, Sim{} } };
        bool found[FaultCount] = {};
        std::string first[FaultCount];
        auto const sequence = [&](std::size_t at, char last) {
            std::string path(1, last);
            for (; at != 0; at = trail[at].From) path.insert(path.begin(), trail[at].Step);
            return path;
        };
        unsigned long long steps = 0;
        auto deepest = 0;
        auto closed = true;
        while (!queue.empty())
        {
            auto const [at, s] = queue.front();
            queue.pop_front();
            for (unsigned i = 0; i < static_cast<unsigned>(Op::Count); i++)
            {
                auto n = s;
                if (!Take(n, static_cast<Op>(i), two)) continue;
                CheckState(s, n, !two);
                steps++;
                for (unsigned f = 0; f < FaultCount; f++)
                {
                    if ((n.Faults & (1u << f)) == 0 || found[f]) continue;
                    found[f] = true;
                    first[f] = sequence(at, Letters[i]);
                }
                if (n.M.Resends > CountMax || n.Retries > CountMax)
                {
                    closed = false;
                    continue;
                }
                if (seen.size() >= MaxStates)
                {
                    closed = closed && seen.contains(Key(n));
                    continue;
                }
                auto const depth = trail[at].Depth + 1;
                if (!seen.try_emplace(Key(n), trail.size()).second) continue;
                trail.push_back({ at, Letters[i], depth });
                deepest = (std::max)(deepest, depth);
                queue.emplace_back(trail.size() - 1, n);
            }
        }
        for (unsigned f = 0; f < FaultCount; f++)
        {
            // With BinkWait on two threads, two BinkPause calls can be out at once (see SecondThread): the picture is not checked.
            if (two && f == Sync) continue;
            Check(!found[f], FaultText[f]);
            if (found[f]) std::printf("  first after the steps %s\n", first[f].c_str());
        }
        Check(closed, two ? "the walk with two threads closes: every state the model has is reached" : "the walk closes: every state the model has is reached");
        std::printf("MovieRules%s: all %zu states, reached within %d steps; %llu steps from them checked\n", two ? " (BinkWait on two threads)" : "",
                    seen.size(), deepest, steps);
    }

    // ! Named cases
    // ========================================

    /// Takes the steps named by their letters, each of which must be able to happen and break no rule.
    bool Run(Sim& s, std::string_view steps, char const* what)
    {
        for (std::size_t i = 0; i < steps.size(); i++)
        {
            auto const before = s;
            auto const op = Letters.find(steps[i]);
            auto const ok = op != std::string_view::npos && Take(s, static_cast<Op>(op), false);
            if (ok) CheckState(before, s, true);
            if (!ok || s.Faults != 0)
            {
                Check(false, what);
                std::printf("  at step %zu ('%c') of %.*s\n", i, steps[i], static_cast<int>(steps.size()), steps.data());
                return false;
            }
        }
        return true;
    }

    /// Paused with event 7: asked, applied at a BinkWait, queued by Wwise.
    Sim PausedWith7()
    {
        Sim s;
        Run(s, "TWK", "a pause with its Wwise event");
        Check(s.Picture && s.Ours[7] == 1 && s.M.Sound == SoundState::Paused && s.M.SoundId == 7, "the picture and its event paused, our pause owed a resume");
        return s;
    }

    /// Start pressed on the game thread while the movie's thread has the resume's calls out (LE2's Start toggles through
    /// Toggle, not the movie's poll), with the latch on another event by then.
    void StartWhileResumeOut()
    {
        for (auto const queued : { true, false })
        {
            auto s = PausedWith7();
            Run(s, "TW", "a resume asked and applied");
            Check(s.Out[0].D.Do == Step::Resume && s.Out[0].D.Sound == 7 && s.M.Sound == SoundState::Resuming, "the resume's calls out, the event's with the picture's");
            Run(s, "LT", "Start pressed while the resume is out");
            Check(s.M.Want && s.M.SoundId == 7, "a pause asked while a resume is out keeps the event: the resume may be refused");
            Run(s, queued ? "K" : "R", "the resume comes back");
            if (queued)
            {
                Check(s.M.Sound == SoundState::None && s.M.SoundId == 8 && s.Ours[7] == 0, "resume queued: the pause asked meanwhile takes the latch's event then");
                Run(s, "W", "the pause asked meanwhile is applied");
                Check(s.Out[0].D.Do == Step::Pause && s.Out[0].D.Sound == 8, "its Wwise pause goes to that event");
                Run(s, "K", "Wwise queues it");
                Check(s.Picture && s.Ours[8] == 1 && s.Ours[7] == 0, "the picture and the new event paused, the old one playing");
            }
            else
            {
                Check(s.M.Sound == SoundState::Paused && s.M.SoundId == 7 && s.Ours[7] == 1, "resume refused: the old event stays owed under its id");
                Run(s, "W", "the pause asked meanwhile is applied");
                Check(s.Out[0].D.Do == Step::Pause && s.Out[0].D.Sound == 0, "with no second Wwise pause");
                Run(s, "K", "BinkPause(1)");
                Check(s.Picture && s.Ours[7] == 1, "the picture stops with its event, paused by us once");
            }
        }
    }

    void PauseRefused()
    {
        Sim s;
        Run(s, "TW", "a pause asked and applied");
        Check(s.Out[0].D.Do == Step::Pause && s.Out[0].D.Sound == 7 && s.M.Sound == SoundState::Pausing, "the pause's Wwise call is out");
        Run(s, "R", "Wwise refuses the pause");
        Check(s.PictureCalls == 0 && !s.Picture, "a pause whose sound Wwise refused makes no BinkPause");
        Check(!s.M.Want && !s.M.Paused && s.M.Sound == SoundState::None && s.Ours[7] == 0, "and pauses nothing: no pause asked, applied or owed");
        Run(s, "TW", "the next press");
        Check(s.Out[0].D.Do == Step::Pause && s.Out[0].D.Sound == 7, "the next press tries again");
        Run(s, "K", "Wwise queues it");
        Check(s.Picture && s.Ours[7] == 1, "and pauses both");
        Decision const pause{ Step::Pause, 7, 0, {} };
        Check(!PausedAfter(pause, { Refused, 0 }) && PausedAfter(pause, { AkSuccess, 1 }), "Apply reports a refused pause as no pause (MovieIdle presents nothing for it)");
    }

    void ResumeRefusedResendsClose()
    {
        auto s = PausedWith7();
        Run(s, "TWR", "a resume Wwise refuses");
        Check(!s.Picture && s.M.Sound == SoundState::Paused && s.Ours[7] == 1, "the picture plays, the event stays paused, its resume owed");
        Run(s, "W", "a BinkWait at once");
        Check(!s.Out[0].On, "no resend before SoundResendMs");
        for (unsigned n = 1; n <= SoundResends; n++)
        {
            Run(s, "+W", "SoundResendMs later");
            Check(s.Out[0].On && s.Out[0].D.Do == Step::Resend && s.Out[0].D.Resend == n && s.Out[0].D.Sound == 7, "the owed resume is sent again");
            Run(s, "R", "and refused");
        }
        Run(s, "+W+F+W", "more time, BinkWait and BinkDoFrame");
        Check(!s.Out[0].On && s.Retries == SoundResends, "SoundResends sends again at most while the picture plays");
        Run(s, "C", "the movie closes");
        Check(s.CloseResumes == 1 && s.Ours[7] == 0, "the close sends the owed resume, once");

        // A resend Wwise queues ends the debt: the close then sends nothing.
        auto q = PausedWith7();
        Run(q, "TWR+WK", "a resend Wwise queues");
        Check(q.M.Sound == SoundState::None && q.Ours[7] == 0, "the event plays again");
        Run(q, "+WC", "the movie closes");
        Check(q.CloseResumes == 0, "nothing owed at the close");
    }

    void PauseWhileResumeOwed()
    {
        auto s = PausedWith7();
        Run(s, "TWR", "a resume Wwise refuses");
        Run(s, "LT", "a press pauses again, the latch on another event");
        Check(s.M.Want && s.M.SoundId == 7, "the owed event keeps its id");
        Run(s, "W", "the pause applied");
        Check(s.Out[0].D.Do == Step::Pause && s.Out[0].D.Sound == 0, "no second Wwise pause of an event still paused by us");
        Run(s, "K", "BinkPause(1)");
        Check(s.Picture && s.Ours[7] == 1 && s.M.Sound == SoundState::Paused, "the picture stops with its event again");
        Run(s, "+W", "a BinkWait later");
        Check(!s.Out[0].On, "no resend while the picture is paused");
        Run(s, "TW", "the next press resumes");
        Check(s.Out[0].D.Do == Step::Resume && s.Out[0].D.Sound == 7 && s.M.Resends == 0, "the press's resume sends the owed one, its retries counted afresh");
        Run(s, "K", "Wwise queues it");
        Check(!s.Picture && s.Ours[7] == 0 && s.M.Sound == SoundState::None, "both play again");
    }

    /// The retries are counted afresh from a press's resume, after earlier ones used them up: the walk bounds the resends
    /// from above only, so it can't see one missing.
    void ResendsCountedAfresh()
    {
        auto s = PausedWith7();
        Run(s, "TWR", "a resume Wwise refuses");
        for (unsigned n = 1; n <= SoundResends; n++) Run(s, "+WR", "sent again and refused");
        Check(s.M.Resends == SoundResends, "the retries used up");
        Run(s, "LTWK", "a press pauses the picture again, with the event still ours");
        Run(s, "TWR", "the next press resumes, and Wwise refuses that resume too");
        Run(s, "+W", "SoundResendMs later");
        Check(s.Out[0].On && s.Out[0].D.Do == Step::Resend && s.Out[0].D.Resend == 1, "the press's resume gets its own retries: the first is sent");
        Run(s, "K", "Wwise queues it");
        Check(s.Ours[7] == 0 && s.M.Sound == SoundState::None, "the event plays again before the close");
    }

    /// A game that overlaps BinkClose with another call on the movie: the Apply finds its movie gone and sends the resume
    /// owed itself.
    void ClosedDuringCalls()
    {
        Sim s;
        Run(s, "TWCK", "closed while its pause is out, Wwise queues the pause");
        Check(s.Ours[7] == 0 && s.CloseResumes == 0, "the Apply sends the resume, the close none");
        Sim r;
        Run(r, "TWCR", "closed while its pause is out, Wwise refuses it");
        Check(r.Ours[7] == 0, "nothing owed");
        auto p = PausedWith7();
        Run(p, "TWCR", "closed while its resume is out, Wwise refuses it");
        Check(p.Ours[7] == 0 && p.CloseResumes == 0, "the Apply sends the resume once more");
        auto k = PausedWith7();
        Run(k, "TWCK", "closed while its resume is out, Wwise queues it");
        Check(k.Ours[7] == 0, "nothing owed");
    }

    /// BinkWait on a second thread while the first one's calls are out: the guard on Pausing and Resuming keeps the second
    /// Apply's hands off the event. Not with no Wwise call out (Bink audio, or a pause of an event still owed): then a
    /// resume decided there sends BinkPause(0) while BinkPause(1) is out, and the two can land either way. Bink.cpp takes a
    /// movie's BinkWait to come on one thread (WaitThread); the walk with two threads checks the sound alone.
    void SecondThread()
    {
        Sim s;
        Run(s, "TW", "a pause's Wwise call out on the movie's thread");
        Decided(s, 1, true);
        Check(!s.Out[1].On && s.M.WaitThread == Thread2, "a BinkWait on another thread meanwhile applies nothing");
        auto r = PausedWith7();
        Run(r, "TW", "a resume's calls out on the movie's thread");
        Decided(r, 1, true);
        Check(!r.Out[1].On, "nor while a resume is out");
    }

    // ! The rules one by one
    // ========================================

    void Tables()
    {
        constexpr SoundState All[] = { SoundState::None, SoundState::Pausing, SoundState::Paused, SoundState::Resuming };
        auto legal = 0;
        for (auto const from : All)
        {
            for (auto const to : All) legal += LegalStep(from, to) ? 1 : 0;
        }
        Check(legal == 6 && LegalStep(SoundState::None, SoundState::Pausing) && LegalStep(SoundState::Pausing, SoundState::Paused) &&
                  LegalStep(SoundState::Pausing, SoundState::None) && LegalStep(SoundState::Paused, SoundState::Resuming) &&
                  LegalStep(SoundState::Resuming, SoundState::None) && LegalStep(SoundState::Resuming, SoundState::Paused),
              "LegalStep: None -> Pausing -> Paused or None, Paused -> Resuming -> None or Paused, and nothing else");
        State st;
        auto bad = SetSound(st, SoundState::Paused, T0);
        Check(bad.Any && bad.From == SoundState::None && bad.To == SoundState::Paused && st.Sound == SoundState::Paused && st.SoundSentAt == 0,
              "SetSound sets a step it doesn't take all the same, says so, and stamps no send");
        bad = SetSound(st, SoundState::Resuming, T0 + 5);
        Check(!bad.Any && st.SoundSentAt == T0 + 5, "SetSound stamps a send");

        for (auto const sound : All)
        {
            State x;
            x.Sound = sound;
            x.SoundId = 7;
            auto calls = 0;
            AskPause(x, [&] { calls++; return Latch{ true, 8 }; });
            auto const none = sound == SoundState::None;
            Check(x.Want && x.SoundId == (none ? 8u : 7u) && calls == (none ? 1 : 0), "AskPause takes the latch's event only while nothing of ours is owed or out");
        }
        State a;
        Check(!AskResume(a), "AskResume leaves a movie with nothing of ours alone");
        a.Paused = true;
        Check(AskResume(a) && !a.Want && a.Paused, "AskResume asks the resume of a movie we paused");

        for (auto const sound : All)
        {
            for (auto const paused : { false, true })
            {
                State x;
                x.Sound = sound;
                x.SoundId = 7;
                x.Paused = paused;
                auto const c = Close(x);
                Check(c.Resume == (sound == SoundState::Paused ? 7u : 0u) && c.Out == (sound == SoundState::Pausing || sound == SoundState::Resuming) && c.Picture == paused,
                      "Close: the owed resume, a call out left to its Apply, BinkPause(0) for a paused picture");
            }
        }

        auto const gone = [](Step step, int ak) { return Settle(nullptr, Decision{ step, 7, 0, {} }, Answer{ ak, 0 }, T0, [] { return Latch{}; }).OwedGone; };
        Check(gone(Step::Pause, AkSuccess) && !gone(Step::Pause, Refused) && !gone(Step::Resume, AkSuccess) && gone(Step::Resume, Refused) &&
                  !gone(Step::Resend, AkSuccess) && gone(Step::Resend, Refused),
              "Settle on a movie gone: a resume is owed after a pause queued or a resume refused");
        auto latchReads = 0;
        auto const none = Settle(nullptr, Decision{ Step::Pause, 0, 0, {} }, Answer{}, T0, [&] { latchReads++; return Latch{}; });
        Check(!none.OwedGone && !none.Recount && latchReads == 0, "Settle of an Apply with no Wwise call does nothing");

        State u;
        u.Sound = SoundState::Resuming;
        u.SoundId = 7;
        u.Want = true;
        auto const kept = Settle(&u, Decision{ Step::Resume, 7, 0, {} }, Answer{}, T0, [] { return Latch{ false, 0 }; });
        Check(!kept.Retook && u.SoundId == 7 && u.Sound == SoundState::None, "a resume queued with a pause asked: a latch that doesn't know the sound leaves the event");
        u.Sound = SoundState::Resuming;
        u.Want = false;
        Settle(&u, Decision{ Step::Resume, 7, 0, {} }, Answer{}, T0, [&] { latchReads++; return Latch{ true, 8 }; });
        Check(u.SoundId == 7 && latchReads == 0, "with no pause asked, the latch is not read");

        State o;
        o.Sound = SoundState::Paused;
        o.WaitThread = Thread1;
        o.SoundSentAt = T0;
        auto const due = T0 + SoundResendMs;
        Check(ResendDue(o, Thread1, due), "an owed resume is due SoundResendMs after the last send");
        Check(!ResendDue(o, Thread1, due - 1), "not before");
        Check(!ResendDue(o, Thread2, due), "only on the thread of the movie's BinkWait");
        o.Resends = SoundResends;
        Check(!ResendDue(o, Thread1, due), "SoundResends times at most");
        o.Resends = 0;
        o.Want = true;
        Check(!ResendDue(o, Thread1, due), "not while a pause is asked");
        o.Want = false;
        o.Paused = true;
        Check(!ResendDue(o, Thread1, due), "not while the picture is paused");

        State w;
        w.Want = true;
        w.WaitThread = Thread1;
        Check(Decide(w, false, Thread1, T0, true).Do == Step::None && !w.Paused, "a pause is applied in BinkWait only");
        Check(Decide(w, true, Thread1, T0, false).Do == Step::None && !w.Paused, "nothing is applied without BinkPause");
        State q;
        q.Paused = true;
        q.WaitThread = Thread1;
        Check(Decide(q, false, Thread2, T0, true).Do == Step::None && q.Paused, "a resume waits for the thread of the movie's BinkWait");
        Check(Decide(q, false, Thread1, T0, true).Do == Step::Resume && !q.Paused && q.ChangedAt == T0, "and is applied there, in any call");
    }
}

void PauseAnytime::Tests::RunMovieRules()
{
    Tables();
    StartWhileResumeOut();
    PauseRefused();
    ResumeRefusedResendsClose();
    PauseWhileResumeOwed();
    ResendsCountedAfresh();
    ClosedDuringCalls();
    SecondThread();
    EverySequence(false);
    EverySequence(true);
}
