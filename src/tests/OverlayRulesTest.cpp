// The checks of OverlayRules.hpp, run by PauseAnytimeTests (Main.cpp) after the ASIs: one failed check fails the build. The
// gate, on given thread ids and on real threads, a resize's wait for it on a clock of the test's own, the rules that decide
// which swap chain is the game's, and those for another swap chain once the overlay draws. They are Overlay.cpp's rules,
// not DXGI, a real process's windows or the engine's threads: which thread makes the game's window is seen in the game
// (the start line names it).
#include "PauseAnytime/OverlayRules.hpp"
#include "PauseAnytime/tests/Check.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{
    using namespace PauseAnytime::OverlayRules;
    using PauseAnytime::Tests::Check;

    // ! The gate
    // ========================================

    void GateOnOneThread()
    {
        std::atomic<Word> owner{ 0 };
        auto const a = Enter(owner, 11);
        Check(a == Gate::Taken && owner.load() == 11, "a free gate is taken");
        Check(Enter(owner, 11) == Gate::Held, "the holder's own call further down its stack finds it held (MovieIdle's present)");
        Leave(owner, Gate::Held, 11);
        Check(owner.load() == 11, "the Leave of a call that found it held gives nothing back");
        Check(Enter(owner, 22) == Gate::Busy, "another thread finds it busy");
        Leave(owner, Gate::Busy, 22);
        Check(owner.load() == 11, "the Leave of a call that found it busy gives nothing back");
        Leave(owner, Gate::Taken, 22);
        Check(owner.load() == 11, "a Leave by a thread that doesn't hold it gives nothing back");
        Leave(owner, a, 11);
        Check(owner.load() == 0, "the holder's Leave frees it");
        Leave(owner, a, 11);
        Check(owner.load() == 0, "a second Leave does nothing");

        // An early Leave, another thread takes the gate, then the first frame's __finally leaves again.
        auto const b = Enter(owner, 11);
        Leave(owner, b, 11);
        auto const c = Enter(owner, 22);
        Leave(owner, b, 11);
        Check(b == Gate::Taken && c == Gate::Taken && owner.load() == 22, "a second Leave after another thread took the gate leaves it with that thread");
        Check(Enter(owner, 33) == Gate::Busy, "which keeps it from a third");
        Leave(owner, c, 22);
        Check(owner.load() == 0, "until it leaves");
    }

    /// A holder away in an original call it makes with the gate (MovieIdle's present, a resize), on one thread.
    void GateAway()
    {
        constexpr Word Away22 = 22 | AwayMark;
        std::atomic<Word> owner{ 0 };
        Check(!GoAway(owner, 22) && owner.load() == 0, "a thread that doesn't hold the gate can't go away with it");
        auto const a = Enter(owner, 22);
        Check(!GoAway(owner, 33) && owner.load() == 22, "nor can another thread than its holder");
        Check(GoAway(owner, 22) && owner.load() == Away22 && !Holds(owner, 22), "its holder goes away with it, and no longer holds it for our work");
        Check(!GoAway(owner, 22) && owner.load() == Away22, "once");
        Check(Enter(owner, 33) == Gate::Busy && Enter(owner, 22) == Gate::Busy,
              "away, it is busy to every present, its own thread's too: one inside the call it went away for passes through");
        Check(ComeBack(owner, 22) && owner.load() == 22 && Holds(owner, 22), "back, it holds it for our work again");
        Check(!ComeBack(owner, 22) && owner.load() == 22, "a ComeBack with no GoAway changes nothing");
        GoAway(owner, 22);
        Leave(owner, a, 22);
        Check(owner.load() == 0, "a Leave while away, its ComeBack skipped, gives it back all the same");

        // Taken from while away: the ComeBack says so, and the old holder's Leave gives nothing back.
        auto const b = Enter(owner, 22);
        GoAway(owner, 22);
        Word was = Away22;
        owner.compare_exchange_strong(was, 11);
        Check(!ComeBack(owner, 22) && owner.load() == 11 && !Holds(owner, 22), "taken from while away, ComeBack finds the gate no longer its own");
        Leave(owner, b, 22);
        Check(owner.load() == 11, "and its Leave leaves the gate with the resize");
        Leave(owner, Gate::Taken, 11);
        Check(owner.load() == 0, "until that leaves");
    }

    /// Threads taking the gate in turn, each leaving twice as BeforeResize can (early, then in its frame's __finally after the
    /// original ResizeBuffers), and once more for a call that found it busy: never two inside at once. What it asserts holds
    /// in every run; how the threads interleave differs from run to run, so GateOnOneThread pins the orders that matter.
    void GateOnThreads()
    {
        constexpr int Threads = 8;
        constexpr int Rounds = 20000;
        std::atomic<Word> owner{ 0 };
        std::atomic<int> inside{ 0 };
        std::atomic<int> most{ 0 };
        std::atomic<int> taken{ 0 };
        std::atomic<int> busy{ 0 };
        std::atomic<int> wrong{ 0 };
        std::atomic<bool> go{ false };
        std::vector<std::thread> threads;
        for (int t = 0; t < Threads; t++)
        {
            threads.emplace_back([&, self = static_cast<std::uint32_t>(t + 1)] {
                while (!go.load()) std::this_thread::yield();
                for (int r = 0; r < Rounds; r++)
                {
                    auto const gate = Enter(owner, self);
                    if (gate != Gate::Taken)
                    {
                        if (gate == Gate::Held) wrong++;
                        busy++;
                        Leave(owner, gate, self);
                        continue;
                    }
                    auto const now = inside.fetch_add(1) + 1;
                    for (auto m = most.load(); now > m && !most.compare_exchange_weak(m, now);) {}
                    taken++;
                    // Some work inside, then the holder's own nested call.
                    for (int spin = 0; spin < 16; spin++) std::atomic_signal_fence(std::memory_order_seq_cst);
                    if (owner.load() != self || Enter(owner, self) != Gate::Held) wrong++;
                    inside.fetch_sub(1);
                    Leave(owner, gate, self);
                    // The original call between the two Leaves: another thread may take the gate meanwhile.
                    std::this_thread::yield();
                    Leave(owner, gate, self);
                }
            });
        }
        go.store(true);
        for (auto& t : threads) t.join();
        Check(most.load() == 1, "on 8 threads, one holds the gate at a time");
        Check(wrong.load() == 0, "a holder always finds the gate its own, and no thread finds it held at the start of a call");
        Check(taken.load() + busy.load() == Threads * Rounds && taken.load() > 0 && owner.load() == 0, "every call took it or passed, and it is free at the end");
    }

    // ! A resize's wait for the gate
    // ========================================

    /// EnterWaiting on the test's clock: each pause is one more millisecond, or what a case makes of it.
    struct WaitClock
    {
        std::uint64_t Now = 1000;
        int Pauses = 0;
        int OursAsked = 0;
        /// The waiting count as each pause saw it.
        bool CountedAll = true;
    };

    void Waiting()
    {
        constexpr std::uint64_t Limit = ResizeWaitMs;
        constexpr std::uint64_t Own = ResizeWaitOwnMs;
        static_assert(Limit < Own, "a holder in our own work is waited for longer than one away");
        constexpr Word Away22 = 22 | AwayMark;
        constexpr Word Away33 = 33 | AwayMark;
        std::atomic<unsigned> waiting{ 0 };
        auto const wait = [&](std::atomic<Word>& owner, WaitClock& c, auto ours, auto during) {
            return EnterWaiting(
                owner, waiting, 11, Limit, Own, [&] { return c.Now; },
                [&](unsigned) {
                    c.Pauses++;
                    c.CountedAll = c.CountedAll && waiting.load() == 1;
                    during(c);
                },
                [&] {
                    c.OursAsked++;
                    return ours(c);
                });
        };
        auto const yes = [](WaitClock&) { return true; };
        auto const no = [](WaitClock&) { return false; };
        auto const tick = [](WaitClock& k) { k.Now += 1; };

        std::atomic<Word> unheld{ 0 };
        WaitClock a;
        auto w = wait(unheld, a, yes, tick);
        Check(w.Got == Gate::Taken && w.TakenFrom == 0 && a.Pauses == 0 && a.OursAsked == 0 && waiting.load() == 0, "a free gate is taken at once");
        Leave(unheld, w.Got, 11);

        std::atomic<Word> mine{ 11 };
        WaitClock h;
        w = wait(mine, h, yes, tick);
        Check(w.Got == Gate::Held && h.Pauses == 0, "a resize inside ours on the same thread finds it held, and waits for nothing");

        std::atomic<Word> soon{ 22 };
        WaitClock b;
        w = wait(soon, b, yes, [&](WaitClock& c) {
            c.Now += 1;
            if (c.Pauses == 3) Leave(soon, Gate::Taken, 22);
        });
        Check(w.Got == Gate::Taken && w.TakenFrom == 0 && b.Pauses == 3 && b.OursAsked == 0, "a holder that leaves is waited for");
        Check(b.CountedAll && waiting.load() == 0, "the resize is counted while it waits, and only then");
        Leave(soon, w.Got, 11);

        // Away in a call we don't control (MovieIdle's present) and stuck there.
        std::atomic<Word> stuck{ Away22 };
        WaitClock c;
        w = wait(stuck, c, yes, tick);
        Check(w.Got == Gate::Taken && w.TakenFrom == 22 && w.FromAway && stuck.load() == 11 && c.Now - 1000 == Limit && c.OursAsked == 1,
              "a holder stuck away past the limit on the swap chain we draw on: the gate is taken from it");
        Check(!ComeBack(stuck, 22), "back from its call, it finds the gate taken");
        Leave(stuck, Gate::Taken, 22);
        Check(stuck.load() == 11, "and its own Leave leaves it with the resize");
        Check(Enter(stuck, 33) == Gate::Busy && Enter(stuck, 22) == Gate::Busy, "every other call, the old holder's next one too, finds it busy");
        Leave(stuck, w.Got, 11);
        Check(stuck.load() == 0, "until the resize leaves");

        // In our own work past the away limit (Adopt's shader builds, the frame drawn after): never taken from at it.
        std::atomic<Word> working{ 22 };
        WaitClock o;
        w = wait(working, o, yes, [&](WaitClock& k) {
            k.Now += 1;
            if (k.Now == 1000 + Limit + 50) Leave(working, Gate::Taken, 22);
        });
        Check(w.Got == Gate::Taken && w.TakenFrom == 0 && o.Now - 1000 == Limit + 50 && o.OursAsked == 0,
              "a holder in our own work is waited for past the away limit, and the gate is never taken from it there");
        Leave(working, w.Got, 11);

        // Adopt: DrawnChain null through the shader builds, set 240 ms in, then the frame on the new swap chain to 260 ms.
        std::atomic<Word> adopting{ 22 };
        WaitClock g;
        w = wait(
            adopting, g, [](WaitClock& k) { return k.Now >= 1240; },
            [&](WaitClock& k) {
                k.Now += 1;
                if (k.Now == 1260) Leave(adopting, Gate::Taken, 22);
            });
        Check(w.Got == Gate::Taken && w.TakenFrom == 0 && g.Now == 1260 && g.OursAsked == 0,
              "a resize of the swap chain Adopt just took waits for its frame: nothing is taken from it at the limit, DrawnChain set or not");
        Leave(adopting, w.Got, 11);

        // The last resort: our own work that never ends (a lock we can't see, a hung driver).
        std::atomic<Word> hung{ 22 };
        WaitClock z;
        w = wait(hung, z, yes, tick);
        Check(w.Got == Gate::Taken && w.TakenFrom == 22 && !w.FromAway && z.Now - 1000 == Own && z.OursAsked == 1,
              "past ResizeWaitOwnMs a holder in our own work is taken from all the same: the resize never hangs, and says so");
        Leave(hung, w.Got, 11);
        std::atomic<Word> hungOther{ 22 };
        WaitClock y;
        w = wait(hungOther, y, no, tick);
        Check(w.Got == Gate::Busy && w.TakenFrom == 0 && hungOther.load() == 22 && y.Now - 1000 == Own && waiting.load() == 0,
              "for another swap chain it is left with it at ResizeWaitOwnMs, not at the away limit");

        // Own work for 200 ms, then away: the away limit counts from there.
        std::atomic<Word> goes{ 22 };
        WaitClock v;
        w = wait(goes, v, yes, [&](WaitClock& k) {
            k.Now += 1;
            if (k.Now == 1200) goes.store(Away22);
        });
        Check(w.Got == Gate::Taken && w.TakenFrom == 22 && w.FromAway && v.Now - 1200 == Limit, "a holder that goes away starts the limit over");
        Leave(goes, w.Got, 11);

        // The holder changes 200 ms in: the limit counts for the new one from there.
        std::atomic<Word> turns{ Away22 };
        WaitClock e;
        w = wait(turns, e, yes, [&](WaitClock& k) {
            k.Now += 1;
            if (k.Now == 1200) turns.store(Away33);
        });
        Check(w.Got == Gate::Taken && w.TakenFrom == 33 && e.Now - 1200 == Limit, "a new holder starts the limit over, and the gate is taken from it");
        Leave(turns, w.Got, 11);

        // The stuck holder leaves and another thread enters right as the limit runs out (inside ours, the last thing asked
        // before the take): that thread is at the start of its own work, and keeps the gate until it leaves.
        std::atomic<Word> late{ Away22 };
        WaitClock f;
        w = wait(
            late, f,
            [&](WaitClock& k) {
                if (k.OursAsked == 1) late.store(33);
                return true;
            },
            [&](WaitClock& k) {
                k.Now += 1;
                if (k.Now == 1000 + Limit + 10) Leave(late, Gate::Taken, 33);
            });
        Check(w.Got == Gate::Taken && w.TakenFrom == 0 && f.OursAsked == 1 && f.Now - 1000 == Limit + 10,
              "the gate is never taken from a thread that entered after the last try");
        Leave(late, w.Got, 11);

        // Back from its call as the limit runs out: in its own work again, it is not taken from.
        std::atomic<Word> back{ Away22 };
        WaitClock r;
        w = wait(
            back, r,
            [&](WaitClock& k) {
                if (k.OursAsked == 1) ComeBack(back, 22);
                return true;
            },
            [&](WaitClock& k) {
                k.Now += 1;
                if (k.Now == 1000 + Limit + 10) Leave(back, Gate::Taken, 22);
            });
        Check(w.Got == Gate::Taken && w.TakenFrom == 0 && r.OursAsked == 1 && r.Now - 1000 == Limit + 10,
              "nor from a holder that came back from its call after the last try: the compare sees it");
        Leave(back, w.Got, 11);

        std::atomic<Word> other{ Away22 };
        WaitClock d;
        w = wait(other, d, no, tick);
        Check(w.Got == Gate::Busy && w.TakenFrom == 0 && other.load() == Away22 && d.Now - 1000 == Limit && waiting.load() == 0,
              "on another swap chain the gate stays with an away holder past the limit: nothing of ours to let go of");

        // A resize inside this thread's own away call (the window's, a layer's): held again for it, then away again.
        std::atomic<Word> nested{ 11 | AwayMark };
        WaitClock n;
        w = wait(nested, n, yes, tick);
        Check(w.Got == Gate::Reclaimed && nested.load() == 11 && n.Pauses == 0, "a resize inside its own thread's away call takes it back at once");
        WaitClock m;
        auto const third = EnterWaiting(nested, waiting, 33, Limit, Own, [&] { return m.Now; }, [&](unsigned) { m.Now += 1; }, [] { return true; });
        Check(third.TakenFrom == 11 && !third.FromAway && m.Now - 1000 == Own, "a resize elsewhere can't take it from it at the away limit meanwhile");
        Leave(nested, third.Got, 33);
        nested.store(11);
        Leave(nested, w.Got, 11);
        Check(nested.load() == (11 | AwayMark), "its Leave hands it back to the away call");
        Check(ComeBack(nested, 11) && nested.load() == 11, "whose ComeBack finds it its own");
    }

    /// A holder (MovieIdle's thread, 7) and a resize (11) on real threads. The holder enters, works, goes away for an
    /// original, makes a resize of its own from inside it at times (Reclaimed), comes back, works again if the gate is
    /// still its own, and leaves, over and over. The resize waits for no away holder and for every one in our own work.
    /// Never two at work at once, and the gate is taken from the away holder.
    void AwayOnThreads()
    {
        constexpr int Rounds = 4000;
        std::atomic<Word> owner{ 0 };
        std::atomic<unsigned> waiting{ 0 };
        std::atomic<int> inside{ 0 };
        std::atomic<int> most{ 0 };
        std::atomic<int> takes{ 0 };
        std::atomic<int> lost{ 0 };
        std::atomic<int> reclaimed{ 0 };
        std::atomic<int> wrong{ 0 };
        std::atomic<bool> done{ false };
        std::atomic<std::uint64_t> clock{ 0 };
        auto const work = [&](std::uint32_t self) {
            auto const now = inside.fetch_add(1) + 1;
            for (auto m = most.load(); now > m && !most.compare_exchange_weak(m, now);) {}
            for (int spin = 0; spin < 64; spin++) std::atomic_signal_fence(std::memory_order_seq_cst);
            // Nobody took it meanwhile: a take from our own work would show here.
            if (HolderOf(owner.load()) != self || IsAway(owner.load())) wrong++;
            inside.fetch_sub(1);
        };
        auto const enterWaiting = [&](std::uint32_t self, std::uint64_t ownLimit) {
            return EnterWaiting(
                owner, waiting, self, 0, ownLimit, [&] { return clock.fetch_add(1); }, [](unsigned) { std::this_thread::yield(); }, [] { return true; });
        };
        std::thread holder([&] {
            for (int r = 0; r < Rounds; r++)
            {
                auto const gate = Enter(owner, 7);
                if (gate != Gate::Taken)
                {
                    std::this_thread::yield();
                    continue;
                }
                work(7);
                if (!GoAway(owner, 7)) wrong++;
                if (r % 3 == 0)
                {
                    // A resize of this thread's own from inside the original: the gate back for it, or the resize's.
                    auto const nested = enterWaiting(7, ~std::uint64_t{ 0 });
                    if (nested.Got == Gate::Reclaimed) reclaimed++;
                    if (nested.Got == Gate::Taken || nested.Got == Gate::Reclaimed) work(7);
                    else wrong++;
                    Leave(owner, nested.Got, 7);
                }
                // The rest of the original: long enough for the resize to find it away twice, or taken.
                for (int spin = 0; spin < 20000 && owner.load() == (7 | AwayMark); spin++) std::this_thread::yield();
                if (ComeBack(owner, 7)) work(7);
                else lost++;
                Leave(owner, gate, 7);
            }
            done.store(true);
        });
        std::thread resize([&] {
            while (!done.load())
            {
                auto const w = enterWaiting(11, ~std::uint64_t{ 0 });
                if (w.Got != Gate::Taken)
                {
                    wrong++;
                    continue;
                }
                if (w.TakenFrom != 0) takes++;
                if (w.TakenFrom != 0 && !w.FromAway) wrong++;
                work(11);
                Leave(owner, w.Got, 11);
            }
        });
        holder.join();
        resize.join();
        Check(most.load() == 1 && wrong.load() == 0, "a holder and a resize on two threads: never both at work, and nothing taken from our own work");
        Check(takes.load() > 0 && lost.load() > 0 && reclaimed.load() > 0 && owner.load() == 0,
              "the gate taken from the away holder, which found it gone, a nested resize reclaimed it, and it is free at the end");
    }

    /// MovieIdle on its own thread (7) as a paused movie runs it at 60 Hz: its BinkWait comes 4 ms after its last present
    /// ended, takes the gate (EnterIdle) and holds it 16 ms across the next, vsync-bound. A resize polls every 20 ms, each
    /// time while that present is out, the worst a coarse Sleep(1) can do.
    void IdleGivesWay()
    {
        std::atomic<unsigned> waiting{ 0 };
        std::atomic<Word> owner{ 0 };
        std::uint64_t now = 0;
        auto holding = false;
        auto presents = 0;
        // Its life from now to <until>: a present ends at each 20 ms, its next BinkWait comes 4 ms later.
        auto const idleTo = [&](std::uint64_t until) {
            for (auto t = now + 1; t <= until; t++)
            {
                if (t % 20 == 0 && holding)
                {
                    ComeBack(owner, 7);
                    Leave(owner, Gate::Taken, 7);
                    holding = false;
                }
                if (t % 20 == 4 && !holding && EnterIdle(owner, waiting, 7) == Gate::Taken)
                {
                    // Its frame, then away for the original.
                    GoAway(owner, 7);
                    holding = true;
                    presents++;
                }
            }
            now = until;
        };
        idleTo(10);
        Check(holding, "MovieIdle holds the gate across its present when the resize comes");
        auto const start = now;
        auto const w = EnterWaiting(
            owner, waiting, 11, ResizeWaitMs, ResizeWaitOwnMs, [&] { return now; }, [&](unsigned) { idleTo(now + 20); }, [] { return true; });
        Check(w.Got == Gate::Taken && w.TakenFrom == 0 && now - start <= 20, "MovieIdle takes no gate while a resize waits: it gets it after the present in flight");
        Check(EnterIdle(owner, waiting, 7) == Gate::Busy, "and MovieIdle finds it busy until the resize is done");
        Leave(owner, w.Got, 11);
        auto const before = presents;
        idleTo(now + 40);
        Check(presents > before, "then presents again");
    }

    // ! The game's window
    // ========================================

    void GameWindow()
    {
        constexpr std::uint32_t Main = 100;
        constexpr std::uint32_t Game = 200;
        constexpr std::uint32_t Other = 300;
        Check(IsGameWindow(true, Main, Main, 0), "a window of ours made by the main thread is the game's, before the game thread is known");
        Check(IsGameWindow(true, Main, Main, Main), "and after (UE3: the game thread is the main thread)");
        Check(IsGameWindow(true, Game, Main, Game), "one made by the game thread, once it is known");
        Check(!IsGameWindow(true, Game, Main, 0), "but not before");
        Check(!IsGameWindow(true, Other, Main, Game), "one made by a thread of its own (an ASI's, a tool's) is not the game's");
        Check(!IsGameWindow(false, Main, Main, Game), "another process's window is not, whichever its thread id");
        Check(!IsGameWindow(true, 0, Main, Game) && !IsGameWindow(true, 0, 0, 0), "a window whose maker is unknown is not, also with no thread known");
        Check(IsGameWindow(true, Game, 0, Game) && !IsGameWindow(true, Other, 0, 0), "with the main thread unread, the game thread alone decides");
    }

    // ! Start
    // ========================================

    void StartChoice()
    {
        constexpr std::uint64_t T = 1000000;
        StartClock first;
        Check(Judge(first, true, true, false, false, T) == Take::Game && first.Refused == 0 && first.WaitFrom == 0,
              "the game's swap chain is taken at its first present, in the foreground or not");

        // While the game boots (its thread not known), refusals start no clock: a minute of them takes nothing.
        StartClock c;
        auto refusedAll = true;
        for (std::uint64_t t = T; t < T + 60000; t += 1000) refusedAll = refusedAll && Judge(c, true, false, true, false, t) == Take::Refuse;
        Check(refusedAll && c.WaitFrom == 0 && c.Refused == 60, "refusals before the game thread is known start no clock");
        auto const from = T + 60000;
        Check(Judge(c, true, false, true, true, from) == Take::Refuse && c.WaitFrom == from, "the clock starts at the first refusal once it is known");
        Check(Judge(c, true, false, true, true, from + StartAnywayMs - 1) == Take::Refuse, "a swap chain not the game's is left alone until StartAnywayMs");
        Check(Judge(c, false, false, true, true, from + StartAnywayMs) == Take::Refuse && Judge(c, false, true, true, true, from + 600000) == Take::Refuse,
              "one that is not D3D11 is never taken, past StartAnywayMs too");
        Check(Judge(c, true, false, false, true, from + StartAnywayMs) == Take::Refuse && Judge(c, true, false, false, true, from + 600000) == Take::Refuse,
              "nor one on a window the player doesn't look at: the game's and another one both present by then, and the first would win");
        auto const refused = c.Refused;
        Check(Judge(c, true, false, true, true, from + StartAnywayMs) == Take::Anyway && c.Refused == refused + 1 && c.WaitFrom == from,
              "a D3D11 one on the foreground window is taken anyway StartAnywayMs after the clock started, counted with the refusals");

        StartClock n;
        Check(Judge(n, false, false, false, true, T) == Take::Refuse && n.WaitFrom == T, "a swap chain that is not D3D11 starts the clock too");
        Check(Judge(n, true, false, false, true, T + StartAnywayMs) == Take::Refuse && n.WaitFrom == T, "as does one in the background, which keeps it running");
        Check(Judge(n, true, false, true, true, T + StartAnywayMs + 1) == Take::Anyway, "and a D3D11 one is taken once its window comes to the foreground");

        StartClock g;
        Judge(g, true, false, true, true, T);
        Check(Judge(g, true, true, false, true, T + 1) == Take::Game && g.Refused == 1, "the game's after a refusal is the game's, the refusal counted for its line");
    }

    // ! Another swap chain once started
    // ========================================

    void OtherChains()
    {
        auto const o250 = OtherChain(BannerAfterMs);
        auto const o251 = OtherChain(BannerAfterMs + 1);
        auto const o499 = OtherChain(AdoptAfterMs - 1);
        auto const o500 = OtherChain(AdoptAfterMs);
        Check(!o250.Pending && o251.Pending && !o499.Adopt && o500.Adopt && o500.Pending,
              "another swap chain: the banner after BannerAfterMs of silence from ours, Adopt tried after AdoptAfterMs");
        auto same = true;
        auto any = false;
        for (std::uint64_t since = 0; since <= 2 * AdoptAfterMs; since++)
        {
            auto const other = OtherChain(since);
            auto const by = PassBy(true, false, since);
            any = any || by;
            // A present passed by is one Frame does nothing for; one Frame does nothing for while ours presents is passed by.
            same = same && (by == (!other.Pending && !other.Adopt));
        }
        Check(any && same, "BeforePresent passes by, without the gate, exactly the presents for which Frame does nothing");
        Check(!PassBy(true, true, 0) && !PassBy(false, false, 0), "never the swap chain we draw on, nor any while the overlay is not drawing (Start, Off)");
    }

    void Adopting()
    {
        constexpr auto G = Take::Game;
        constexpr auto A = Take::Anyway;
        // Readopt(drawn, sameWindow, gameWindow, drawnWindowAlive, foreground)
        Check(Readopt(G, true, false, true, false) == G && Readopt(G, true, true, false, false) == G,
              "a swap chain on the window we draw on is the game's new one");
        Check(Readopt(G, false, true, true, false) == G, "as is one on a window of the game's, the window we draw on still there");
        Check(Readopt(G, false, false, true, true) == Take::Refuse && Readopt(A, false, false, true, true) == Take::Refuse,
              "one on another window next to a live window of ours is someone else's, in the foreground too: left alone");
        Check(Readopt(G, false, false, false, false) == Take::Refuse && Readopt(A, false, false, false, false) == Take::Refuse,
              "once the window we draw on is gone, a swap chain on a background window is still refused: it would win the race to be first");
        Check(Readopt(G, false, true, false, false) == G, "and the game's new window is taken, foreground or not");
        Check(Readopt(G, false, false, false, true) == Take::Refuse, "an overlay the rule started takes no other window, the foreground one neither");
        Check(Readopt(A, false, false, false, true) == A, "an overlay taken anyway follows the game to the foreground window once its own is gone");
        Check(Readopt(A, true, false, true, false) == A && Readopt(A, false, true, false, false) == G,
              "taken anyway stays so on its own window, and turns the game's on a window the rule takes");
    }
}

void PauseAnytime::Tests::RunOverlayRules()
{
    GateOnOneThread();
    GateAway();
    GateOnThreads();
    Waiting();
    AwayOnThreads();
    IdleGivesWay();
    GameWindow();
    StartChoice();
    OtherChains();
    Adopting();
}
