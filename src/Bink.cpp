#include "PauseAnytime/Api.hpp"
#include "PauseAnytime/KeyRules.hpp"
#include "PauseAnytime/MovieRules.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Bink 2 movies (the galaxy map's travel movies, ...) keep playing under the world pause: Esc pauses them with BinkPause
// instead. bink2w64.dll is the ASI loader's proxy and forwards every export to bink2w64_original.dll, where the hooks go.
// Esc is read here, in BinkWait, on the thread that plays the movie: during a fullscreen movie the engine's own movie input
// takes the press before Console.InputKey (only its release got there in the first test), while the player calls BinkWait
// some 250 times a second, paused or not. Each press toggles the pause of the fullscreen movie: one opened from a file
// (loading screens play from memory), calling BinkWait, while the HUD draws nothing (in play it draws every frame, so an
// in-level texture movie never takes Esc). Only atomics are read from the game on this thread, never a game object.
// The pause is applied by that thread at its BinkWait: Bink's calls on one movie belong on one thread. Only there: BinkPause
// makes BinkWait answer "wait" and stops the sound, so a player that doesn't call BinkWait would keep its picture going, and
// this Bink (2022.05) hangs a player that pauses and then stops calling BinkWait (fixed by RAD in 2025.05). A movie that
// hasn't called BinkWait lately isn't paused at all. A resume is applied at the movie's next call of any kind on the thread
// of its BinkWait, and before its close at the latest: the one exception to the one thread, made by whichever thread closes
// it, since no other call on that movie can overlap its BinkClose. Esc is read on the movie's own thread, so a silent game
// thread (a load, a wait for this very movie) neither keeps it from resuming nor lifts the pause. A pause whose movie has
// made no Bink call for 2 s reads as over at once (PauseWanted), and is dropped by the next Esc at the latest (its thread
// reads nothing any more: Esc goes back to the game). A gamepad's Start can't be read here (XInput goes through the game,
// and Steam Input may stand in for it): the engine's movie input takes its press too, but its repeats and its release reach
// Console.InputKey, where Pause.cpp toggles the same pause through Toggle, on the game thread. Every movie logs its threads,
// how often each call comes, and whether it was a fullscreen one.
// In LE2 (MoviePolicy::BinkAndEvent) most movies have no Bink audio: their sound is a Wwise event a Kismet movie action
// posts beside the picture. There a fullscreen movie takes the pause keys only if we know its sound: the event latched for
// it on the game thread, paused right before BinkPause and resumed right after it (MovieSound.cpp), or Bink audio (the
// game's list). Any other movie leaves Esc and Start to the game, which skips it. Wwise's answer decides: a pause it
// refuses leaves the picture playing too, and a resume it refuses is sent again a few times, and at the close.
namespace PauseAnytime::Bink
{
    namespace
    {
        using HBINK = void*;
        using t_BinkOpen = HBINK(char const* name, unsigned int flags);
        using t_BinkOpenWithOptions = HBINK(char const* name, void const* options, unsigned int flags);
        using t_BinkClose = void(HBINK bink);
        using t_BinkPause = int(HBINK bink, int pause);
        using t_BinkWait = int(HBINK bink);
        using t_BinkDoFrame = int(HBINK bink);
        using t_BinkDoFrameAsync = int(HBINK bink, unsigned int threadOne, unsigned int threadTwo);

        std::atomic<t_BinkOpen*> BinkOpen_orig{ nullptr };
        std::atomic<t_BinkOpenWithOptions*> BinkOpenWithOptions_orig{ nullptr };
        std::atomic<t_BinkClose*> BinkClose_orig{ nullptr };
        std::atomic<t_BinkWait*> BinkWait_orig{ nullptr };
        std::atomic<t_BinkDoFrame*> BinkDoFrame_orig{ nullptr };
        std::atomic<t_BinkDoFrameAsync*> BinkDoFrameAsync_orig{ nullptr };
        /// Not hooked: called by us only.
        std::atomic<t_BinkPause*> BinkPause_fn{ nullptr };

        // bink.h: the "name" is a file handle or a pointer to the file's bytes, not a path.
        constexpr unsigned int BINKFILEHANDLE = 0x00800000;
        constexpr unsigned int BINKFROMMEMORY = 0x04000000;

        /// A movie is paused only if it called BinkWait this recently: its player gates its frames on BinkWait. Polls of Esc
        /// further apart than this start over (a key already down then is no press), except while our movie pause is on.
        constexpr ULONGLONG WaitStaleMs = 500;
        /// A movie pause whose movie has made no Bink call for this long (no BinkWait, while the pause is not applied yet) is
        /// dropped by the next Esc at the latest (or another movie's poll), and PauseWanted reads it as over at once: nothing
        /// reads Esc for it any more, so Esc goes back to the game.
        constexpr ULONGLONG PausedSilentMs = 2000;
        /// A press of Esc this soon after the last one acted on is a bounce.
        constexpr ULONGLONG DebounceMs = 150;
        /// Presses of Esc not acted on are logged at most this often.
        constexpr ULONGLONG RefusedLogMs = 2000;
        /// A press acted on keeps Pause.cpp's skip guard armed while it is down, this long at most.
        constexpr ULONGLONG EscHeldMaxMs = GuardMs;
        // A movie's pause and its Wwise event follow MovieRules.hpp's rules, which its tests run in every order of the threads'
        // steps: AkSuccess, the resends' limits and SoundState are there, beside the rules that use them.
        using MovieRules::AkSuccess;
        using MovieRules::SoundResends;
        using MovieRules::SoundState;

        enum class Call { Wait, DoFrame, DoFrameAsync };

        /// Calls of one kind on one movie since the last report.
        struct Counter
        {
            unsigned Calls = 0;
            unsigned Nonzero = 0;
            DWORD Thread = 0;
            bool OtherThreads = false;

            void Add(DWORD thread, int result)
            {
                Calls++;
                if (result != 0) Nonzero++;
                if (Thread == 0) Thread = thread;
                else if (Thread != thread) OtherThreads = true;
            }
        };

        /// Its pause (Want, Paused, WaitThread) and its Wwise event (SoundId, Sound, Resends) are MovieRules::State's fields,
        /// changed by MovieRules' functions only.
        struct Movie : MovieRules::State
        {
            HBINK Handle = nullptr;
            unsigned Serial = 0;
            std::string Name;
            /// Name without its folder and .bik, in lower case (BaseName).
            std::string Base;
            unsigned int Flags = 0;
            DWORD OpenThread = 0;
            ULONGLONG OpenedAt = 0;

            /// Opened from a file (a path or a file handle), not from memory: unknown for a movie first seen in a player call.
            bool FromFile = false;
            /// It was a fullscreen movie at one of its BinkWaits (logged once); else why not, as last seen.
            bool Fullscreen = false;
            char const* NotFullscreenWhy = nullptr;

            /// The latest ask of a pause or a resume (State::Want).
            ULONGLONG RequestedAt = 0;
            ULONGLONG LastWaitAt = 0;
            /// The latest hooked call of any kind: a pause whose movie stopped calling is dropped.
            ULONGLONG LastCallAt = 0;

