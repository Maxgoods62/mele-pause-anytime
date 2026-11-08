#pragma once

#include <atomic>
#include <cstdint>

// The overlay's decisions that other threads race for, as plain functions: the gate, which one thread at a time takes for
// our work (Overlay.cpp's presents, MovieIdle and resizes, on whichever thread calls them), which swap chain is the game's
// (IsGameWindow, Start) and which present has nothing for us to do. tests/OverlayRulesTest.cpp runs them on every build
// (../build.sh), the gate on real threads too. No game, SDK or Windows type here: thread ids and ticks come in as plain
// values, the clock and the sleep of a wait as callables. Overlay.cpp keeps the Win32 and DXGI calls, its state and every
// log line.
namespace PauseAnytime::OverlayRules
{
    /// A tick written on another thread may be a little ahead of ours.
    constexpr std::uint64_t Since(std::uint64_t now, std::uint64_t then) { return now > then ? now - then : 0; }

    // ! The gate
    // ========================================

    // R, ImGui's context, the blur and our use of the device's immediate context belong to one thread at a time, the one
    // the gate's word names (0: none). Each call of ours takes it first, and a present never waits for it: one that finds
    // it held passes through untouched, a frame without the pause screen at worst. It is no std::mutex: the frames that take
    // it have __try blocks and hold no C++ object, so it is given back in their __finally blocks (Overlay.cpp says why).

    /// The gate's word: the holder's thread id in the low half (0: none), and AwayMark while that holder is inside an
    /// original call it makes with the gate, one we don't control (MovieIdle's present, a resize of the swap chain we draw
    /// on), after which it does nothing more with R until it leaves (GoAway, ComeBack). A resize takes the gate from such a
    /// holder only (EnterWaiting), so that no two threads are ever at work on R at once.
    using Word = std::uint64_t;
    inline constexpr Word AwayMark = Word{ 1 } << 32;
    constexpr std::uint32_t HolderOf(Word word) { return static_cast<std::uint32_t>(word); }
    constexpr bool IsAway(Word word) { return (word & AwayMark) != 0; }

    enum class Gate : unsigned char
    {
        /// Taken by this call: given back by Leave on every way out of the frame that took it.
        Taken,
        /// This thread holds it further up its stack, in our work (MovieIdle's own present): used, not given back here.
        Held,
        /// This thread holds it further up its stack, away in an original call (GoAway), and this resize came back into ours
        /// from inside it (a layer's, or the window's, on the same thread): held again for this call, so that no resize
        /// takes it meanwhile, and away again at its Leave. Only EnterWaiting gives it.
        Reclaimed,
        /// Another thread holds it, or this one away up its stack and the call is a present: none of our work in this call.
        Busy,
    };

    /// <paramref name="self"/> is the calling thread's id, never 0. A present inside this thread's own away call passes
    /// through (Busy): that holder's state is R's, and our hook drew its frame before.
    inline Gate Enter(std::atomic<Word>& owner, std::uint32_t self)
    {
        Word was = 0;
        if (owner.compare_exchange_strong(was, self, std::memory_order_acquire, std::memory_order_relaxed)) return Gate::Taken;
        return was == self ? Gate::Held : Gate::Busy;
    }

    /// Before an original call made with the gate, after which this thread does nothing more with R until it leaves: marks
    /// it away, if <paramref name="self"/> holds the gate here, in our work. True if marked: ComeBack is then due on every
    /// way out of that call.
    inline bool GoAway(std::atomic<Word>& owner, std::uint32_t self)
    {
        Word held = self;
        // Release: a resize that takes the gate from the away holder sees all it did to R before.
        return owner.compare_exchange_strong(held, self | AwayMark, std::memory_order_release, std::memory_order_relaxed);
    }

    /// Back from that call: true if the gate is still this thread's, in our work again; false if a resize took it meanwhile
    /// (EnterWaiting). R is then that resize's: this thread does nothing more with it, and its Leave gives nothing back.
    inline bool ComeBack(std::atomic<Word>& owner, std::uint32_t self)
    {
        Word away = self | AwayMark;
        return owner.compare_exchange_strong(away, self, std::memory_order_acquire, std::memory_order_relaxed);
    }

