#include "PauseAnytime/Pause.hpp"
#include "PauseAnytime/Engine.hpp"
#include "PauseAnytime/Photo.hpp"
#include "PauseAnytime/game/Game.hpp"
#include "Common/Objects.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Esc pauses a conversation or an in-engine cutscene with the world pause the "pause" console command sets
// (PlayerController.Pause -> ServerPause -> GameInfo.SetPause: WorldInfo.Pauser), which stops the picture, the voice-over,
// the music and the lips together and resumes them in sync. Not WorldInfo.bPlayersOnly (the pause menu's and photo mode's):
// the conversation's clock and the music keep running under it. A Bink movie does not stop under either: Bink.cpp pauses it,
// and reads Esc itself on the movie's thread (a fullscreen movie's Esc press never reaches Console.InputKey); here Esc is only
// kept from the game while a fullscreen movie plays. A gamepad's Start (XboxTypeS_Start: an Xbox pad, or a PlayStation one
// through Steam Input) does what Esc does; during a fullscreen movie its press is taken by the engine too, and what reaches
// Console.InputKey of it (its repeats, its release) toggles the movie pause from here. Under our world pause, a gamepad's Y
// or F7 opens the game's photo mode over it, and closes it again (so do Esc and Start). Anywhere else these keys keep their
// vanilla behavior. Everything here runs on the game thread, inside the ProcessEvent calls it reacts to. Where a movie's
// sound is a Wwise event (MoviePolicy::BinkAndEvent), the Kismet movie action that starts it is latched here for Bink.cpp.
namespace PauseAnytime
{
    namespace
    {
        // ! Game facts
        // ========================================

        // The modes, the GuiKey events and the rest that differ per game: game/LE<n>.hpp.
        using namespace Game;

        // Engine.Interaction.EInputEvent. IE_Repeat (2) and the rest only matter as "not a press".
        constexpr unsigned char IE_Pressed = 0;
        constexpr unsigned char IE_Released = 1;

        /// A held gamepad button repeats after 0.2 s, then every 0.1 s: a press of Start with no event for this long is over,
        /// its release lost (focus change)...
        constexpr ULONGLONG StartStaleMs = 500;
        /// ...unless the game thread stalled since its last event (no ProcessEvent for this long): the pad is read once a
        /// frame, so a stall holds the repeats back too.
        constexpr ULONGLONG GameStallMs = 250;

        /// A movie pause is lifted once the HUD has drawn this long without a gap: no fullscreen movie covers the screen.
        constexpr ULONGLONG HudBackMs = 1000;

        /// Frames the pause screen stays hidden after photo mode ends: the render thread presents a frame or two behind this
        /// one, and photo mode's last frames must not show it.
        constexpr unsigned PhotoHideGrace = 3;

        // ! State
        // ========================================

        /// A pause key's press as Console.InputKey sees it.
        struct KeyState
        {
            /// A press we acted on is down: its repeats and its release are ours too.
            bool Held = false;
            /// Start only: a press seen here and left to the game is down. Its repeats and release stay the game's, also
            /// once a fullscreen movie has started under it.
            bool Vanilla = false;
            ULONGLONG PressedAt = 0;
            ULONGLONG ReleasedAt = 0;
            /// Start only: its latest event, of any kind.
            ULONGLONG LastEventAt = 0;
        };

        struct State
        {
            /// Our world pause: the WorldInfo we paused (with its index, checked again before each use: a map change or a load
            /// replaces it) and the controller that paused it, which resumes it.
            bool WorldPaused = false;
            AWorldInfo* World = nullptr;
            int WorldIndex = -1;
            ABioPlayerController* Controller = nullptr;
            int ControllerIndex = -1;
            unsigned char PausedMode = NoMode;
            ULONGLONG PausedAt = 0;
            /// Start paused it: the banner names Start, also before PlayerInput has seen the pad (our press never reaches it).
            bool PausedByStart = false;
            /// The pause's sound holds the game's audio (Game::PauseSoundHoldsAudio) and its end is not posted yet.
            bool AudioHeld = false;

            /// The first game-thread ProcessEvent after a gap of more than GameStallMs.
            ULONGLONG StallEndedAt = 0;

            KeyState Esc;
            KeyState Start;
            /// Photo mode's keys: a gamepad's Y, and F7.
            KeyState Y;
            KeyState F7;
            /// Resume only: the space bar, and a gamepad's A.
            KeyState Space;
            KeyState A;

            /// Turned off for the session by an access violation while drawing, or a missing Canvas function.
            bool BannerOff = false;
            /// Frames the pause screen stays hidden after photo mode (PhotoHideGrace).
            unsigned PhotoHideFrames = 0;
            /// Access violations in the per-frame checks: each drops our pause state, and the third turns them off for the
            /// session (Esc still checks the state), rather than fault and log at frame rate.
            unsigned FrameFaults = 0;

            /// The first BioHUD.PostRender after a gap longer than HudSilentMs: since then the HUD has drawn every frame.
            ULONGLONG HudDrawsSince = 0;

            /// Diagnostics, once each: the local player, and per movie whether input and the HUD still run during it.
            bool PlayerChecked = false;
            unsigned InputMovieSerial = 0;
            unsigned HudMovieSerial = 0;

            /// Frames on which ReadGameTexts tried (MaxGameTextTries once it is done).
            unsigned GameTextTries = 0;
            /// Frames on which ReadGameIcons tried (MaxGameIconTries once it is done).
            unsigned GameIconTries = 0;
            /// Frames on which ReadGameFonts tried (MaxGameFontTries once it is done).
            unsigned GameFontTries = 0;
        };
        State G;

        std::atomic<DWORD> GameThreadId{ 0 };
        std::atomic<ULONGLONG> GameThreadTick{ 0 };
        /// The end of the latest BioHUD.PostRender: Bink.cpp's movie threads read it.
        std::atomic<ULONGLONG> HudTick{ 0 };

        // ! The player
        // ========================================

        ABioPlayerController* AsBio(APlayerController* pc)
        {
            return pc != nullptr && pc->IsA(ABioPlayerController::StaticClass()) ? static_cast<ABioPlayerController*>(pc) : nullptr;
        }

        /// GEngine's first LocalPlayer. Console.ConsoleTargetPlayer is the fallback: the console sets it only once it was used.
        ABioPlayerController* LocalController(UConsole* console)
        {
            APlayerController* pc = nullptr;
            auto const engine = GEngine != nullptr ? *GEngine : nullptr;
            if (engine != nullptr && engine->GamePlayers.Count() > 0 && engine->GamePlayers.GetData()[0] != nullptr)
                pc = engine->GamePlayers.GetData()[0]->Actor;
            if (pc == nullptr && console != nullptr && console->ConsoleTargetPlayer != nullptr) pc = console->ConsoleTargetPlayer->Actor;
            return AsBio(pc);
        }

        /// The movie mode: GameModes[8] is SFXGameModeMovie, and the array is searched in case it isn't. For the logs only:
        /// whether a movie is paused doesn't depend on it.
        USFXGameModeMovie* MovieMode(ABioPlayerController* pc)
        {
            auto const manager = ModeManagerOf(pc);
            if (manager == nullptr) return nullptr;
            auto const& modes = manager->GameModes;
            for (UINT i = 0; i < modes.Count(); i++)
            {
                auto const mode = modes.GetData()[(i + ModeMovie) % modes.Count()];
                if (mode != nullptr && mode->IsA(USFXGameModeMovie::StaticClass())) return static_cast<USFXGameModeMovie*>(mode);
            }
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                LEASI_WARN("no SFXGameModeMovie in GameModes ({} modes): the logs can't say the movie mode", modes.Count());
            }
            return nullptr;
        }