            Counter Waits;
            Counter Frames;
            Counter AsyncFrames;
            ULONGLONG CountedSince = 0;
            unsigned Reports = 0;
        };

        /// Also taken on the game thread inside Pause.cpp's fault guards (OwnsKeys, Toggle, RequestResume). Under /EHsc a fault
        /// the guard takes while it is held may skip its unlock, and the movie's thread would then wait at its next BinkWait
        /// for good. So nothing done under it reads the game's or Bink's memory unguarded (ReadHeader and ReadPlayer guard
        /// their own reads): only a corrupt heap could fault there.
        std::mutex Lock;
        std::vector<Movie> Movies;
        std::atomic<int> OpenCount{ 0 };
        /// Of those, the ones MayPause takes: Overlay.cpp keeps a frame for them.
        std::atomic<int> PausableCount{ 0 };
        /// MovieSound::Changes() when Recount last ran: under Lock.
        unsigned SoundsSeen = 0;
        std::atomic<unsigned> Serial{ 0 };
        std::atomic<bool> Wanted{ false };
        /// While Wanted: LifeOf the movie whose pause we want. PauseWanted reads it with no lock, so a pause that DropStale would
        /// drop reads as over before anything calls DropStale.
        std::atomic<ULONGLONG> WantedLife{ 0 };
        /// BinkPause, BinkWait's hook and BinkClose's are there.
        std::atomic<bool> CanPause{ false };
        /// Set by Install before any hook, under Lock: read under it. The Bink-audio movies as BaseName gives them.
        MoviePolicy Policy = MoviePolicy::Bink;
        std::vector<std::string> BinkAudio;
        /// The language suffixes of localized movie files, in lower case, no underscore.
        std::vector<std::string> Languages;

        /// Esc as the BinkWait polls see it, on whichever thread: under Lock. Its edge (Down, PolledAt, PressedAt, Taken) is
        /// KeyRules::Polled, which PollEdge keeps.
        struct EscPoll : KeyRules::Polled
        {
            ULONGLONG RefusedLoggedAt = 0;
            unsigned RefusedUnlogged = 0;
        };
        EscPoll Poller;
        /// The latest poll that saw a taken press down: Pause.cpp's skip guard reads it on the game thread.
        std::atomic<ULONGLONG> EscDownTick{ 0 };
        /// The space bar, polled the same way: it only resumes.
        EscPoll SpacePoller;
        std::atomic<ULONGLONG> SpaceDownTick{ 0 };
        // KeyRules::PollEdge decides each poll's edge with its own copy of these times, so that its tests build with no
        // Windows type.
        static_assert(KeyRules::PollGapMs == WaitStaleMs && KeyRules::DebounceMs == DebounceMs && KeyRules::HeldMaxMs == EscHeldMaxMs,
                      "KeyRules.hpp and Bink.cpp disagree");

        /// BinkOpen may open through BinkOpenWithOptions: only the outer call tracks the movie.
        thread_local int OpenDepth = 0;

        /// The loader makes a hook live before it hands back the original: a call that early waits for it.
        template <typename T>
        T* Original(std::atomic<T*> const& orig)
        {
            auto f = orig.load(std::memory_order_acquire);
            while (f == nullptr)
            {
                YieldProcessor();
                f = orig.load(std::memory_order_acquire);
            }
            return f;
        }

        std::string ThreadText(DWORD thread)
        {
            if (thread == 0) return "none";
            auto const game = GameThread();
            return std::to_string(thread) + (game == 0 ? " (game thread not known yet)" : thread == game ? " (game thread)" : " (not the game thread)");
        }

        /// A tick written on another thread may be a little ahead of ours.
        ULONGLONG Since(ULONGLONG now, ULONGLONG then) { return now > then ? now - then : 0; }

        std::string HudText(ULONGLONG now, ULONGLONG hud)
        {
            return hud == 0 ? std::string{ "no BioHUD.PostRender yet" } : std::format("last BioHUD.PostRender {} ms ago", Since(now, hud));
        }

        std::string Describe(Counter const& c, double seconds)
        {
            if (c.Calls == 0) return "none";
            auto const rate = seconds > 0.0 ? static_cast<double>(c.Calls) / seconds : 0.0;
            return std::format("{} ({:.0f}/s, {} nonzero) on thread {}{}", c.Calls, rate, c.Nonzero, ThreadText(c.Thread),
                               c.OtherThreads ? " and others" : "");
        }

        /// The first fields of RAD's BINK struct (bink.h), for the logs only: they show whether a paused movie stays on
        /// one frame.
        struct Header
        {
            unsigned Width;
            unsigned Height;
            unsigned Frames;
            unsigned FrameNum;
        };

        /// No C++ object may live in this frame.
        bool ReadHeader(HBINK bink, Header* out)
        {
            __try
            {
                std::memcpy(out, bink, sizeof(Header));
                return true;
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                return false;
            }
        }

        std::string FrameText(HBINK bink)
        {
            Header h{};
            if (!ReadHeader(bink, &h)) return "frame unreadable";
            return std::format("frame {}/{}", h.FrameNum, h.Frames);
        }

        /// No C++ object may live in this frame.
        bool CopyName(char const* name, char* out, size_t size)
        {
            __try
            {
                size_t i = 0;
                for (; i + 1 < size && name[i] != '\0'; i++) out[i] = name[i];
                out[i] = '\0';
                return true;
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                return false;
            }
        }

        /// The movie player's fields as Install's MoviePlayerFields names them, for the logs only.
        struct PlayerRead
        {
            void const* Player;
            unsigned Flags;
            unsigned Skippable;
        };

        MoviePlayerFields PlayerFields;

        /// No C++ object may live in this frame. Reads only: the game thread may change the player meanwhile.
        bool ReadPlayer(PlayerRead* out)
        {
            __try
            {
                auto const exe = reinterpret_cast<unsigned char const*>(GetModuleHandleW(nullptr));
                auto const player = *reinterpret_cast<unsigned char const* const*>(exe + PlayerFields.Rva);
                out->Player = player;
                if (player == nullptr) return true;
                out->Flags = *reinterpret_cast<unsigned const*>(player + PlayerFields.Flags);
                out->Skippable = *reinterpret_cast<unsigned const*>(player + PlayerFields.Skippable);
                return true;
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                return false;
            }
        }

        /// The movie player's fields, for a movie line: empty when the game names none.
        std::string PlayerText()
        {
            if (PlayerFields.Rva == 0) return {};
            PlayerRead r{};
            if (!ReadPlayer(&r)) return "; player unreadable";
            if (r.Player == nullptr) return "; no player";
            return std::format("; player {}: flags 0x{:X}, skippable {}", r.Player, r.Flags, r.Skippable);
        }

        std::string NameText(char const* name, unsigned int flags)
        {
            if (name == nullptr) return "(null)";
            if (flags & BINKFILEHANDLE) return "(file handle)";
            if (flags & BINKFROMMEMORY) return "(from memory)";
            char buffer[260];
            if (!CopyName(name, buffer, sizeof(buffer))) return "(unreadable)";
            std::string text{ buffer };
            for (auto& c : text)
            {
                if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) c = '?';
            }
            return text;
        }