    /// This thread holds the gate, in our work: R is its own.
    inline bool Holds(std::atomic<Word> const& owner, std::uint32_t self) { return owner.load(std::memory_order_acquire) == self; }

    /// Gives the gate back if this call took it and <paramref name="self"/> still holds it, away too: a second Leave, after
    /// an early one, does nothing, also once another thread has taken the gate since. A Reclaimed gate goes back to away,
    /// for the call further up this thread's stack.
    inline void Leave(std::atomic<Word>& owner, Gate gate, std::uint32_t self)
    {
        if (gate == Gate::Reclaimed)
        {
            GoAway(owner, self);
            return;
        }
        if (gate != Gate::Taken) return;
        Word held = self;
        if (owner.compare_exchange_strong(held, 0, std::memory_order_release, std::memory_order_relaxed)) return;
        // Still away: a ComeBack skipped would otherwise keep the gate from every call for good.
        if (held == (self | AwayMark)) owner.compare_exchange_strong(held, 0, std::memory_order_release, std::memory_order_relaxed);
    }

    /// How long a resize waits for a holder away in a call we don't control before it takes the gate from it: that call
    /// may wait on the resizing thread (a full-screen present that sends this thread's window a message, a driver).
    inline constexpr std::uint64_t ResizeWaitMs = 250;
    /// How long it waits for a holder in our own work, which waits on no other thread and so ends: the longest of it is
    /// Start's and Adopt's shader builds, six small shaders. Past this, something we can't see holds it (a lock of a layer
    /// around both, a hung driver), and the resize goes on rather than hang the game.
    inline constexpr std::uint64_t ResizeWaitOwnMs = 2000;

    /// What a resize got of the gate (EnterWaiting).
    struct Waited
    {
        /// Taken, Held or Reclaimed; Busy when the wait ran out on a swap chain we don't draw on, and the gate stays with
        /// its holder.
        Gate Got = Gate::Busy;
        /// The thread the gate was taken from when the wait ran out on the swap chain we draw on; 0 otherwise. Its own
        /// Leave then gives nothing back, since the gate is no longer its own, and every other call finds the gate busy
        /// until the resize leaves.
        std::uint32_t TakenFrom = 0;
        /// That thread was away in a call we don't control, as the rule asks. False: it was in our own work past
        /// ResizeWaitOwnMs, the last resort, and may still be at work on R.
        bool FromAway = false;
    };

    /// Enter for a resize, which must let go of the back buffer of the swap chain we draw on before the original runs: tried
    /// again, <paramref name="pause"/>(tries) between tries, by <paramref name="now"/>()'s clock. Meanwhile <paramref
    /// name="waiting"/> counts it, and MovieIdle, which holds the gate across a present and its original, takes none
    /// (EnterIdle): the wait ends with the present in flight. A holder found away (GoAway) at every try for <paramref
    /// name="awayLimitMs"/> is stuck in a call we don't control. For the swap chain we draw on (<paramref name="ours"/>(),
    /// asked only then) the gate is then taken from it: its back buffer must be let go of, and the holder, back from that
    /// call, does nothing more with R (ComeBack). For any other, nothing of ours is on it to let go of, and Got is Busy. A
    /// holder in our own work is never taken from at that limit, whatever DrawnChain says at it, and is waited for: that
    /// work ends, and may be about to draw on the swap chain resized (Adopt's, past its shader builds). Only past <paramref
    /// name="ownLimitMs"/> is it taken from (FromAway false) or, for another swap chain, left (Busy): a resize never hangs,
    /// nor fails for a back buffer we hold. Each limit counts from the first try that found the holder in that state: a
    /// holder going away, or another one, starts it over, and the gate is taken from the one it counted for only, by a
    /// compare: one that had just entered or come back would be at the start of its work. A holder that leaves and enters
    /// again between two tries counts as one. A count, not a flag: two threads may resize at once.
    template <class Now, class Pause, class Ours>
    Waited EnterWaiting(std::atomic<Word>& owner, std::atomic<unsigned>& waiting, std::uint32_t self, std::uint64_t awayLimitMs, std::uint64_t ownLimitMs,
                        Now now, Pause pause, Ours ours)
    {
        // A hint, not a lock: the gate alone keeps the threads apart.
        waiting.fetch_add(1, std::memory_order_relaxed);
        Waited w;
        Word holder = 0;
        std::uint64_t heldSince = 0;
        for (unsigned tries = 0;; tries++)
        {
            Word was = 0;
            if (owner.compare_exchange_strong(was, self, std::memory_order_acquire, std::memory_order_relaxed))
            {
                w.Got = Gate::Taken;
                break;
            }
            if (was == self)
            {
                w.Got = Gate::Held;
                break;
            }
            if (was == (self | AwayMark))
            {
                // Back in ours from this thread's own away call: held again, unless a resize took it since the try.
                if (owner.compare_exchange_strong(was, self, std::memory_order_acquire, std::memory_order_relaxed))
                {
                    w.Got = Gate::Reclaimed;
                    break;
                }
                continue;
            }
            auto const t = now();
            if (was != holder)
            {
                holder = was;
                heldSince = t;
            }
            else if (Since(t, heldSince) >= (IsAway(was) ? awayLimitMs : ownLimitMs))
            {
                if (!ours()) break;
                if (owner.compare_exchange_strong(was, self, std::memory_order_acq_rel, std::memory_order_relaxed))
                {
                    w.Got = Gate::Taken;
                    w.TakenFrom = HolderOf(holder);
                    w.FromAway = IsAway(holder);
                    break;
                }
                // It changed hands since the try, or its holder came back: the next try finds the gate free, or counts for
                // what it finds.
                holder = 0;
                continue;
            }
            pause(tries);
        }
        waiting.fetch_sub(1, std::memory_order_relaxed);
        return w;
    }