        /// The movie mode is on, whichever mode CurrentMode names: Movie shares ModePriority_Menus with GUI and Galaxy, and
        /// the travel movie starts from the galaxy map, so CurrentMode may stay 9 (Galaxy) for the whole movie.
        bool MovieOn(USFXGameModeMovie const* movie) { return movie != nullptr && movie->bIsActive != 0; }

        /// A loading screen's movie (ActivateSpecifier 'LoadingMovie'): its SkipMovie does nothing.
        bool LoadingMovie(USFXGameModeMovie const* movie) { return movie != nullptr && movie->bLoadingMovie != 0; }

        char const* MovieModeText(USFXGameModeMovie const* movie)
        {
            return movie == nullptr ? "missing" : !MovieOn(movie) ? "off" : LoadingMovie(movie) ? "on (loading movie)" : "on";
        }

        /// Every game mode that is on, by index (a movie runs on top of a conversation or a cutscene): for the logs.
        std::string ActiveModesText(ABioPlayerController* pc)
        {
            auto const manager = ModeManagerOf(pc);
            if (manager == nullptr) return "unknown";
            std::string text;
            auto const& modes = manager->GameModes;
            for (UINT i = 0; i < modes.Count(); i++)
            {
                auto const mode = modes.GetData()[i];
                if (mode == nullptr || mode->bIsActive == 0) continue;
                text += std::format("{}{}", text.empty() ? "" : " ", i);
            }
            return text.empty() ? "none" : text;
        }

        // ! World pause
        // ========================================

        /// PlayerController.Pause, the "pause" console command: ServerPause toggles SetPause(!IsPaused()).
        bool CallPause(ABioPlayerController* pc)
        {
            static CachedFunction cache;
            auto const pause = FunctionOf(pc, L"Pause", cache);
            if (pause == nullptr) return false;
            APlayerController_execPause_Parms parms;
            CallGame(pc, pause, &parms);
            return true;
        }

        void EndWorldPause()
        {
            G.WorldPaused = false;
            G.World = nullptr;
            G.WorldIndex = -1;
            G.Controller = nullptr;
            G.ControllerIndex = -1;
            G.PausedMode = NoMode;
            G.AudioHeld = false;
        }

        /// The game's GUI sound for our world pause starting or ending (game/LE<n>.hpp; none for null): the pause's heard at
        /// once under it, the resume's once the world runs again. True when it played.
        bool PlayPauseSound(ABioPlayerController* pc, wchar_t const* row)
        {
            if (row == nullptr) return false;
            auto const played = PhotoMode::PlayGuiSound(pc, row);
            LEASI_INFO("GUI sound {}: {}", ToUtf8(row), played ? "played" : "NOT played (no GUI manager, the GUI sound hooks are off, or the game refused it)");
            return played;
        }

        /// Posts the end of what the pause's sound holds, once, for what Validate meets: ResumeWorld does it for a resume of
        /// ours, in the frame the world resumes. Here it is the Steam overlay while it is open and the world is still held, as
        /// a voice line still held when the world runs again is lost and a posting after that saves it no more
        /// (docs/how-it-works.md), or a pause already over, which gives back the rest of the audio.
        void ReleaseAudio(ABioPlayerController* pc, char const* why)
        {
            if (!G.AudioHeld) return;
            G.AudioHeld = false;
            LEASI_INFO("game audio released: {}", why);
            if (pc == nullptr && Alive(G.Controller, G.ControllerIndex)) pc = G.Controller;
            PlayPauseSound(pc, ResumeSound);
        }

        bool PauseWorld(ABioPlayerController* pc, unsigned char mode)
        {
            auto const world = pc->WorldInfo;
            if (world == nullptr || world->Pauser != nullptr) return false;
            if (!CallPause(pc)) return false;
            if (world->Pauser == nullptr)
            {
                LEASI_WARN("world pause refused: Pause ran and WorldInfo.Pauser is still None (GameInfo.AllowPausing?), thread {}", GetCurrentThreadId());
                return false;
            }
            G.WorldPaused = true;
            G.World = world;
            G.WorldIndex = world->ObjectInternalInteger;
            G.Controller = pc;
            G.ControllerIndex = pc->ObjectInternalInteger;
            G.PausedMode = mode;
            G.PausedAt = GetTickCount64();
            LEASI_INFO("world paused in mode {} ({}): Pauser {} ({}), {}, thread {}", mode, ModeName(mode), PathOf(world->Pauser),
                       static_cast<void const*>(world->Pauser), PathOf(world), GetCurrentThreadId());
            G.AudioHeld = PlayPauseSound(pc, PauseSound) && PauseSoundHoldsAudio;
            return true;
        }

        /// Toggles the pause back through the controller that paused, only while the world is still paused: Pause on an
        /// unpaused world would pause it again. Our state ends before the call, so a fault inside it can't leave it armed. A
        /// key's resume (<paramref name="byKey"/>) plays the game's sound for it, and so does any resume while its pause's
        /// sound holds the game's audio.
        void ResumeWorld(char const* why, bool byKey = false)
        {
            if (!G.WorldPaused) return;
            auto const mode = G.PausedMode;
            auto const since = G.PausedAt;
            auto const world = G.World;
            auto const controller = G.Controller;
            auto const audioHeld = G.AudioHeld;
            auto const worldAlive = Alive(G.World, G.WorldIndex);
            auto const controllerAlive = Alive(G.Controller, G.ControllerIndex);
            auto const giveAudioBack = [&] { if (audioHeld) PlayPauseSound(controllerAlive ? controller : LocalController(nullptr), ResumeSound); };
            EndWorldPause();
            if (!worldAlive || world->Pauser == nullptr)
            {
                LEASI_INFO("world resume ({}): nothing to do, the world is gone or no longer paused", why);
                giveAudioBack();
                return;
            }
            if (!controllerAlive || controller->WorldInfo != world)
            {
                LEASI_WARN("world resume ({}): the controller that paused is gone, the world stays paused by {}", why, PathOf(world->Pauser));
                giveAudioBack();
                return;
            }
            CallPause(controller);
            auto const pauser = world->Pauser;
            if (pauser != nullptr)
                LEASI_WARN("world resume ({}) after {:.1f} s: still paused by {} ({}): another pause holds the world (Steam overlay?)", why,
                           HeldFor(since), PathOf(pauser), static_cast<void const*>(pauser));
            else
                LEASI_INFO("world resumed ({}) after {:.1f} s, paused in mode {} ({}), thread {}", why, HeldFor(since), mode, ModeName(mode),
                           GetCurrentThreadId());
            if (PauseSoundHoldsAudio ? audioHeld : byKey && pauser == nullptr) PlayPauseSound(controller, ResumeSound);
        }

        // ! World pause: checks
        // ========================================