        /// A movie's file name without its folder and .bik, in lower case: how a Kismet movie action and the Bink-audio list
        /// name it (BinkOpen gets a path like ..\..\BioGame\Movies\GLO_01_Relay.bik).
        std::string BaseName(std::string_view path)
        {
            auto const slash = path.find_last_of("\\/");
            if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
            std::string base{ path };
            for (auto& c : base)
            {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            if (base.size() > 4 && base.ends_with(".bik")) base.resize(base.size() - 4);
            return base;
        }

        /// Under Lock. The file a Kismet movie action named, as BaseName gives both: the same name, or that name and one of
        /// the game's language suffixes (opening_sunmid_int for opening_sunmid).
        bool SameMovie(std::string_view named, std::string_view opened)
        {
            if (opened == named) return true;
            if (opened.size() <= named.size() + 1 || !opened.starts_with(named) || opened[named.size()] != '_') return false;
            return std::find(Languages.begin(), Languages.end(), opened.substr(named.size() + 1)) != Languages.end();
        }

        /// Under Lock.
        Movie* Find(HBINK bink)
        {
            auto const it = std::find_if(Movies.begin(), Movies.end(), [&](Movie const& m) { return m.Handle == bink; });
            return it != Movies.end() ? &*it : nullptr;
        }

        /// Under Lock: the movie of that handle while it is still the one with that serial. Apply finds its movie again this
        /// way once its calls are back: Movies may have moved meanwhile, and a handle closed and opened again is another movie.
        Movie* Find(HBINK bink, unsigned serial)
        {
            auto const m = Find(bink);
            return m != nullptr && m->Serial == serial ? m : nullptr;
        }

        /// Under Lock: a step of a movie's SoundState that MovieRules::SetSound took and LegalStep doesn't (a bug).
        void LogBadStep(Movie const& m, MovieRules::BadStep bad)
        {
            if (!bad.Any) return;
            static char const* const Names[] = { "None", "Pausing", "Paused", "Resuming" };
            LEASI_WARN("movie #{} '{}': Wwise state {} -> {} is not a step we take (a bug)", m.Serial, m.Name, Names[static_cast<int>(bad.From)],
                       Names[static_cast<int>(bad.To)]);
        }

        /// Under Lock: what our calls left on the movie's Wwise event, for the movie lines; empty when nothing of ours is on it.
        std::string SoundStateText(Movie const& m)
        {
            switch (m.Sound)
            {
            case SoundState::Pausing: return std::format(", Wwise pause of event {} sent", m.SoundId);
            case SoundState::Paused:
                if (m.Paused) return std::format(", Wwise event {} paused", m.SoundId);
                return std::format(", Wwise event {} still paused, its resume refused ({} sent again)", m.SoundId, m.Resends);
            case SoundState::Resuming: return std::format(", Wwise resume of event {} sent", m.SoundId);
            case SoundState::None: break;
            }
            return {};
        }

        bool MayPause(Movie const& m);

        /// Under Lock, for a movie whose pause we want: its latest sign of life, from which DropStale counts its silence. Once
        /// the pause is applied any Bink call counts. Before, only a BinkWait does, since only a BinkWait applies it: a movie
        /// that calls BinkDoFrame alone would keep a pause it never applies, and the pause keys with it.
        ULONGLONG LifeOf(Movie const& m) { return (std::max)(m.Paused ? m.LastCallAt : m.LastWaitAt, m.RequestedAt); }

        /// Under Lock.
        void Recount()
        {
            // Read first: a latch changed while this counts is counted again at the next poll.
            SoundsSeen = MovieSound::Changes();
            OpenCount.store(static_cast<int>(Movies.size()), std::memory_order_relaxed);
            PausableCount.store(static_cast<int>(std::count_if(Movies.begin(), Movies.end(), MayPause)), std::memory_order_relaxed);
            auto wanted = false;
            ULONGLONG life = 0;
            for (auto const& m : Movies)
            {
                if (!m.Want) continue;
                wanted = true;
                life = (std::max)(life, LifeOf(m));
            }
            // Before Wanted: its release store publishes it to PauseWanted.
            WantedLife.store(life, std::memory_order_relaxed);
            Wanted.store(wanted, std::memory_order_release);
        }

        /// Under Lock.
        Movie& Track(HBINK bink, std::string name, unsigned int flags)
        {
            auto const now = GetTickCount64();
            Movies.push_back({});
            auto& m = Movies.back();
            m.Handle = bink;
            m.Serial = Serial.fetch_add(1, std::memory_order_relaxed) + 1;
            m.Name = std::move(name);
            m.Base = BaseName(m.Name);
            m.Flags = flags;
            m.OpenThread = GetCurrentThreadId();
            m.OpenedAt = now;
            m.CountedSince = now;
            Recount();
            return m;
        }

        /// Under Lock. Why Esc wouldn't pause the movie, or null when it is a fullscreen one: opened from a file (loading
        /// screens play from memory), calling BinkWait, and BioHUD.PostRender silent (<paramref name="hud"/> is HudSeen) for a
        /// quarter second (an in-level texture movie plays under a HUD drawing every frame).
        char const* NotFullscreen(Movie const& m, ULONGLONG now, ULONGLONG hud)
        {
            if (!m.FromFile) return m.Flags & BINKFROMMEMORY ? "opened from memory, as loading screens are" : "opened before Pause Anytime, from where unknown";
            if (m.LastWaitAt == 0 || Since(now, m.LastWaitAt) >= WaitStaleMs) return "no BinkWait in the last half second";
            if (hud != 0 && Since(now, hud) <= HudSilentMs) return "the HUD draws";
            return nullptr;
        }

        /// A movie's sound as we know it.
        struct Sound
        {
            /// The Wwise event that plays it; 0 for Bink audio, or when unknown.
            unsigned Event = 0;
            /// Why we don't know it, or null when we do.
            char const* Unknown = nullptr;
        };

        /// Under Lock. Under Bink every movie carries Bink audio. Under BinkAndEvent its sound is known when the Kismet movie
        /// action latched last plays this very movie and posts a Wwise event (its id is not 0), else when its base name is on
        /// the game's Bink-audio list. The event comes first: a listed movie that has one too gets both paused.
        Sound SoundOf(Movie const& m)
        {
            if (Policy == MoviePolicy::Bink) return {};
            if (!MovieSound::Ready()) return { 0, "the exe exports no ExecuteActionOnEvent" };
            auto const latched = MovieSound::Current();
            auto const mine = !latched.Movie.empty() && SameMovie(BaseName(latched.Movie), m.Base);
            if (mine && latched.Beside) return { 0, "its Kismet runs more beside it, see the latch line" };
            if (mine && latched.Id != 0) return { latched.Id, nullptr };
            if (std::find(BinkAudio.begin(), BinkAudio.end(), m.Base) != BinkAudio.end()) return {};
            if (latched.Movie.empty()) return { 0, "no Kismet movie action latched" };
            if (!mine) return { 0, "the latched Kismet movie action plays another movie" };
            return { 0, "its Kismet movie action posts no Wwise event" };
        }

        /// Under Lock: SoundOf as MovieRules takes it.
        MovieRules::Latch LatchOf(Movie const& m)
        {
            auto const sound = SoundOf(m);
            return { sound.Unknown == nullptr, sound.Event };
        }

        /// Under Lock. Why Esc wouldn't pause the movie, or null: NotFullscreen's reasons, and under BinkAndEvent a sound we
        /// don't know (the movie is then the game's: Esc skips it).
        char const* NotPausable(Movie const& m, ULONGLONG now, ULONGLONG hud)
        {
            if (auto const why = NotFullscreen(m, now, hud); why != nullptr) return why;
            return Policy == MoviePolicy::BinkAndEvent && SoundOf(m).Unknown != nullptr ? "no known sound" : nullptr;
        }

        /// Under Lock. Esc could pause the movie once the HUD goes silent (NotPausable without its timing tests), or our pause
        /// holds it. A movie with no known sound under BinkAndEvent stays the game's, and its frames are not kept.
        bool MayPause(Movie const& m)
        {
            return m.Want || m.Paused || (m.FromFile && SoundOf(m).Unknown == nullptr);
        }

        /// Under Lock, under BinkAndEvent: the movie's sound and the latch, for its fullscreen line.
        std::string SoundText(Movie const& m)
        {
            auto const sound = SoundOf(m);
            auto const latched = MovieSound::Current();
            auto const latch = latched.Movie.empty() ? std::string{ "nothing latched" } : std::format("latched '{}' event {}", latched.Movie, latched.Id);
            if (sound.Unknown != nullptr) return std::format("no known sound ({}; {})", sound.Unknown, latch);
            return sound.Event != 0 ? std::format("its sound is Wwise event {} ({})", sound.Event, latch) : std::format("its sound is Bink audio ({})", latch);
        }

        /// Under Lock. The open movies, for the logs.
        std::string ListMovies(ULONGLONG now, ULONGLONG hud)
        {
            std::string text;
            for (auto const& m : Movies)
            {
                if (!text.empty()) text += ", ";
                auto const waited = m.LastWaitAt != 0 ? std::format("BinkWait {} ms ago", Since(now, m.LastWaitAt)) : std::string{ "BinkWait never" };
                auto const why = NotPausable(m, now, hud);
                auto const pending = m.Want && !m.Paused ? ", the pause not applied yet" : !m.Want && m.Paused ? ", the resume not applied yet" : "";
                text += std::format("#{} '{}' ({}, {}, {}{}{})", m.Serial, m.Name, m.Paused ? "paused" : "playing", waited, why != nullptr ? why : "fullscreen",
                                    pending, SoundStateText(m));
            }
            return text.empty() ? std::string{ "none" } : text;
        }

        /// Under Lock. Every movie we paused (or asked to) resumes at its next call. A movie the game paused itself stays as
        /// it is.
        void ResumeLocked(ULONGLONG now)
        {
            for (auto& m : Movies)
            {
                if (MovieRules::AskResume(m)) m.RequestedAt = now;
            }
            Recount();
        }

        /// Under Lock. Every second for 5 s after a pause or resume (and while one waits to be applied), every 10 s during a
        /// long pause, else after spans of 1, 2, 4, 8 and 16 s, then every 30 s: a looping texture movie writes little. The
        /// game thread's silence is for the logs only: does the game run under a fullscreen movie?
        void Report(Movie& m, ULONGLONG now, char const* why)
        {
            auto const seconds = static_cast<double>(Since(now, m.CountedSince)) / 1000.0;
            auto const game = GameThreadSeen();
            LEASI_INFO("movie #{} '{}' {}: {:.1f} s open, {}, paused {} (asked {}){}; over {:.1f} s: BinkWait {}, BinkDoFrame {}, BinkDoFrameAsync {}; {}, game thread's last ProcessEvent {}{}",
                       m.Serial, m.Name, why, static_cast<double>(Since(now, m.OpenedAt)) / 1000.0, FrameText(m.Handle), m.Paused, m.Want, SoundStateText(m), seconds,
                       Describe(m.Waits, seconds), Describe(m.Frames, seconds), Describe(m.AsyncFrames, seconds), HudText(now, HudSeen()),
                       game == 0 ? std::string{ "not seen yet" } : std::format("{} ms ago", Since(now, game)), PlayerText());
            m.Waits = {};
            m.Frames = {};
            m.AsyncFrames = {};
            m.CountedSince = now;
            m.Reports++;
        }

        void Opened(HBINK bink, char const* name, unsigned int flags, char const* how)
        {
            auto const text = NameText(name, flags);
            if (bink == nullptr)
            {
                LEASI_WARN("{} '{}' flags 0x{:08X} failed, thread {}", how, text, flags, ThreadText(GetCurrentThreadId()));
                return;
            }
            Header h{};
            auto const header = ReadHeader(bink, &h);
            std::scoped_lock lock{ Lock };
            if (auto const old = Find(bink); old != nullptr)
            {
                // Closed behind our back (BinkClose not hooked yet): the handle was reused.
                Movies.erase(Movies.begin() + (old - Movies.data()));
            }
            auto& m = Track(bink, text, flags);
            m.FromFile = (flags & BINKFROMMEMORY) == 0;
            Recount();
            LEASI_INFO("movie #{}: {} '{}' flags 0x{:08X} -> {} ({}x{}, {} frames{}), from {}, thread {}", m.Serial, how, m.Name, flags, bink, h.Width,
                       h.Height, h.Frames, header ? "" : ", header unreadable", m.FromFile ? "a file" : "memory", ThreadText(m.OpenThread));
            LEASI_FLUSH();
        }

        /// Under Lock. A press of Esc the poll saw and didn't act on: at most one line every 2 s, with how many went unlogged.
        void Refuse(ULONGLONG now, DWORD thread, std::string const& why)
        {
            if (Poller.RefusedLoggedAt != 0 && Since(now, Poller.RefusedLoggedAt) < RefusedLogMs)
            {
                Poller.RefusedUnlogged++;
                return;
            }
            LEASI_INFO("Esc pressed, read on thread {}: not taken, {}{}", ThreadText(thread), why,
                       Poller.RefusedUnlogged != 0 ? std::format(" ({} more such presses since the last line)", Poller.RefusedUnlogged) : std::string{});
            Poller.RefusedLoggedAt = now;
            Poller.RefusedUnlogged = 0;
        }

        /// Under Lock. A pause whose movie has made no Bink call for PausedSilentMs (LifeOf) is dropped: its thread neither reads
        /// Esc nor applies anything any more, and the pause keys must not stay the movies' (Pause.cpp asks OwnsKeys at each). The
        /// movie stays as BinkPause left it: if it calls again, the resume is applied then. A thread that only blocked that long
        /// is resumed that way too, if anything called this meanwhile: from outside, a blocked thread and a gone one look alike.
        void DropStale(ULONGLONG now)
        {
            if (!Wanted.load(std::memory_order_relaxed)) return;
            auto dropped = false;
            for (auto& m : Movies)
            {
                if (!m.Want) continue;
                auto const last = LifeOf(m);
                if (Since(now, last) < PausedSilentMs) continue;
                // Want is set: AskResume clears it, the same change as a resume asked.
                MovieRules::AskResume(m);
                m.RequestedAt = now;
                dropped = true;
                LEASI_WARN("movie #{} '{}' has made no {} for {:.1f} s under our pause ({}): pause dropped, Esc and Start go back to the game{}", m.Serial, m.Name,
                           m.Paused ? "Bink call" : "BinkWait", static_cast<double>(Since(now, last)) / 1000.0, m.Paused ? "applied" : "not applied yet",
                           m.Paused ? ", the resume is applied at its next call if any" : "");
            }
            if (!dropped) return;
            Recount();
            LEASI_FLUSH();
        }

        /// The foreground window is one of this process's: a key pressed in another program is not ours.
        bool OurForeground()
        {
            auto const window = GetForegroundWindow();
            DWORD process = 0;
            if (window != nullptr) GetWindowThreadProcessId(window, &process);
            return process != 0 && process == GetCurrentProcessId();
        }

        /// Under Lock, after DropStale. One press of a pause key: resumes every movie we paused (or asked to), else asks the
        /// pause of the newest fullscreen movie (not an older one still open). False when neither: no fullscreen movie, the
        /// caller says so. <paramref name="what"/> opens the log line. <paramref name="esc"/>: the press is Esc's, read by the
        /// poll, and taken once the toggle acts. <paramref name="pressedBy"/> (0: none): the press came by then, so a newest
        /// movie opened after it may be younger than the press, and is not paused (true: there was a movie, the line is
        /// logged here).
        bool ToggleLocked(ULONGLONG now, std::string const& what, bool esc, ULONGLONG pressedBy)
        {
            auto const hud = HudSeen();
            auto const movies = ListMovies(now, hud);
            // Esc's tick before Wanted changes, as Space's (KeyRules::ResumeOnPress): the game thread reads Wanted without Lock,
            // and the skip guard must see the press as ours by then, not only after the log line and its flush. Recount's
            // release store of Wanted publishes the tick.
            auto const take = [&] {
                if (!esc) return;
                Poller.Taken = true;
                EscDownTick.store(now, std::memory_order_relaxed);
            };
            if (Wanted.load(std::memory_order_relaxed))
            {
                KeyRules::ResumeOnPress(take, [&] { ResumeLocked(now); });
                LEASI_INFO("{}: movie resume asked, applied at each paused movie's next call; {}; open: {}", what, HudText(now, hud), movies);
                LEASI_FLUSH();
                return true;
            }

            Movie* target = nullptr;
            for (auto& m : Movies)
            {
                if (NotPausable(m, now, hud) == nullptr && (target == nullptr || m.Serial > target->Serial)) target = &m;
            }
            if (target == nullptr) return false;
            // A Start press the game thread never saw (the engine's movie input took it) may have skipped the movie before
            // this one: the movie it would pause opened after the press.
            if (pressedBy != 0 && target->OpenedAt > pressedBy)
            {
                LEASI_INFO("{}: taken, not toggled, movie #{} '{}' opened {} ms ago may be younger than the press; {}; open: {}", what, target->Serial,
                           target->Name, Since(now, target->OpenedAt), HudText(now, hud), movies);
                LEASI_FLUSH();
                return true;
            }
            take();
            // With the Wwise event to pause with it, as known now, while no pause of ours is owed or out on it (AskPause).
            MovieRules::AskPause(*target, [&] { return LatchOf(*target); });
            target->RequestedAt = now;
            Recount();
            LEASI_INFO("{}: movie pause asked: movie #{} '{}', applied at its next BinkWait; {}; open: {}", what, target->Serial, target->Name,
                       HudText(now, hud), movies);
            LEASI_FLUSH();
            return true;
        }

        /// Under Lock. One key as the BinkWait polls see it: true for a press to act on (the up->down edge, not after a gap in
        /// the polls unless our movie pause is on, not within the debounce). Keeps <paramref name="downTick"/> fresh while a
        /// press acted on (Taken) is down. The game thread judges Esc's same press from its own reads a few ms apart
        /// (Pause.cpp's Press asks OwnsKeys): a second press within the debounce, a tap shorter than the gap between two
        /// polls, or a press on either side of the HUD's 250 ms or BinkWait's 500 ms edge can be taken there for the movie
        /// and toggle nothing here. Left so: the player presses again, where acting on both sides could make one press two
        /// toggles.
        bool PollEdge(EscPoll& k, bool down, ULONGLONG now, bool wanted, std::atomic<ULONGLONG>& downTick)
        {
            auto const edge = KeyRules::PollEdge(k, down, now, wanted);
            if (edge.KeepFresh) downTick.store(now, std::memory_order_relaxed);
            return edge.Pressed;
        }

        /// Before each BinkWait, on the thread that plays the movie: reads Esc, and its press (the up->down edge; repeats
        /// don't make one) toggles the movie pause: pauses the fullscreen movie, or resumes what we paused. A pause asked here
        /// is applied right after, in this same BinkWait when this is the movie. The space bar's press only resumes. Reads only
        /// atomics of the game's.
        void Poll(HBINK bink)
        {
            if (bink == nullptr || !CanPause.load(std::memory_order_acquire)) return;
            auto const down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            auto const space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
            auto const thread = GetCurrentThreadId();
            std::scoped_lock lock{ Lock };
            auto const now = GetTickCount64();
            auto const hud = HudSeen();
            // The game thread latched or cleared a Kismet movie since the count: a movie's sound, and so whether its frames are
            // kept, may have changed.
            if (MovieSound::Changes() != SoundsSeen) Recount();

            // Each movie says once that it is a fullscreen one (under BinkAndEvent, with what we know of its sound then: "no
            // known sound" leaves it to the game); its close line says why it never was.
            if (auto const self = Find(bink); self != nullptr)
            {
                self->LastCallAt = now;
                auto const why = NotFullscreen(*self, now, hud);
                if (why != nullptr) self->NotFullscreenWhy = why;
                else if (!self->Fullscreen)
                {
                    self->Fullscreen = true;
                    auto const pauses = std::format("Esc read on thread {} pauses it", ThreadText(thread));
                    auto const what = Policy == MoviePolicy::Bink ? pauses
                                    : SoundOf(*self).Unknown != nullptr ? std::format("{}: Esc and Start stay the game's", SoundText(*self))
                                                                        : std::format("{}, {}", SoundText(*self), pauses);
                    LEASI_INFO("movie #{} '{}' is a fullscreen movie {:.1f} s after its open (from a file, calling BinkWait, {}): {}", self->Serial, self->Name,
                               static_cast<double>(Since(now, self->OpenedAt)) / 1000.0, HudText(now, hud), what);
                    LEASI_FLUSH();
                }
            }

            DropStale(now);
            auto const wanted = Wanted.load(std::memory_order_relaxed);
            // Both keys' edges before acting on either: an Esc press in the poll that Space resumes in is part of that resume.
            // Left for the next poll, it would pause again at once.
            auto const spacePressed = PollEdge(SpacePoller, space, now, wanted, SpaceDownTick);
            auto const escPressed = PollEdge(Poller, down, now, wanted, EscDownTick);
            // Typed into the Steam overlay or the open console, as the game thread saw them a tick ago: their keys, though
            // the foreground window is still the game's.
            if ((spacePressed || escPressed) && KeysElsewhere())
            {
                if (escPressed) Refuse(now, thread, "the Steam overlay or the console is open");
                return;
            }
            // Space with no movie pause of ours is the game's: it skips a movie the game lets skip.
            if (spacePressed && wanted && OurForeground())
            {
                auto const movies = ListMovies(now, hud);
                // The tick first (KeyRules::ResumeOnPress): the game thread reads Wanted without Lock, and once it is false only
                // this tick says the press is ours (MovieInput.cpp's hook, Pause.cpp's TakeResumeKey). Its release store
                // publishes the tick.
                KeyRules::ResumeOnPress(
                    [&] {
                        SpacePoller.Taken = true;
                        SpaceDownTick.store(now, std::memory_order_relaxed);
                        // Esc's press asked the same resume: it is taken with it, so the skips it is bound to are dropped too.
                        if (escPressed)
                        {
                            Poller.Taken = true;
                            EscDownTick.store(now, std::memory_order_relaxed);
                        }
                    },
                    [&] { ResumeLocked(now); });
                LEASI_INFO("{}, read on thread {}: movie resume asked, applied at each paused movie's next call; {}; open: {}",
                           escPressed ? "Space and Esc pressed" : "Space pressed", ThreadText(thread), HudText(now, hud), movies);
                LEASI_FLUSH();
                Overlay::UsePadPrompts(false);
                return;
            }
            if (!escPressed) return;

            if (!OurForeground())
            {
                Refuse(now, thread, "another program's window is in the foreground");
                return;
            }
            if (!ToggleLocked(now, std::format("Esc pressed, read on thread {}", ThreadText(thread)), true, 0))
            {
                Refuse(now, thread, std::format("no fullscreen movie ({}); open: {}", HudText(now, hud), ListMovies(now, hud)));
                return;
            }
            // Console.InputKey never sees this press: the pause screen names the keyboard's key from here.
            Overlay::UsePadPrompts(false);
        }

        void LogLine(bool warn, std::string const& text)
        {
            if (warn) LEASI_WARN("{}", text);
            else LEASI_INFO("{}", text);
        }

        /// Before each BinkWait, BinkDoFrame and BinkDoFrameAsync, on the thread that plays the movie: applies what Esc asked,
        /// a pause in BinkWait only, a resume in any of them on the thread of the movie's BinkWait (MovieRules::Decide).
        /// BinkPause and the movie's Wwise event are called outside the lock, in MovieRules::Send's order: the game thread may
        /// be waiting for it (OwnsKeys, Toggle, RequestResume). Wwise's answer is brought back under it, to the movie found
        /// again (Movies may have moved meanwhile), and decides (MovieRules::Settle): a pause it refuses pauses nothing, a
        /// resume it refuses stays owed and is sent again (ResendDue). The tick is read under the lock: RequestedAt may have
        /// been set by the game thread while this one waited for it. Returns whether the movie is paused now.
        bool Apply(HBINK bink, Call call)
        {
            if (bink == nullptr) return false;
            auto const thread = GetCurrentThreadId();
            ULONGLONG now = 0;
            MovieRules::Decision d;
            unsigned serial = 0;
            std::string name;
            ULONGLONG requestedAt = 0;
            {
                std::scoped_lock lock{ Lock };
                now = GetTickCount64();
                auto m = Find(bink);
                if (m == nullptr)
                {
                    // Opened before the hooks were live (the attach runs on its own thread). Only while BinkClose is hooked: a
                    // movie must never be tracked without its close being seen.
                    if (BinkClose_orig.load(std::memory_order_acquire) == nullptr) return false;
                    m = &Track(bink, "(opened before Pause Anytime)", 0);
                    LEASI_INFO("movie #{}: {} first seen in a player call, thread {}", m->Serial, bink, ThreadText(thread));
                }
                m->LastCallAt = now;
                // A call of the movie whose pause we want keeps that pause alive for PauseWanted, as it does for DropStale.
                if (m->Want) WantedLife.store(LifeOf(*m), std::memory_order_relaxed);
                d = MovieRules::Decide(*m, call == Call::Wait, static_cast<std::uint32_t>(thread), now,
                                       BinkPause_fn.load(std::memory_order_acquire) != nullptr);
                LogBadStep(*m, d.Bad);
                if (d.Do == MovieRules::Step::None) return m->Paused;
                // Paused is one of MayPause's inputs: a movie resumed here may no longer be one whose frames are kept.
                if (d.Do != MovieRules::Step::Resend) Recount();
                serial = m->Serial;
                name = m->Name;
                requestedAt = m->RequestedAt;
            }

            auto const pauseFn = BinkPause_fn.load(std::memory_order_acquire);
            auto const a = MovieRules::Send(
                d, [&](int pause) { return pauseFn(bink, pause); }, [](std::uint32_t event, bool pause) { return MovieSound::Set(event, pause); });

            auto const paused = MovieRules::PausedAfter(d, a);
            MovieRules::Settled settled;
            if (d.Sound != 0)
            {
                std::scoped_lock lock{ Lock };
                auto const back = GetTickCount64();
                auto const m = Find(bink, serial);
                settled = MovieRules::Settle(m, d, a, back, [&] { return LatchOf(*m); });
                if (m != nullptr) LogBadStep(*m, settled.Bad);
                if (settled.Recount) Recount();
            }
            // The movie closed while the calls were out, and a resume of ours is owed: its close left the event to us.
            auto const closedAk = settled.OwedGone ? MovieSound::Set(d.Sound, false) : AkSuccess;

            static char const* const Where[] = { "BinkWait", "BinkDoFrame", "BinkDoFrameAsync" };
            auto const where = std::format("in {} on thread {}", Where[static_cast<int>(call)], ThreadText(thread));
            auto const waited = Since(now, requestedAt);
            auto const sound = d.Sound;
            auto const ak = a.Ak;
            auto const picture = a.Picture;
            auto const owedGone = settled.OwedGone;
            auto const retook = settled.Retook ? std::format("; the pause asked meanwhile takes event {}", settled.RetookEvent) : std::string{};
            auto const gone = owedGone ? std::format("; the movie closed during the call: Wwise resume {} sent at once, AKRESULT {}", sound, closedAk) : std::string{};
            // BinkPause returns the pause state it leaves, 1 after BinkPause(1) and 0 after BinkPause(0): so our LE2 and LE3
            // logs show, and nothing of RAD's we have says so. Another answer is a warning, and changes nothing.
            if (d.Do == MovieRules::Step::Pause && ak != AkSuccess)
                LEASI_WARN("movie #{} '{}' NOT PAUSED {}, {} ms after the request: Wwise pause {} refused, AKRESULT {}: its sound plays on, so the picture does too "
                           "(no BinkPause); a press tries again",
                           serial, name, where, waited, sound, ak);
            else if (d.Do == MovieRules::Step::Pause)
            {
                auto const wwise = sound != 0 ? std::format("Wwise pause {}: AKRESULT {}, ", sound, ak) : std::string{};
                LogLine(picture != 1 || owedGone,
                        std::format("movie #{} '{}' PAUSED {}, {} ms after the request: {}BinkPause returned {}, {}{}", serial, name, where, waited, wwise, picture,
                                    FrameText(bink), gone));
            }
            else if (d.Do == MovieRules::Step::Resume)
            {
                auto const wwise = sound == 0        ? std::string{}
                                 : ak == AkSuccess  ? std::format("; Wwise resume {}: AKRESULT {}", sound, ak)
                                                    : std::format("; Wwise resume {}: AKRESULT {}, refused: the event stays paused, sent again {} times at most while the "
                                                                  "picture plays, and at the close",
                                                                  sound, ak, SoundResends);
                LogLine(picture != 0 || ak != AkSuccess || owedGone,
                        std::format("movie #{} '{}' RESUMED {}, {} ms after the request: BinkPause returned {}, {}{}{}{}", serial, name, where, waited, picture,
                                    FrameText(bink), wwise, retook, gone));
            }
            else
            {
                auto const after = ak == AkSuccess            ? std::string{ ": the event plays again" }
                                 : d.Resend < SoundResends    ? std::string{ ": refused, the event stays paused" }
                                                              : std::string{ ": refused, the event stays paused; no more tries while the picture plays, the close sends one" };
                LogLine(ak != AkSuccess || owedGone, std::format("movie #{} '{}' Wwise resume {} sent again ({} of {}) {}: AKRESULT {}{}{}{}", serial, name, sound,
                                                                 d.Resend, SoundResends, where, ak, after, retook, gone));
            }
            LEASI_FLUSH();
            return paused;
        }

        /// After each of those calls: counts them, and reports now and then.
        void Count(HBINK bink, Call call, int result)
        {
            if (bink == nullptr) return;
            auto const thread = GetCurrentThreadId();
            std::scoped_lock lock{ Lock };
            auto const now = GetTickCount64();
            auto const m = Find(bink);
            if (m == nullptr) return;
            switch (call)
            {
            case Call::Wait:
                m->Waits.Add(thread, result);
                m->LastWaitAt = now;
                break;
            case Call::DoFrame:
                m->Frames.Add(thread, result);
                break;
            case Call::DoFrameAsync:
                m->AsyncFrames.Add(thread, result);
                break;
            }
            ULONGLONG every = 0;
            if (m->Want != m->Paused || (m->ChangedAt != 0 && Since(now, m->ChangedAt) < 5000)) every = 1000;
            else if (m->Paused) every = 10000;
            else every = (std::min)(1000ull << (std::min)(m->Reports, 5u), 30000ull);
            if (Since(now, m->CountedSince) >= every) Report(*m, now, "report");
        }

        /// A movie closed while its Wwise event is paused by us (a skip by the pad's X, its end, a resume Wwise refused) sends
        /// the resume nothing else would, once: outside the lock, as Apply does. A movie closed while BinkPause(1) holds it (a
        /// skip under the pause, the game quitting) is resumed first: this Bink hangs a player that pauses and then stops
        /// calling BinkWait, and a close may be such a stop. BinkClose never overlaps another call on the same movie (the
        /// original would free the handle under it), so it cannot come between Apply's commit and the end of its calls: Pausing
        /// and Resuming, which last only that long, are not seen here. A game that overlaps them anyway gets a warning, and the
        /// event is left to that Apply, which finds the movie gone and sends a resume owed itself.
        void Closed(HBINK bink)
        {
            MovieRules::Closing closing;
            auto state = SoundState::None;
            unsigned soundId = 0;
            unsigned serial = 0;
            std::string name;
            {
                std::scoped_lock lock{ Lock };
                auto const now = GetTickCount64();
                auto const m = Find(bink);
                if (m == nullptr)
                {
                    LEASI_INFO("BinkClose {} (not tracked), thread {}", bink, ThreadText(GetCurrentThreadId()));
                    return;
                }
                Report(*m, now, "closing");
                auto const fullscreen = m->Fullscreen ? std::string{ "it was a fullscreen movie" }
                                                      : std::format("never a fullscreen movie ({})", m->NotFullscreenWhy != nullptr ? m->NotFullscreenWhy : "Esc never read in its BinkWait");
                LEASI_INFO("movie #{} '{}' closed after {:.1f} s on thread {}, {}{}", m->Serial, m->Name, static_cast<double>(Since(now, m->OpenedAt)) / 1000.0,
                           ThreadText(GetCurrentThreadId()), fullscreen, m->Want || m->Paused ? ": the movie pause ends with it" : "");
                closing = MovieRules::Close(*m);
                state = m->Sound;
                soundId = m->SoundId;
                serial = m->Serial;
                name = m->Name;
                Movies.erase(Movies.begin() + (m - Movies.data()));
                Recount();
                LEASI_FLUSH();
            }
            if (closing.Out)
            {
                LEASI_WARN("movie #{} '{}' closed with our Wwise {} of event {} still out, a Bink call on it overlapping its close: that call sends the resume if one is owed",
                           serial, name, state == SoundState::Pausing ? "pause" : "resume", soundId);
                LEASI_FLUSH();
            }
            if (auto const sound = closing.Resume; sound != 0)
            {
                auto const result = MovieSound::Set(sound, false);
                if (result == AkSuccess)
                    LEASI_INFO("movie #{} '{}' closed with its Wwise event paused by us: resume {} -> AKRESULT {}", serial, name, sound, result);
                else
                    LEASI_WARN("movie #{} '{}' closed with its Wwise event paused by us: resume {} -> AKRESULT {}, refused: nothing resumes it now", serial, name, sound,
                               result);
                LEASI_FLUSH();
            }
            // Last, right before the original close: the movie's Bink audio plays as little as can be before it stops.
            auto const pauseFn = BinkPause_fn.load(std::memory_order_acquire);
            if (!closing.Picture || pauseFn == nullptr) return;
            auto const result = pauseFn(bink, 0);
            // 0 is the answer our logs show (Apply's comment): another one is a warning.
            if (result == 0)
                LEASI_INFO("movie #{} '{}' closed paused: BinkPause(0) before the close returned {}", serial, name, result);
            else
                LEASI_WARN("movie #{} '{}' closed paused: BinkPause(0) before the close returned {}", serial, name, result);
            LEASI_FLUSH();
        }

        // ! Hooks
        // ========================================

        HBINK BinkOpen_hook(char const* name, unsigned int flags)
        {
            OpenDepth++;
            auto const bink = Original(BinkOpen_orig)(name, flags);
            OpenDepth--;
            if (OpenDepth == 0) Opened(bink, name, flags, "BinkOpen");
            return bink;
        }

        HBINK BinkOpenWithOptions_hook(char const* name, void const* options, unsigned int flags)
        {
            OpenDepth++;
            auto const bink = Original(BinkOpenWithOptions_orig)(name, options, flags);
            OpenDepth--;
            if (OpenDepth == 0) Opened(bink, name, flags, "BinkOpenWithOptions");
            return bink;
        }

        void BinkClose_hook(HBINK bink)
        {
            if (bink != nullptr) Closed(bink);
            Original(BinkClose_orig)(bink);
        }

        int BinkWait_hook(HBINK bink)
        {
            Poll(bink);
            auto const paused = Apply(bink, Call::Wait);
            auto const result = Original(BinkWait_orig)(bink);
            Count(bink, Call::Wait, result);
            // A paused movie presents nothing: the pause screen is presented from here, over its last frame.
            if (paused && result != 0) Overlay::MovieIdle();
            return result;
        }

        int BinkDoFrame_hook(HBINK bink)
        {
            Apply(bink, Call::DoFrame);
            auto const result = Original(BinkDoFrame_orig)(bink);
            Count(bink, Call::DoFrame, result);
            return result;
        }

        int BinkDoFrameAsync_hook(HBINK bink, unsigned int threadOne, unsigned int threadTwo)
        {
            Apply(bink, Call::DoFrameAsync);
            auto const result = Original(BinkDoFrameAsync_orig)(bink, threadOne, threadTwo);
            Count(bink, Call::DoFrameAsync, result);
            return result;
        }

        template <typename T>
        bool Hook(::LESDK::Initializer& Init, HMODULE module, char const* name, T* detour, std::atomic<T*>& orig)
        {
            auto const target = reinterpret_cast<void*>(GetProcAddress(module, name));
            if (target == nullptr)
            {
                LEASI_WARN("Bink: no export {}", name);
                return false;
            }
            orig.store(reinterpret_cast<T*>(Init.InstallHook(name, target, reinterpret_cast<void*>(detour))), std::memory_order_release);
            if (orig.load() == nullptr)
            {
                LEASI_WARN("Bink: hooking {} at {} failed", name, target);
                return false;
            }
            return true;
        }
    }