    /// Enter for MovieIdle, which holds the gate across its present, the original included: Busy while a resize waits for
    /// the gate (EnterWaiting), so that it waits for the present in flight, not for a run of them.
    inline Gate EnterIdle(std::atomic<Word>& owner, std::atomic<unsigned> const& waiting, std::uint32_t self)
    {
        if (waiting.load(std::memory_order_relaxed) != 0) return Gate::Busy;
        return Enter(owner, self);
    }

    // ! The game's window
    // ========================================

    /// The game's window: one of this process's (<paramref name="ours"/>), made (<paramref name="maker"/>: the thread that
    /// made it, 0 if unknown) by the process's main thread, or by the game thread once it is known (0: not yet, as at the first
    /// presents). UE3 runs its game thread on the main thread, and that thread makes the viewport's window: the engine's
    /// design as known, not read in LE's code. A window an ASI or a tool makes on a thread of its own is not taken for the
    /// game's. <paramref name="mainThread"/> is 0 when it couldn't be read.
    constexpr bool IsGameWindow(bool ours, std::uint32_t maker, std::uint32_t mainThread, std::uint32_t gameThread)
    {
        if (!ours || maker == 0) return false;
        return maker == mainThread || (gameThread != 0 && maker == gameThread);
    }

    // ! Which swap chain is the game's (Start)
    // ========================================

    /// A swap chain IsGameWindow refuses is taken anyway once refusals have gone on this long of the game's frames with none
    /// taken: the first one then that presents to the foreground window, the one the player looks at. Should the rule be
    /// wrong for some setup, the pause screen starts late, not never, as before the rule, when the first swap chain to present
    /// was taken. The price is near the old one: a swap chain of someone else's on the foreground window is taken in place of
    /// the game's (the start line names its window and who made it).
    inline constexpr std::uint64_t StartAnywayMs = 5000;

    /// Start's presents so far, while no swap chain is taken.
    struct StartClock
    {
        /// The presents refused.
        unsigned Refused = 0;
        /// The tick of the first refusal once the game thread was known (0: none yet), from which StartAnywayMs counts.
        std::uint64_t WaitFrom = 0;
    };

    enum class Take : unsigned char
    {
        /// The game's: the overlay starts on it.
        Game,
        /// Not known for the game's, taken anyway: StartAnywayMs of refusals have gone by, and it presents to the foreground
        /// window.
        Anyway,
        /// Left alone: this present passes through as if we weren't there, and a later one is tried.
        Refuse,
    };