        /// Every frame and every key of ours: our world pause ends when someone else ended it (Pauser None: the game, a load,
        /// the Steam overlay closing clears every CanUnpause that agrees), when its WorldInfo is gone, or when the mode left
        /// Conversation and Cinematic under it (Photo is ours while our photo session runs). Our photo session ends first,
        /// with the world or the mode it stands on. The movie pause isn't checked here: Bink.cpp ends it (close, Esc, its
        /// movie gone silent), and SkipMovie and the HUD drawing again do. An open Steam overlay will end the pause when it
        /// closes, with no key of ours: the audio goes back first, while the world is held.
        void Validate(ABioPlayerController* pc, char const* where)
        {
            PhotoMode::Check(G.WorldPaused, G.World);
            if (G.WorldPaused)
            {
                if (!Alive(G.World, G.WorldIndex))
                {
                    LEASI_INFO("world pause ended ({}): the WorldInfo we paused is gone (map change or load), thread {}", where, GetCurrentThreadId());
                    ReleaseAudio(pc, "our world pause ended with its world");
                    EndWorldPause();
                    LEASI_FLUSH();
                }
                else if (G.World->Pauser == nullptr)
                {
                    LEASI_INFO("world pause ended ({}) after {:.1f} s: WorldInfo.Pauser became None without us, thread {}", where, HeldFor(G.PausedAt),
                               GetCurrentThreadId());
                    ReleaseAudio(pc, "our world pause ended without us");
                    EndWorldPause();
                    LEASI_FLUSH();
                }
                else
                {
                    if (G.AudioHeld && pc != nullptr && pc->bIsExternalUIOpen)
                    {
                        ReleaseAudio(pc, "the Steam overlay is open, the world still held");
                        LEASI_FLUSH();
                    }
                    if (!PhotoMode::On() && pc != nullptr && pc->WorldInfo == G.World)
                    {
                        auto const mode = ModeOf(pc);
                        if (mode != ModeConversation && mode != ModeCinematic)
                        {
                            LEASI_WARN("mode is {} ({}) under our world pause ({}): resuming", mode, ModeName(mode), where);
                            ResumeWorld("mode changed");
                            LEASI_FLUSH();
                        }
                    }
                }
            }
        }

        // ! Esc and Start
        // ========================================

        /// Console.Typing and Console.Open have their own InputKey, where Esc closes the console: vanilla.
        bool ConsoleOpen(UFunction* function)
        {
            static SFXName const Typing{ L"Typing", 0 };
            static SFXName const Open{ L"Open", 0 };
            auto const outer = function->Outer;
            return outer != nullptr && !outer->IsA(UClass::StaticClass()) && (outer->Name == Typing || outer->Name == Open);
        }

        /// Once per movie: does the game thread still run input while a movie plays, in which game mode, and do the pause keys
        /// belong to the movie then?
        void NoteInputDuringMovie(UConsole* console, UConsole_execInputKey_Parms const* p)
        {
            auto const serial = Bink::MovieSerial();
            if (Bink::OpenMovies() == 0 || serial == G.InputMovieSerial) return;
            G.InputMovieSerial = serial;
            auto const key = p->Key.Instanced();
            auto const pc = LocalController(console);
            auto const mode = ModeOf(pc);
            LEASI_INFO("Console.InputKey runs while movie #{} is open: key {} event {}, mode {} ({}), movie mode {}, modes on {}, pause keys {}, thread {}",
                       serial, ToUtf8(std::wstring_view(key.Chars(), key.Length())), p->Event, mode, ModeName(mode), MovieModeText(MovieMode(pc)),
                       ActiveModesText(pc), Bink::OwnsKeys() ? "the movie's" : "vanilla", GetCurrentThreadId());
        }

        enum class Key { Esc, Start, Y, F7, Space, A };

        char const* KeyName(Key key)
        {
            switch (key)
            {
            case Key::Esc: return "Esc";
            case Key::Start: return "Start";
            case Key::Y: return "Y";
            case Key::Space: return "Space";
            case Key::A: return "A";
            default: return "F7";
            }
        }

        /// A fresh press of Esc or Start, outside the open console: the fullscreen movie's, else it leaves our photo mode, else
        /// it resumes our world pause, else pauses the world in a conversation or a cutscene. Returns what it did, or null for
        /// a press left to the game.
        char const* Press(UConsole* console, UFunction* function, Key key)
        {
            auto const pc = LocalController(console);
            Validate(pc, KeyName(key));
            auto const mode = ModeOf(pc);
            auto const world = pc != nullptr ? pc->WorldInfo : nullptr;
            auto const pauser = world != nullptr ? world->Pauser : nullptr;
            auto const movies = Bink::OpenMovies();
            auto const movie = MovieMode(pc);

            char const* action = nullptr;
            if (Bink::OwnsKeys())
            {
                // Esc isn't toggled here: the movie's thread saw this press too and acts on it, so one press is one toggle.
                // That thread can't read Start.
                if (key == Key::Esc) action = "taken for the fullscreen movie, its thread toggles the movie pause";
                else action = Bink::Toggle("Start pressed") ? "toggled the movie pause" : "taken for the fullscreen movie, nothing to toggle";
            }
            else if (PhotoMode::On())
            {
                // Photo mode's own Esc and Start (ShowMenuGui) would open the pause menu over the conversation.
                PhotoMode::Drop(KeyName(key), false);
                action = "left photo mode, the world stays paused";
            }
            else if (G.WorldPaused)
            {
                ResumeWorld(KeyName(key), true);
                action = "resumed the world";
            }
            else if ((mode == ModeConversation || mode == ModeCinematic) && world != nullptr && pauser == nullptr)
            {
                if (PauseWorld(pc, mode))
                {
                    G.PausedByStart = key == Key::Start;
                    action = "paused the world";
                }
            }

            LEASI_INFO("{} pressed: mode {} ({}), movie mode {}, modes on {}, Pauser {} ({}), {} open movie(s), {}: {} (function {}, thread {})",
                       KeyName(key), mode, ModeName(mode), MovieModeText(movie), ActiveModesText(pc), PathOf(pauser), static_cast<void const*>(pauser), movies, PathOf(pc),
                       action != nullptr ? action : "vanilla", PathOf(function), GetCurrentThreadId());
            LEASI_FLUSH();
            return action;
        }

        /// Esc. Returns true when the event is ours.
        bool TakeEsc(UConsole* console, UFunction* function, UConsole_execInputKey_Parms* p, ULONGLONG now)
        {
            auto& k = G.Esc;
            if (p->Event != IE_Pressed)
            {
                // The repeats and the release of a press we took. During a fullscreen movie (or our pause of one), every Esc
                // event, its press seen here or not: the movie's thread reads the key itself and toggles the pause there, and
                // the travel movie's press never comes here (its release did, alone). Not in an open console: it closes on
                // Esc's release, and the engine's movie input takes the press.
                if (!k.Held)
                {
                    if (ConsoleOpen(function) || !Bink::OwnsKeys()) return false;
                    if (p->Event == IE_Released) LEASI_INFO("Esc released during a fullscreen movie, its press unseen here: taken, the movie's thread acts on Esc");
                }
                if (p->Event == IE_Released)
                {
                    k.Held = false;
                    k.ReleasedAt = now;
                }
                p->ReturnValue = 1;
                return true;
            }

            // A fresh press: a release we never saw (focus lost) ends the last one here, and so does the grace after one we
            // saw. Only a press we take sets the guard again: a vanilla press must keep its GuiKey and skips.
            k.Held = false;
            k.ReleasedAt = 0;
            if (ConsoleOpen(function))
            {
                LEASI_INFO("Esc pressed in {}: the console is open, vanilla", PathOf(function));
                return false;
            }
            if (Press(console, function, Key::Esc) == nullptr) return false;
            k.Held = true;
            k.PressedAt = now;
            p->ReturnValue = 1;
            return true;
        }