    void Install(::LESDK::Initializer& Init, MoviePolicy policy, std::span<char const* const> binkAudio,
                 std::span<char const* const> languages, MoviePlayerFields player)
    {
        {
            std::scoped_lock lock{ Lock };
            Policy = policy;
            PlayerFields = player;
            for (auto const name : binkAudio) BinkAudio.push_back(BaseName(name));
            for (auto const language : languages) Languages.push_back(BaseName(language));
        }

        char const* moduleName = "bink2w64_original.dll";
        auto module = GetModuleHandleW(L"bink2w64_original.dll");
        if (module == nullptr)
        {
            moduleName = "bink2w64.dll";
            module = GetModuleHandleW(L"bink2w64.dll");
        }
        // Before the module check: it reads only the exe's exports, and the Kismet latch's lines use its GetIDFromString
        // even when no movie can be paused.
        if (policy == MoviePolicy::BinkAndEvent) MovieSound::Install();
        if (module == nullptr)
        {
            LEASI_WARN("Bink: neither bink2w64_original.dll nor bink2w64.dll is loaded: movies can't be paused");
            return;
        }

        if (policy == MoviePolicy::BinkAndEvent)
        {
            std::string names;
            for (auto const& name : BinkAudio) names += (names.empty() ? "" : ", ") + name;
            LEASI_INFO("Bink: Esc and Start pause only a movie whose sound is known, a Wwise event latched for it or Bink audio ({}); any other stays the game's",
                       names.empty() ? std::string{ "no movie" } : names);
        }

        BinkPause_fn.store(reinterpret_cast<t_BinkPause*>(GetProcAddress(module, "BinkPause")), std::memory_order_release);
        // Close first: a movie must never be tracked without its close being seen.
        auto const closeHooked = Hook(Init, module, "BinkClose", BinkClose_hook, BinkClose_orig);
        auto const waitHooked = Hook(Init, module, "BinkWait", BinkWait_hook, BinkWait_orig);
        auto const frameHooked = Hook(Init, module, "BinkDoFrame", BinkDoFrame_hook, BinkDoFrame_orig);
        auto const asyncHooked = Hook(Init, module, "BinkDoFrameAsync", BinkDoFrameAsync_hook, BinkDoFrameAsync_orig);
        auto const openHooked = closeHooked && Hook(Init, module, "BinkOpen", BinkOpen_hook, BinkOpen_orig);
        auto const optionsHooked = closeHooked && Hook(Init, module, "BinkOpenWithOptions", BinkOpenWithOptions_hook, BinkOpenWithOptions_orig);

        auto const havePause = BinkPause_fn.load() != nullptr;
        CanPause.store(havePause && closeHooked && waitHooked, std::memory_order_release);
        LEASI_INFO("Bink: {} hooked (BinkOpen {}, BinkOpenWithOptions {}, BinkClose {}, BinkWait {}, BinkDoFrame {}, BinkDoFrameAsync {}), BinkPause {}: movie pause {}",
                   moduleName, openHooked, optionsHooked, closeHooked, waitHooked, frameHooked, asyncHooked, havePause,
                   CanPause.load() ? "on (Esc read in BinkWait)" : "OFF");
    }