    /// Start's choice for one present's swap chain: <paramref name="d3d11"/>, it is one we can draw on (a D3D11 device
    /// behind it); <paramref name="gameWindow"/>, IsGameWindow takes its window; <paramref name="foreground"/>, its window is
    /// the foreground one (or inside it); <paramref name="gameThreadKnown"/>, Pause.cpp knows the game thread. StartAnywayMs
    /// counts only while the game runs its frames: from the first refusal once Pause.cpp knows the game thread, at its first
    /// Console.InputKey or BioHUD.PostRender. In the 2026-11-08 logs of LE1, LE2 and LE3 the first BioHUD.PostRender came half a
    /// second after the game's first present, and 8 to 11 s after our hooks went live. A window of someone else's that
    /// presents while the game boots must not use the wait up. Past it, only a swap chain on the foreground window is taken:
    /// the game's and someone else's both present every frame by then, and the first to present would win a race. A swap
    /// chain we can't draw on is never taken. Every refusal counts, the one taken anyway too.
    constexpr Take Judge(StartClock& c, bool d3d11, bool gameWindow, bool foreground, bool gameThreadKnown, std::uint64_t now)
    {
        if (d3d11 && gameWindow) return Take::Game;
        c.Refused++;
        if (c.WaitFrom == 0 && gameThreadKnown) c.WaitFrom = now;
        if (!d3d11 || !foreground || c.WaitFrom == 0 || now - c.WaitFrom < StartAnywayMs) return Take::Refuse;
        return Take::Anyway;
    }

    // ! Another swap chain once started (Frame, Adopt, BeforePresent)
    // ========================================

    /// Another swap chain presents while the one we draw on has been silent this long: the game's new one, maybe, waiting to
    /// be taken. Nothing is drawn meanwhile, so Overlay::Drawing is false and the Canvas banner shows (AdoptPending). Not
    /// set by a mere hitch, after which ours presents first.
    inline constexpr std::uint64_t BannerAfterMs = 250;
    /// A present from another swap chain is the game's new one (a display mode change) once the one we draw on has been
    /// silent this long, if Readopt takes it; before, it is someone else's next to it, left alone.
    inline constexpr std::uint64_t AdoptAfterMs = 500;

    struct Other
    {
        /// AdoptPending is set: the banner shows.
        bool Pending = false;
        /// Adopt is tried.
        bool Adopt = false;
    };

    /// Frame's choice for a present of another swap chain than the one we draw on, which last presented <paramref
    /// name="since"/> ms ago.
    constexpr Other OtherChain(std::uint64_t since) { return { since > BannerAfterMs, since >= AdoptAfterMs }; }

    /// A present Frame would only pass by (OtherChain does nothing): the overlay is drawing (<paramref name="live"/>), on
    /// another swap chain than this one (<paramref name="drawnChain"/> false), which presented <paramref name="since"/> ms
    /// ago. BeforePresent lets it through without the gate, so that it can't keep the game's present from the gate: that
    /// frame would go out without the pause screen.
    constexpr bool PassBy(bool live, bool drawnChain, std::uint64_t since) { return live && !drawnChain && since <= BannerAfterMs; }

    /// Adopt's choice for another swap chain's present, once the one we draw on has gone silent (OtherChain): how the overlay
    /// then comes to draw on it, as Start's Take, or Refuse. <paramref name="drawn"/> is how it came to the swap chain it
    /// draws on: Game, by IsGameWindow, or Anyway (Judge). One on a window IsGameWindow takes (<paramref name="gameWindow"/>:
    /// the game may make its window again at a mode change) is the game's; one on the window we draw on (<paramref
    /// name="sameWindow"/>) is taken as that window was. Any other is someone else's, which once taken would keep the game's
    /// from ever being taken back (and the Canvas banner hidden), with one escape: an overlay taken anyway, whose window is
    /// gone (<paramref name="drawnWindowAlive"/> false), follows the game to a swap chain on the foreground window (<paramref
    /// name="foreground"/>), as Judge took its first. Never to one in the background, which presents next to the game's new
    /// swap chain and would win a race to be first, and never for an overlay the rule started: the rule finds the game's new
    /// window as it found the first.
    constexpr Take Readopt(Take drawn, bool sameWindow, bool gameWindow, bool drawnWindowAlive, bool foreground)
    {
        if (gameWindow) return Take::Game;
        if (sameWindow) return drawn;
        if (drawn == Take::Anyway && !drawnWindowAlive && foreground) return Take::Anyway;
        return Take::Refuse;
    }
}