        /// Start: its press does what Esc's does. During a fullscreen movie the engine's movie input takes the press (as it
        /// takes Esc's), and its first repeat or its release, whichever comes first, toggles the movie pause here instead.
        /// Returns true when the event is ours.
        bool TakeStart(UConsole* console, UFunction* function, UConsole_execInputKey_Parms* p, ULONGLONG now)
        {
            auto& k = G.Start;
            if ((k.Held || k.Vanilla) && now - k.LastEventAt > StartStaleMs && G.StallEndedAt <= k.LastEventAt)
            {
                k.Held = false;
                k.Vanilla = false;
            }
            k.LastEventAt = now;

            if (p->Event == IE_Pressed)
            {
                k.Held = false;
                k.Vanilla = false;
                k.ReleasedAt = 0;
                if (ConsoleOpen(function) || Press(console, function, Key::Start) == nullptr)
                {
                    k.Vanilla = true;
                    return false;
                }
                k.Held = true;
                k.PressedAt = now;
                p->ReturnValue = 1;
                return true;
            }

            auto const released = p->Event == IE_Released;
            if (k.Vanilla)
            {
                if (released) k.Vanilla = false;
                return false;
            }
            if (!k.Held)
            {
                // A press never seen here: the engine's movie input took it. Not a press held into the movie (Vanilla).
                if (ConsoleOpen(function) || !Bink::OwnsKeys()) return false;
                Bink::Toggle(released ? "Start released during a fullscreen movie, its press unseen here"
                                      : "Start held during a fullscreen movie, its press unseen here");
                if (!released)
                {
                    k.Held = true;
                    k.PressedAt = now;
                }
            }
            if (released)
            {
                k.Held = false;
                k.ReleasedAt = now;
            }
            p->ReturnValue = 1;
            return true;
        }

        void PublishOverlay(ABioPlayerController* pc);

        /// A fresh press of Y or F7, outside the open console: in our photo mode it leaves it, under our pause of a
        /// conversation or a cutscene it enters it. Anywhere else, and during a fullscreen movie, the key is the game's.
        char const* PressPhoto(UConsole* console, UFunction* function, Key key)
        {
            auto const pc = LocalController(console);
            Validate(pc, KeyName(key));
            auto const mode = ModeOf(pc);
            char const* action = nullptr;
            if (Bink::OwnsKeys())
                action = nullptr;
            else if (PhotoMode::On())
            {
                PhotoMode::Drop(KeyName(key), false);
                action = "left photo mode, the world stays paused";
            }
            else if (G.WorldPaused && pc != nullptr && pc == G.Controller && (mode == ModeConversation || mode == ModeCinematic))
            {
                // The pause screen goes before photo mode shows anything: its first screenshot must not catch it.
                G.PhotoHideFrames = PhotoHideGrace;
                Overlay::Publish(false, false, true);
                action = PhotoMode::Enter(pc, mode, key == Key::Y);
                // What Enter got to before it stopped. The pause screen comes back this frame, not three frames later.
                if (action == nullptr)
                {
                    PhotoMode::Drop("refused", false);
                    G.PhotoHideFrames = 0;
                    PublishOverlay(pc);
                }
            }
            LEASI_INFO("{} pressed: mode {} ({}), world paused by us {}, {}: {} (function {}, thread {})", KeyName(key), mode, ModeName(mode), G.WorldPaused,
                       PathOf(pc), action != nullptr ? action : "vanilla", PathOf(function), GetCurrentThreadId());
            LEASI_FLUSH();
            return action;
        }

        /// Y and F7: a press we acted on takes its repeats and its release too. Returns true when the event is ours.
        bool TakePhotoKey(UConsole* console, UFunction* function, UConsole_execInputKey_Parms* p, KeyState& k, Key key, ULONGLONG now)
        {
            if (p->Event != IE_Pressed)
            {
                if (!k.Held) return false;
                if (p->Event == IE_Released)
                {
                    k.Held = false;
                    k.ReleasedAt = now;
                }
                p->ReturnValue = 1;
                return true;
            }
            k.Held = false;
            k.ReleasedAt = 0;
            if (ConsoleOpen(function) || PressPhoto(console, function, key) == nullptr) return false;
            k.Held = true;
            k.PressedAt = now;
            p->ReturnValue = 1;
            return true;
        }

        /// Space and A only resume: our world pause (not in photo mode, where Space takes the picture), or our movie pause. A
        /// press we acted on takes its repeats and its release too, and so does a key the movie's thread resumed on (Space,
        /// read there like Esc). During a movie the engine's movie input takes the press: its first repeat or its release
        /// resumes then, as Start's does. Anywhere else the key is the game's (Space skips a movie or a cutscene the game
        /// lets skip). Returns true when the event is ours.
        bool TakeResumeKey(UConsole* console, UFunction* function, UConsole_execInputKey_Parms* p, KeyState& k, Key key, ULONGLONG now)
        {
            auto const pressed = p->Event == IE_Pressed;
            auto const released = p->Event == IE_Released;
            if (pressed)
            {
                k.Held = false;
                k.ReleasedAt = 0;
            }
            else if (k.Held)
            {
                if (released)
                {
                    k.Held = false;
                    k.ReleasedAt = now;
                }
                p->ReturnValue = 1;
                return true;
            }
            if (ConsoleOpen(function)) return false;

            char const* action = nullptr;
            if (Bink::PauseWanted() && Bink::OwnsKeys())
            {
                auto const space = key == Key::Space;
                Bink::RequestResume(pressed ? (space ? "Space pressed" : "A pressed")
                                            : (space ? "Space during a fullscreen movie, its press unseen here" : "A during a fullscreen movie, its press unseen here"));
                action = "resumed the movie";
            }
            // After the test above: the movie's thread stores this tick before it ends the pause.
            else if (key == Key::Space && TakenLately(Bink::SpaceSeenDown(), now))
                action = "taken: the movie's thread resumed the movie on it";
            else if (pressed && G.WorldPaused && !PhotoMode::On())
            {
                auto const pc = LocalController(console);
                Validate(pc, KeyName(key));
                if (G.WorldPaused)
                {
                    ResumeWorld(KeyName(key), true);
                    action = "resumed the world";
                }
            }
            if (action == nullptr) return false;
            LEASI_INFO("{} {}: {} (function {}, thread {})", KeyName(key), pressed ? "pressed" : released ? "released" : "repeated", action, PathOf(function),
                       GetCurrentThreadId());
            LEASI_FLUSH();
            if (!released)
            {
                k.Held = true;
                k.PressedAt = now;
            }
            else
                k.ReleasedAt = now;
            p->ReturnValue = 1;
            return true;
        }

        /// Returns true when the key is ours: the caller skips the original, which tells the viewport the key was handled.
        bool TakeKey(UConsole* console, UFunction* function, UConsole_execInputKey_Parms* p)
        {
            static SFXName const Escape{ L"Escape", 0 };
            static SFXName const Start{ L"XboxTypeS_Start", 0 };
            static SFXName const PadY{ L"XboxTypeS_Y", 0 };
            static SFXName const F7{ L"F7", 0 };
            static SFXName const SpaceBar{ L"SpaceBar", 0 };
            static SFXName const PadA{ L"XboxTypeS_A", 0 };
            GameThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
            NoteInputDuringMovie(console, p);
            // Any event of those keys, not only presses: during a movie the engine's movie input takes Start's press.
            if (p->Key == Escape || p->Key == F7 || p->Key == SpaceBar) Overlay::UsePadPrompts(false);
            else if (p->Key == Start || p->Key == PadY || p->Key == PadA) Overlay::UsePadPrompts(true);
            if (p->Key == Escape) return TakeEsc(console, function, p, GetTickCount64());
            if (p->Key == Start) return TakeStart(console, function, p, GetTickCount64());
            if (p->Key == PadY) return TakePhotoKey(console, function, p, G.Y, Key::Y, GetTickCount64());
            if (p->Key == F7) return TakePhotoKey(console, function, p, G.F7, Key::F7, GetTickCount64());
            if (p->Key == SpaceBar) return TakeResumeKey(console, function, p, G.Space, Key::Space, GetTickCount64());
            if (p->Key == PadA) return TakeResumeKey(console, function, p, G.A, Key::A, GetTickCount64());
            return false;
        }