    int OpenMovies() { return OpenCount.load(std::memory_order_relaxed); }

    int PausableMovies() { return CanPause.load(std::memory_order_acquire) ? PausableCount.load(std::memory_order_relaxed) : 0; }

    unsigned MovieSerial() { return Serial.load(std::memory_order_relaxed); }

    bool PauseWanted()
    {
        if (!Wanted.load(std::memory_order_acquire)) return false;
        // DropStale runs only when a key or a movie's poll calls it: a pause it would drop reads as over now, so the pause
        // screen doesn't stay up over a movie that stopped calling.
        return Since(GetTickCount64(), WantedLife.load(std::memory_order_relaxed)) < PausedSilentMs;
    }

    bool OwnsKeys()
    {
        if (!CanPause.load(std::memory_order_acquire)) return false;
        std::scoped_lock lock{ Lock };
        auto const now = GetTickCount64();
        DropStale(now);
        auto const hud = HudSeen();
        return std::any_of(Movies.begin(), Movies.end(), [&](Movie const& m) { return m.Want || NotPausable(m, now, hud) == nullptr; });
    }

    bool Toggle(char const* what, ULONGLONG pressedBy)
    {
        if (!CanPause.load(std::memory_order_acquire)) return false;
        std::scoped_lock lock{ Lock };
        auto const now = GetTickCount64();
        DropStale(now);
        if (ToggleLocked(now, std::format("{} on thread {}", what, ThreadText(GetCurrentThreadId())), false, pressedBy)) return true;
        auto const hud = HudSeen();
        LEASI_INFO("{} on thread {}: nothing to toggle, no fullscreen movie ({}); open: {}", what, ThreadText(GetCurrentThreadId()), HudText(now, hud),
                   ListMovies(now, hud));
        return false;
    }

    ULONGLONG EscSeenDown() { return EscDownTick.load(std::memory_order_relaxed); }

    ULONGLONG SpaceSeenDown() { return SpaceDownTick.load(std::memory_order_relaxed); }

    void RequestResume(char const* why)
    {
        std::scoped_lock lock{ Lock };
        auto const now = GetTickCount64();
        auto const movies = ListMovies(now, HudSeen());
        ResumeLocked(now);
        LEASI_INFO("movie resume asked ({}) on thread {}, applied at each paused movie's next call; open: {}", why, ThreadText(GetCurrentThreadId()), movies);
    }
}