        /// A press of that key we acted on is down (2 s at most), or was released a moment ago.
        bool Guarding(KeyState const& k, ULONGLONG now)
        {
            if (k.Held) return now - k.PressedAt < GuardMs;
            return k.ReleasedAt != 0 && now - k.ReleasedAt < ReleaseGraceMs;
        }

        /// Which of our keys is down (or was a moment ago), or null: whether or not Console.InputKey's TRUE kept it from
        /// PlayerInput. A press of Esc the movie's thread acted on counts too, seen here or not: Bink.cpp keeps its tick fresh
        /// while it is down.
        char const* KeyGuard(ULONGLONG now)
        {
            if (TakenLately(Bink::EscSeenDown(), now)) return "Esc";
            if (TakenLately(Bink::SpaceSeenDown(), now)) return "Space";
            if (Guarding(G.Esc, now)) return "Esc";
            if (Guarding(G.Start, now)) return "Start";
            if (Guarding(G.Space, now)) return "Space";
            if (Guarding(G.A, now)) return "A";
            return nullptr;
        }

        /// SkipMovie (PC_MovieSkip: Escape and SpaceBar), SkipCinematic (PC_CineSkip) and the static binds' GuiKey
        /// Escape/Start, on an SFXGameMode. Returns true to drop the call.
        bool DropSkip(UObject* mode, UFunction* function, void* parms, bool guiKey)
        {
            auto const ours = KeyGuard(GetTickCount64());
            if (guiKey)
            {
                auto const event = parms != nullptr ? *static_cast<unsigned char const*>(parms) : 0xFF;
                if (event != GuiStart && event != GuiEscape && event != GuiStartRelease && event != GuiEscapeRelease) return false;
                if (ours == nullptr) return false;
                LEASI_INFO("GuiKey {} on {} dropped: our {}", event, PathOf(mode), ours);
                return true;
            }

            auto const name = function->Name.Instanced();
            LEASI_INFO("{} on {}: {}{}", ToUtf8(std::wstring_view(name.Chars(), name.Length())), PathOf(mode),
                       ours != nullptr ? "dropped, our " : "passed", ours != nullptr ? ours : "");
            static SFXName const SkipMovie{ L"SkipMovie", 0 };
            if (ours == nullptr && function->Name == SkipMovie && Bink::PauseWanted()) Bink::RequestResume("SkipMovie");
            return ours != nullptr;
        }

        // ! Kismet movie actions
        // ========================================

        /// BioSeqAct_MovieBink.Activated and Deactivated, under BinkAndEvent: the movie a Kismet action plays and the id of the
        /// Wwise event it posts beside the picture are latched for Bink.cpp, which pauses that event with the movie; its
        /// Deactivated clears them, unless another movie's action was latched since. The line cross-checks the id with
        /// GetIDFromString of the event's lowercased name.
        void LatchMovie(UObject* object, bool activated)
        {
            MovieAction action;
            if (!MovieActionOf(object, action)) return;
            auto const movie = action.Movie != nullptr ? ToUtf8(std::wstring_view(action.Movie->Chars(), action.Movie->Length())) : std::string{};
            if (!activated)
            {
                LEASI_INFO("movie event {}: {} (Deactivated), {}, thread {}", MovieSound::Unlatch(movie) ? "cleared" : "not cleared, the latch is not this movie's",
                           movie, PathOf(object), GetCurrentThreadId());
                LEASI_FLUSH();
                return;
            }
            MovieSound::Latch(movie, action.EventId, action.Beside != nullptr);
            std::string check = "no Wwise event";
            if (action.Event != nullptr)
            {
                auto event = NameText(action.Event->Name);
                for (auto& c : event)
                {
                    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                }
                auto const hash = MovieSound::Hash(event.c_str());
                check = hash == 0 ? std::format("no hash of {}: the exe exports no GetIDFromString", NameText(action.Event->Name))
                                  : std::format("hash of {} {}{}", NameText(action.Event->Name), hash, hash == action.EventId ? "" : ": DIFFERS");
            }
            if (action.Beside != nullptr)
            {
                auto const via = action.Via == L"Matinee"      ? std::string{ "a Matinee's event track starts it:" }
                               : action.Via == L"Conversation" ? std::string{ "a conversation line starts it:" }
                               : action.Via == L"Beside"       ? std::string{ "it starts with" }
                                                               : std::format("its {} output starts", ToUtf8(action.Via));
                check += std::format("; its sound is not known: {} {}", via, PathOf(action.Beside));
            }
            LEASI_INFO("movie event latched: {} id {} ({}), {}, thread {}", movie, action.EventId, check, PathOf(object), GetCurrentThreadId());
            LEASI_FLUSH();
        }

        // ! Banner
        // ========================================

        struct CanvasFunctions
        {
            bool Tried = false;
            UFunction* DrawText = nullptr;
            UFunction* TextSize = nullptr;
        };
        CanvasFunctions Fn;

        FColor Color(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
        {
            FColor c;
            c.R = r;
            c.G = g;
            c.B = b;
            c.A = a;
            return c;
        }

        /// One line centred on the screen, its top at <paramref name="top"/>, with a drop shadow.
        void DrawCentered(UCanvas* canvas, wchar_t const* text, float top, float scale, FColor color)
        {
            static_assert(sizeof(FString) == 0x10);
            FString const line{ text };

            UCanvas_execTextSize_Parms size;
            std::memcpy(&size.String, &line, sizeof(FString));
            CallGame(canvas, Fn.TextSize, &size);
            auto const left = (static_cast<float>(canvas->SizeX) - size.XL * scale) * 0.5f;
            auto const shadow = scale > 1.5f ? 2.0f : 1.0f;

            for (int pass = 0; pass < 2; pass++)
            {
                UCanvas_execDrawText_Parms draw;
                std::memcpy(&draw.Text, &line, sizeof(FString));
                draw.XScale = scale;
                draw.YScale = scale;
                // CurX/CurY are relative to the canvas origin.
                canvas->CurX = left + (pass == 0 ? shadow : 0.0f) - canvas->OrgX;
                canvas->CurY = top + (pass == 0 ? shadow : 0.0f) - canvas->OrgY;
                canvas->DrawColor = pass == 0 ? Color(0, 0, 0, color.A) : color;
                CallGame(canvas, Fn.DrawText, &draw);
            }
        }

        /// "PAUSE" near the top centre, and the keys under it: Start and Y when Start paused or the player uses a gamepad
        /// (PlayerInput's bUsingGamepad, which the game's own button prompts follow), else Esc and F7. The HUD's own font,
        /// position and colour are put back.
        void DrawBanner(ABioHUD* hud)
        {
            auto const canvas = hud->Canvas;
            if (canvas == nullptr || canvas->SizeX <= 0 || canvas->SizeY <= 0) return;
            if (!Fn.Tried)
            {
                Fn.Tried = true;
                Fn.DrawText = FindFunction(UCanvas::StaticClass(), SFXName{ L"DrawText", 0 });
                Fn.TextSize = FindFunction(UCanvas::StaticClass(), SFXName{ L"TextSize", 0 });
                if (Fn.DrawText == nullptr || Fn.TextSize == nullptr)
                {
                    LEASI_WARN("banner off: Canvas.DrawText or Canvas.TextSize not found");
                    G.BannerOff = true;
                    return;
                }
            }
            auto const engine = GEngine != nullptr ? *GEngine : nullptr;
            auto font = engine != nullptr ? engine->MediumFont : nullptr;
            if (font == nullptr) font = canvas->Font;
            if (font == nullptr) return;

            auto const savedFont = canvas->Font;
            auto const savedColor = canvas->DrawColor;
            auto const savedX = canvas->CurX;
            auto const savedY = canvas->CurY;
            auto const savedCenter = canvas->bCenter;
            canvas->Font = font;
            canvas->bCenter = 0;

            // Sized for 1080 lines, like the HUD.
            auto const height = static_cast<float>(canvas->SizeY);
            auto const scale = height / 1080.0f;
            DrawCentered(canvas, L"PAUSE", height * 0.08f, 2.5f * scale, Color(255, 255, 255, 235));
            auto const input = hud->PlayerOwner != nullptr ? hud->PlayerOwner->PlayerInput : nullptr;
            auto const gamepad = G.PausedByStart || (input != nullptr && input->bUsingGamepad != 0);
            auto const keys = gamepad ? L"Start: resume     Y: photo mode" : L"Esc: resume     F7: photo mode";
            DrawCentered(canvas, keys, height * 0.08f + 48.0f * scale, 1.1f * scale, Color(200, 200, 200, 220));

            canvas->Font = savedFont;
            canvas->DrawColor = savedColor;
            canvas->CurX = savedX;
            canvas->CurY = savedY;
            canvas->bCenter = savedCenter;
        }

        // ! Per frame
        // ========================================

        /// The console is open (its Typing or Open state), read each frame: it closes on keys that aren't ours to watch.
        bool ConsoleIsOpen()
        {
            static SFXName const Typing{ L"Typing", 0 };
            static SFXName const Open{ L"Open", 0 };
            auto const engine = GEngine != nullptr ? *GEngine : nullptr;
            if (engine == nullptr || engine->GamePlayers.Count() == 0) return false;
            auto const player = engine->GamePlayers.GetData()[0];
            auto const client = player != nullptr ? player->ViewportClient : nullptr;
            auto const console = client != nullptr ? client->ViewportConsole : nullptr;
            auto const frame = console != nullptr ? console->StateFrame : nullptr;
            auto const state = frame != nullptr ? frame->StateNode : nullptr;
            return state != nullptr && (state->Name == Typing || state->Name == Open);
        }

        /// What Overlay.cpp shows of our world pause, after each frame's checks: nothing while photo mode runs and for a few
        /// frames after it, nor while the console is open (both at once, no fade); photo mode's key only where it opens and
        /// answers (its input and GUI tick hooks are in).
        void PublishOverlay(ABioPlayerController* pc)
        {
            auto const photoOn = PhotoMode::On();
            if (photoOn) G.PhotoHideFrames = PhotoHideGrace;
            else if (G.PhotoHideFrames > 0) G.PhotoHideFrames--;
            auto const hidden = G.WorldPaused && (photoOn || G.PhotoHideFrames > 0 || ConsoleIsOpen());
            auto const mode = ModeOf(pc);
            auto const photo = PhotoMode::Answers() && pc != nullptr && pc == G.Controller && (mode == ModeConversation || mode == ModeCinematic);
            Overlay::Publish(G.WorldPaused && !hidden, G.WorldPaused && !hidden && photo, hidden);
        }

        /// Frames on which ReadGameTexts may try, each with a game object: the strings are there from the first.
        constexpr unsigned MaxGameTextTries = 3;

        /// One string of the game's string table, in its language: GetSimpleString, a native that any game object of that class
        /// runs. The FString it returns is the game's allocation, left to it: a few bytes, once a session. Empty for id 0.
        std::string GameString(StringGame* game, int id)
        {
            if (id == 0) return {};
            static CachedFunction cache;
            GetSimpleStringParms parms;
            parms.StrRef = id;
            parms.bParse = 0;
            if (!Call(game, L"GetSimpleString", cache, parms)) return {};
            return ToUtf8(std::wstring_view(parms.ReturnValue.Chars(), parms.ReturnValue.Length()));
        }

        /// Once the world has a game object: the pause screen's words from the game's string table (game/LE<n>.hpp's PauseTexts),
        /// for Overlay.cpp. Tried again on the next frames while none comes back.
        void ReadGameTexts(ABioPlayerController* pc)
        {
            auto const world = pc != nullptr ? pc->WorldInfo : nullptr;
            auto const game = world != nullptr ? world->Game : nullptr;
            if (game == nullptr || !game->IsA(StringGame::StaticClass())) return;
            // Counted first: a fault in the calls costs a try, not a frame's checks each frame.
            G.GameTextTries++;
            auto const sp = static_cast<StringGame*>(game);
            auto title = GameString(sp, PauseTexts.Title);
            auto resume = GameString(sp, PauseTexts.Resume);
            auto photo = GameString(sp, PauseTexts.Photo);
            if (title.empty() && resume.empty() && photo.empty())
            {
                if (G.GameTextTries >= MaxGameTextTries) LEASI_WARN("the game's string table gave none of the pause screen's texts: ours stay");
                return;
            }
            G.GameTextTries = MaxGameTextTries;
            LEASI_INFO("the game's string table: title \"{}\", resume \"{}\", photo mode \"{}\"", title, resume, photo);
            Overlay::UseGameTexts(std::move(title), std::move(resume), std::move(photo));
        }

        void ReadGameTextsEntry(void* context) { ReadGameTexts(static_cast<ABioPlayerController*>(context)); }

        /// The object of that class and name right under that package, from the object list, or null. Names are compared first:
        /// the list holds nearly 200,000 objects.
        template <class T>
        T* FindObject(SFXName const& package, SFXName const& name)
        {
            auto& objects = *UObject::GObjObjects;
            for (UINT i = 0; i < objects.Count(); i++)
            {
                auto const object = objects.GetData()[i];
                if (object != nullptr && object->Name == name && object->Outer != nullptr && object->Outer->Name == package
                    && object->IsA(T::StaticClass()))
                    return static_cast<T*>(object);
            }
            return nullptr;
        }

        /// Frames on which ReadGameIcons may try: the textures load with the game, before the first HUD.
        constexpr unsigned MaxGameIconTries = 3;

        /// The texture's D3D11 view, read raw (Game::TextureView), or null where a method table isn't the expected one
        /// (another build of the game) or nothing is there yet. Overlay.cpp checks the view itself.
        void* ViewOf(UTexture2D* texture)
        {
            using Bytes = unsigned char const*;
            auto const exe = reinterpret_cast<Bytes>(GetModuleHandleW(nullptr));
            auto const resource = static_cast<Bytes>(texture->Resource);
            if (resource == nullptr || *reinterpret_cast<Bytes const*>(resource) != exe + TextureView.ResourceTableRva) return nullptr;
            auto const inner = *reinterpret_cast<Bytes const*>(resource + TextureView.Rhi);
            if (inner == nullptr || *reinterpret_cast<Bytes const*>(inner - TextureView.Base) != exe + TextureView.RhiTableRva) return nullptr;
            return *reinterpret_cast<void* const*>(inner + TextureView.View);
        }

        /// The views of the gamepad's buttons (game/LE<n>.hpp's PauseIcons), for Overlay.cpp. Tried again on the next frames
        /// while one is missing; a fault ends the tries (AfterPostRender).
        void ReadGameIcons()
        {
            G.GameIconTries++;
            SFXName const package{ PauseIcons.Package, 0 };
            auto const padA = FindObject<UTexture2D>(package, SFXName{ PauseIcons.PadA, 0 });
            auto const padY = FindObject<UTexture2D>(package, SFXName{ PauseIcons.PadY, 0 });
            auto const viewA = padA != nullptr ? ViewOf(padA) : nullptr;
            auto const viewY = padY != nullptr ? ViewOf(padY) : nullptr;
            if (viewA == nullptr || viewY == nullptr)
            {
                if (G.GameIconTries >= MaxGameIconTries)
                    LEASI_WARN("no view of the game's gamepad icons (texture A {}, Y {}; view A {}, Y {}): the pause screen draws its own", PathOf(padA), PathOf(padY),
                               viewA != nullptr, viewY != nullptr);
                return;
            }
            G.GameIconTries = MaxGameIconTries;
            Overlay::UseGameIcons(viewA, viewY);
        }

        void ReadGameIconsEntry(void*) { ReadGameIcons(); }

        /// Frames on which ReadGameFonts may try: the movies load with the game, before the first HUD.
        constexpr unsigned MaxGameFontTries = 3;

        /// The bytes of a font's movie, empty without them.
        std::span<unsigned char const> MovieBytes(UGFxMovieInfo* movie)
        {
            if (movie == nullptr) return {};
            return { movie->RawData.GetData(), static_cast<std::size_t>(movie->RawData.Count()) };
        }

        /// The game's loader brings back the package of a movie it doesn't keep in memory (game/LE<n>.hpp's PauseFonts.LoadPath),
        /// on this thread, for the one read after it: the next garbage collection drops it.
        void LoadFontMovies(ABioPlayerController* pc)
        {
            static CachedFunction cache;
            FString const path{ PauseFonts.LoadPath };
            UObject_execDynamicLoadObject_Parms parms;
            std::memcpy(&parms.ObjectName, &path, sizeof(FString));
            parms.ObjectClass = UObjectReferencer::StaticClass();
            parms.MayFail = 1;
            auto const start = GetTickCount64();
            if (Call(pc, L"DynamicLoadObject", cache, parms) && parms.ReturnValue != nullptr)
                LEASI_INFO("the game's fonts: loaded their package in {} ms: {}", GetTickCount64() - start, PathOf(parms.ReturnValue));
            else
                LEASI_WARN("the game's fonts: their package did not load ({})", ToUtf8(PauseFonts.LoadPath));
        }

        /// The bytes of the movies that hold the game's fonts (game/LE<n>.hpp's PauseFonts), which Overlay.cpp converts in this
        /// call. Tried again on the next frames while there are none; a fault ends the tries (AfterPostRender).
        void ReadGameFonts(ABioPlayerController* pc)
        {
            G.GameFontTries++;
            SFXName const package{ PauseFonts.Package, 0 };
            if (PauseFonts.LoadPath != nullptr && pc != nullptr && FindObject<UGFxMovieInfo>(package, SFXName{ PauseFonts.LightMovie, 0 }) == nullptr)
                LoadFontMovies(pc);
            auto const lightMovie = FindObject<UGFxMovieInfo>(package, SFXName{ PauseFonts.LightMovie, 0 });
            auto const boldMovie = FindObject<UGFxMovieInfo>(package, SFXName{ PauseFonts.BoldMovie, 0 });
            auto const light = MovieBytes(lightMovie);
            auto const bold = MovieBytes(boldMovie);
            if (light.empty() || bold.empty())
            {
                if (G.GameFontTries >= MaxGameFontTries)
                    LEASI_WARN("no bytes from the game's fonts movies ({}: {}, {}: {}): the pause screen keeps Exo 2", PathOf(lightMovie), light.size(), PathOf(boldMovie),
                               bold.size());
                return;
            }
            G.GameFontTries = MaxGameFontTries;
            Overlay::UseGameFonts({ light, PauseFonts.LightFont }, { bold, PauseFonts.BoldFont });
        }

        void ReadGameFontsEntry(void* context) { ReadGameFonts(static_cast<ABioPlayerController*>(context)); }

        /// The checks of each frame. The banner is drawn apart, under its own guard: a fault while drawing must not stop them.
        void AfterPostRender(ABioHUD* hud)
        {
            GameThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
            auto const pc = AsBio(hud->PlayerOwner);
            if (!G.PlayerChecked)
            {
                G.PlayerChecked = true;
                auto const local = LocalController(nullptr);
                LEASI_INFO("first BioHUD.PostRender on thread {}: HUD owner {}, GEngine's local player controller {}{}", GetCurrentThreadId(), PathOf(pc),
                           PathOf(local), local == pc ? " (same)" : " (DIFFERENT: Esc would read the wrong controller)");
            }

            auto const serial = Bink::MovieSerial();
            if (Bink::OpenMovies() > 0 && serial != G.HudMovieSerial)
            {
                G.HudMovieSerial = serial;
                LEASI_INFO("BioHUD.PostRender runs while movie #{} is open, thread {}", serial, GetCurrentThreadId());
            }

            // A movie is only paused while the HUD is silent: once it draws again for a second, the game moved on and nothing
            // covers the screen. Nothing else of the game's would end that pause (Esc goes to the movie while it is on).
            if (Bink::PauseWanted() && GetTickCount64() - G.HudDrawsSince >= HudBackMs)
            {
                Bink::RequestResume("the HUD has drawn for 1 s: no fullscreen movie covers the screen");
                LEASI_FLUSH();
            }

            Validate(pc, "frame");
            PhotoMode::Frame();
            PublishOverlay(pc);

            // Under its own guard: a fault there ends the reading, not the checks above.
            if (G.GameTextTries < MaxGameTextTries && !Guarded(ReadGameTextsEntry, pc))
            {
                G.GameTextTries = MaxGameTextTries;
                LEASI_ERROR("access violation while reading the game's string table: the pause screen keeps its own texts");
            }
            if (G.GameIconTries < MaxGameIconTries && !Guarded(ReadGameIconsEntry, nullptr))
            {
                G.GameIconTries = MaxGameIconTries;
                LEASI_ERROR("access violation while reading the game's gamepad icons: the pause screen draws its own");
            }
            // After the texts: the fonts are cut to their letters.
            if (G.GameTextTries >= MaxGameTextTries && G.GameFontTries < MaxGameFontTries && !Guarded(ReadGameFontsEntry, pc))
            {
                G.GameFontTries = MaxGameFontTries;
                LEASI_ERROR("access violation while reading the game's fonts: the pause screen keeps Exo 2");
            }
        }

        // ! Guarded entry points
        // ========================================

        struct KeyCall
        {
            UConsole* Console;
            UFunction* Function;
            UConsole_execInputKey_Parms* Parms;
            bool Taken;
        };

        void TakeKeyEntry(void* context)
        {
            auto const c = static_cast<KeyCall*>(context);
            c->Taken = TakeKey(c->Console, c->Function, c->Parms);
        }

        void AfterPostRenderEntry(void* context) { AfterPostRender(static_cast<ABioHUD*>(context)); }

        void DrawBannerEntry(void* context) { DrawBanner(static_cast<ABioHUD*>(context)); }

        void ResumeWorldEntry(void*) { ResumeWorld("fault"); }

        struct MovieCall
        {
            UObject* Action;
            bool Activated;
        };

        void LatchMovieEntry(void* context)
        {
            auto const c = static_cast<MovieCall*>(context);
            LatchMovie(c->Action, c->Activated);
        }

        struct TickCall
        {
            ABioPlayerController* Pc;
            float DeltaTime;
        };

        void PhotoTickEntry(void* context)
        {
            auto const c = static_cast<TickCall*>(context);
            PhotoMode::Tick(c->Pc, c->DeltaTime);
        }

        constexpr unsigned MaxFrameFaults = 3;

        void PublishBareEntry(void*)
        {
            auto const photoOn = PhotoMode::On();
            Overlay::Publish(G.WorldPaused && !photoOn && Alive(G.World, G.WorldIndex) && G.World->Pauser != nullptr, false, photoOn);
        }

        /// After an access violation in the per-frame checks: our state may be what faulted, so it ends here, photo mode
        /// first, and a world we paused is resumed (under its own guard) rather than left paused with nothing of ours to
        /// resume it.
        void DropState()
        {
            PhotoMode::Drop("fault", false);
            if (G.WorldPaused && !Guarded(ResumeWorldEntry, nullptr))
                LEASI_ERROR("access violation while resuming the world: if the game stays paused, the console's \"pause\" resumes it");
            EndWorldPause();
            if (Bink::PauseWanted()) Bink::RequestResume("fault");
        }

        // ! ProcessEvent hook
        // ========================================

        using t_UObject_ProcessEvent = void(UObject* Context, UFunction* Function, void* Parms, void* Result);

        /// The loader makes the hook live before InstallHook hands back the original, so a call can arrive while this is still
        /// null: such a call waits for it.
        std::atomic<t_UObject_ProcessEvent*> ProcessEvent_orig{ nullptr };

        void ProcessEvent_hook(UObject* Context, UFunction* Function, void* Parms, void* Result)
        {
            auto orig = ProcessEvent_orig.load(std::memory_order_acquire);
            while (orig == nullptr)
            {
                YieldProcessor();
                orig = ProcessEvent_orig.load(std::memory_order_acquire);
            }
            // The game thread's heartbeat for Bink.cpp's movie reports, written only when the tick count moved, and the end of
            // its stalls for Start.
            if (GetCurrentThreadId() == GameThreadId.load(std::memory_order_relaxed))
            {
                auto const now = GetTickCount64();
                auto const last = GameThreadTick.load(std::memory_order_relaxed);
                if (last != now)
                {
                    if (last != 0 && now - last > GameStallMs) G.StallEndedAt = now;
                    GameThreadTick.store(now, std::memory_order_relaxed);
                }
            }
            if (InOwnCall || Context == nullptr || Function == nullptr)
            {
                orig(Context, Function, Parms, Result);
                return;
            }

            // Before the original: the movie its Activated starts finds the latch in place.
            if constexpr (Game::Movies == MoviePolicy::BinkAndEvent)
            {
                static SFXName const Activated{ L"Activated", 0 };
                static SFXName const Deactivated{ L"Deactivated", 0 };
                if (Function->Name == Activated || Function->Name == Deactivated)
                {
                    MovieCall call{ Context, Function->Name == Activated };
                    if (!Guarded(LatchMovieEntry, &call)) LEASI_ERROR("access violation while reading a Kismet movie action: nothing latched or cleared");
                }
            }

            static SFXName const InputKey{ L"InputKey", 0 };
            static SFXName const PostRender{ L"PostRender", 0 };
            static SFXName const SkipMovie{ L"SkipMovie", 0 };
            static SFXName const SkipCinematic{ L"SkipCinematic", 0 };
            static SFXName const GuiKey{ L"GuiKey", 0 };
            static SFXName const PhotoMenu{ PhotoMenuFunction, 0 };
            static SFXName const PlayerTick{ L"PlayerTick", 0 };

            auto const& name = Function->Name;
            if (name == InputKey)
            {
                if (Parms != nullptr && Context->IsA(UConsole::StaticClass()))
                {
                    KeyCall call{ static_cast<UConsole*>(Context), Function, static_cast<UConsole_execInputKey_Parms*>(Parms), false };
                    if (!Guarded(TakeKeyEntry, &call))
                    {
                        LEASI_ERROR("access violation while reading a key: passed to the game{}", PhotoMode::On() ? ", photo mode left" : "");
                        call.Taken = false;
                        PhotoMode::Drop("fault while reading a key", false);
                    }
                    if (call.Taken) return;
                }
            }
            else if (name == SkipMovie || name == SkipCinematic || name == GuiKey)
            {
                if (Context->IsA(USFXGameModeBase::StaticClass()) && DropSkip(Context, Function, Parms, name == GuiKey)) return;
            }
            else if (name == PhotoMenu)
            {
                // Photo mode's Esc and Start binds: they would hide it and open the pause menu over the conversation. Ours
                // take those keys first in Console.InputKey; this is for a bind that gets past it.
                if (PhotoMode::On() && Context->IsA(USFXGameModePhoto::StaticClass()))
                {
                    LEASI_INFO("{} on {} dropped: our photo mode is left with Esc, Start, Y or F7", NameText(name), PathOf(Context));
                    return;
                }
            }

            orig(Context, Function, Parms, Result);

            if (name == PlayerTick && PhotoMode::IsController(Context) && Parms != nullptr)
            {
                TickCall call{ static_cast<ABioPlayerController*>(Context), static_cast<APlayerController_eventPlayerTick_Parms*>(Parms)->DeltaTime };
                if (!Guarded(PhotoTickEntry, &call))
                {
                    LEASI_ERROR("access violation while driving the photo camera: photo mode left");
                    PhotoMode::Drop("fault in the photo camera", false);
                }
            }

            if (name == PostRender && Context->IsA(ABioHUD::StaticClass()))
            {
                // Before the guarded checks: written even when they are off for the session. Only this thread writes it.
                auto const now = GetTickCount64();
                auto const last = HudTick.load(std::memory_order_relaxed);
                if (last == 0 || now - last > HudSilentMs) G.HudDrawsSince = now;
                HudTick.store(now, std::memory_order_relaxed);
                if (G.FrameFaults < MaxFrameFaults && !Guarded(AfterPostRenderEntry, Context))
                {
                    G.FrameFaults++;
                    LEASI_ERROR("access violation in the checks after BioHUD.PostRender ({} of {}): our pause state dropped{}", G.FrameFaults,
                                MaxFrameFaults, G.FrameFaults == MaxFrameFaults ? ", per-frame checks off for the session" : "");
                    DropState();
                }
                // With the checks off for the session, our state isn't kept up: the pause screen shows a pause Esc made only
                // while that world still exists and is paused, without photo mode's key.
                if (G.FrameFaults >= MaxFrameFaults && !Guarded(PublishBareEntry, nullptr)) Overlay::Publish(false, false, false);
                // The Canvas banner: only where the pause screen can't draw (its hooks or its start failed).
                if (!Overlay::Drawing() && G.WorldPaused && !PhotoMode::On() && !G.BannerOff && !Guarded(DrawBannerEntry, Context))
                {
                    LEASI_ERROR("access violation while drawing the banner: banner off");
                    G.BannerOff = true;
                }
            }
        }

    }

    void InitializeGlobals(::LESDK::Initializer& Init)
    {
        Common::InitializeRequiredGlobals(Init);
        GEngine = Init.ResolveTyped<UEngine*>(BUILTIN_GENGINE_RVA);
        CHECK_RESOLVED(GEngine);
    }

    void InstallHooks(::LESDK::Initializer& Init)
    {
        auto const target = Init.ResolveTyped<t_UObject_ProcessEvent>(BUILTIN_PROCESSEVENT_RVA);
        CHECK_RESOLVED(target);
        auto const orig = static_cast<t_UObject_ProcessEvent*>(Init.InstallHook("UObject::ProcessEvent", target, ProcessEvent_hook));
        CHECK_RESOLVED(orig);
        ProcessEvent_orig.store(orig, std::memory_order_release);
        LEASI_INFO("ProcessEvent hooked");

        PhotoMode::InstallHooks(Init);

    }

    DWORD GameThread() { return GameThreadId.load(std::memory_order_relaxed); }

    ULONGLONG GameThreadSeen() { return GameThreadTick.load(std::memory_order_relaxed); }

    ULONGLONG HudSeen() { return HudTick.load(std::memory_order_relaxed); }
}
