#include "PauseAnytime/Pause.hpp"
#include "PauseAnytime/Engine.hpp"
#include "PauseAnytime/KeyRules.hpp"
#include "PauseAnytime/Minigame.hpp"
#include "PauseAnytime/Photo.hpp"
#include "PauseAnytime/game/Game.hpp"
#include "PauseAnytime/SharedVersion.h"
#include "Common/Objects.hpp"

#include <array>
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

        /// The first repeat of a held gamepad button comes this long after its press (InitialButtonRepeatDelay, see
        /// KeyRules::StartStaleMs).
        constexpr ULONGLONG StartRepeatMs = 200;

        // KeyRules.hpp keeps its own copy of the skip guard's times, so that its tests build without Api.hpp's Windows types.
        static_assert(KeyRules::GuardMs == GuardMs && KeyRules::ReleaseGraceMs == ReleaseGraceMs, "KeyRules.hpp and Api.hpp disagree");

        /// A movie pause is lifted once the HUD has drawn this long without a gap: no fullscreen movie covers the screen.
        constexpr ULONGLONG HudBackMs = 1000;

        /// Frames the pause screen stays hidden after photo mode ends: the render thread presents a frame or two behind this
        /// one, and photo mode's last frames must not show it.
        constexpr unsigned PhotoHideGrace = 3;

        /// Access violations in the per-frame checks that turn them off for the session (State::FrameFaults).
        constexpr unsigned MaxFrameFaults = 3;

        // ! State
        // ========================================

        /// A pause key's press as Console.InputKey sees it: whose it is (KeyRules::Hold, changed by SetHold only), and when.
        using KeyRules::Hold;
        using KeyRules::KeyState;

        /// What our world pause holds: nothing, a conversation or an in-engine cutscene, or a minigame's board while its
        /// clock runs (Minigame.hpp). Changed by SetPauseKind only.
        enum class PauseKind : unsigned char { Off, Scene, Minigame };

        char const* KindName(PauseKind kind)
        {
            switch (kind)
            {
            case PauseKind::Scene: return "scene";
            case PauseKind::Minigame: return "minigame";
            default: return "off";
            }
        }

        struct State
        {
            /// Our world pause, its kind (PauseKind), the WorldInfo we paused (checked again before each use: a map change
            /// or a load replaces it) and the controller that paused it, which resumes it.
            PauseKind Kind = PauseKind::Off;
            Kept<AWorldInfo> World;
            Kept<ABioPlayerController> Controller;
            /// A minigame pause's panel (a UBioSFPanel): the pause ends with it.
            Kept<UObject> MinigamePanel;
            unsigned char PausedMode = NoMode;
            ULONGLONG PausedAt = 0;
            /// Start paused it: the banner names Start, also before PlayerInput has seen the pad (our press never reaches it).
            bool PausedByStart = false;
            /// The pause's sound holds the game's audio (Game::PauseSoundHoldsAudio) and its end has not played yet. It outlives
            /// our pause while that end found nothing to play it (no controller or GUI manager, as after a load): the next
            /// Validate tries again (MaxAudioTries).
            bool AudioHeld = false;
            /// Tries of that end that did not play.
            unsigned AudioTries = 0;

            /// The first game-thread ProcessEvent after a gap of more than KeyRules::GameStallMs, and the last one before it.
            ULONGLONG StallEndedAt = 0;
            ULONGLONG StallStartedAt = 0;
            /// A fault while reading the Steam overlay's and the console's state for the movie's thread: not read again.
            bool ElsewhereOff = false;

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

            /// The read of the pause screen's words (TextRules::TextRead), changed by SetTextRead only, and its tries while
            /// Waiting.
            TextRules::TextRead TextRead = TextRules::TextRead::Waiting;
            unsigned TextTries = 0;
            /// The launch language, and the words picked: its built-in text until the read is done (InstallHooks). The
            /// banner draws them, and the game's fonts are cut to their letters.
            TextRules::Language Language = TextRules::Language::English;
            TextRules::Picks Texts;
            /// The banner's words shown in a built-in text in their place (not Latin-1) and logged so: one bit per
            /// TextRules::Word.
            unsigned BannerStandInLogged = 0;
            /// Frames on which ReadGameIcons tried (MaxGameIconTries once it is done).
            unsigned GameIconTries = 0;
            /// Frames on which ReadGameFonts tried (MaxGameFontTries once it is done).
            unsigned GameFontTries = 0;

            /// Our world pause holds, of either kind.
            bool Paused() const { return Kind != PauseKind::Off; }
        };
        State G;

        /// The one change of State::Kind.
        void SetPauseKind(PauseKind kind) { G.Kind = kind; }

        std::atomic<DWORD> GameThreadId{ 0 };
        std::atomic<ULONGLONG> GameThreadTick{ 0 };
        /// The end of the latest BioHUD.PostRender: Bink.cpp's movie threads read it.
        std::atomic<ULONGLONG> HudTick{ 0 };
        /// The Steam overlay or the console is open, read once a tick on the game thread: Bink.cpp's movie threads read it.
        std::atomic<bool> Elsewhere{ false };

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

        /// Our world pause's state ends. The audio hold is not part of it: it ends when the resume's sound plays.
        void EndWorldPause()
        {
            SetPauseKind(PauseKind::Off);
            G.World.Set(nullptr);
            G.Controller.Set(nullptr);
            G.MinigamePanel.Set(nullptr);
            G.PausedMode = NoMode;
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

        /// Validates on which the end of the pause's sound is tried again when it did not play.
        constexpr unsigned MaxAudioTries = 3;

        /// Posts the end of what the pause's sound holds, once, for what Validate meets: ResumeWorld does it for a resume of
        /// ours, in the frame the world resumes. Here it is the Steam overlay while it is open and the world is still held, as
        /// a voice line still held when the world runs again is lost and a posting after that saves it no more
        /// (docs/how-it-works.md), or a pause already over, which gives back the rest of the audio, or an end that did not
        /// play before. The hold is cleared before the call, so a fault in it ends the tries; it is set again when the sound
        /// did not play, for the next Validate.
        void ReleaseAudio(ABioPlayerController* pc, char const* why)
        {
            if (!G.AudioHeld) return;
            G.AudioHeld = false;
            LEASI_INFO("game audio released: {}", why);
            if (pc == nullptr) pc = G.Controller.Live();
            if (pc == nullptr) pc = LocalController(nullptr);
            if (PlayPauseSound(pc, ResumeSound)) G.AudioTries = 0;
            else if (++G.AudioTries < MaxAudioTries) G.AudioHeld = true;
            else
            {
                G.AudioTries = 0;
                LEASI_WARN("game audio still held: {} did not play in {} tries, the game's audio waits for a game menu to play it", ToUtf8(ResumeSound),
                           MaxAudioTries);
            }
        }

        /// Pauses the world for a conversation or a cutscene, or, with <paramref name="minigame"/> (its panel), for a minigame
        /// whose clock runs.
        bool PauseWorld(ABioPlayerController* pc, unsigned char mode, Minigame::Board const* minigame = nullptr)
        {
            auto const world = pc->WorldInfo;
            if (world == nullptr || world->Pauser != nullptr) return false;
            if (!CallPause(pc)) return false;
            if (world->Pauser == nullptr)
            {
                LEASI_WARN("world pause refused: Pause ran and WorldInfo.Pauser is still None (GameInfo.AllowPausing?), thread {}", GetCurrentThreadId());
                return false;
            }
            SetPauseKind(minigame != nullptr ? PauseKind::Minigame : PauseKind::Scene);
            G.World.Set(world);
            G.Controller.Set(pc);
            G.MinigamePanel.Set(minigame != nullptr ? minigame->Panel : nullptr);
            G.PausedMode = mode;
            G.PausedAt = GetTickCount64();
            LEASI_INFO("world paused in mode {} ({}){}: Pauser {} ({}), {}, thread {}", mode, ModeName(mode),
                       minigame != nullptr ? std::format(" over minigame {} ({})", Minigame::Describe(*minigame), PathOf(minigame->Panel)) : std::string{},
                       PathOf(world->Pauser), static_cast<void const*>(world->Pauser), PathOf(world), GetCurrentThreadId());
            // A hold whose end is still being tried stays held: this pause's end gives it back too.
            if (PlayPauseSound(pc, PauseSound) && PauseSoundHoldsAudio) G.AudioHeld = true;
            G.AudioTries = 0;
            return true;
        }

        /// Toggles the pause back through the controller that paused, only while the world is still paused: Pause on an
        /// unpaused world would pause it again. Our state ends before the call, so a fault inside it can't leave it armed. A
        /// key's resume (<paramref name="byKey"/>) plays the game's sound for it, and so does any resume while its pause's
        /// sound holds the game's audio. When that sound does not play, the hold stays for the next Validate (ReleaseAudio).
        void ResumeWorld(char const* why, bool byKey = false)
        {
            if (!G.Paused()) return;
            auto const kind = G.Kind;
            auto const mode = G.PausedMode;
            auto const since = G.PausedAt;
            auto const world = G.World.Live();
            auto const controller = G.Controller.Live();
            auto const audioHeld = G.AudioHeld;
            auto const worldAlive = world != nullptr;
            auto const controllerAlive = controller != nullptr;
            auto const giveAudioBack = [&]
            {
                if (audioHeld && !PlayPauseSound(controllerAlive ? controller : LocalController(nullptr), ResumeSound)) G.AudioHeld = true;
            };
            EndWorldPause();
            // The hold ends with the rest, and is set again below only when the resume's sound does not play.
            G.AudioHeld = false;
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
                LEASI_INFO("world resumed ({}) after {:.1f} s, paused in mode {} ({}) over a {}, thread {}", why, HeldFor(since), mode, ModeName(mode),
                           KindName(kind), GetCurrentThreadId());
            if ((PauseSoundHoldsAudio ? audioHeld : byKey && pauser == nullptr) && !PlayPauseSound(controller, ResumeSound)) G.AudioHeld = audioHeld;
        }

        // ! World pause: checks
        // ========================================

        /// What ends our world pause besides its world and its Pauser, read under it: a scene pause ends when the mode left
        /// Conversation and Cinematic, a minigame pause when the mode left GUI or its panel closed. Under the pause neither
        /// can happen by itself (no GUI tick, no Kismet, no input), so this is the net under a load or a mod, not a path a
        /// test reaches. NoMode, no mode manager to read, is not a mode change. Null: nothing ends it.
        char const* PauseLeft(ABioPlayerController* pc)
        {
            auto const mode = ModeOf(pc);
            if (G.Kind == PauseKind::Minigame)
            {
                if (mode != ModeGui && mode != NoMode) return "mode changed";
                if (!Minigame::StillOpen(G.MinigamePanel.Live())) return "minigame closed";
                return nullptr;
            }
            return mode != ModeConversation && mode != ModeCinematic && mode != NoMode ? "mode changed" : nullptr;
        }

        /// Every frame and every key of ours: our world pause ends when someone else ended it (Pauser None: the game, a load,
        /// the Steam overlay closing clears every CanUnpause that agrees), when its WorldInfo is gone, or when what it held
        /// left under it (PauseLeft; Photo is ours while our photo session runs). Our photo session ends first, with the world
        /// or the mode it stands on. The movie pause isn't checked here: Bink.cpp ends it (close, Esc, its movie gone silent),
        /// and SkipMovie and the HUD drawing again do. An open Steam overlay will end the pause when it closes, with no key of
        /// ours: the audio goes back first, while the world is held. Returns true when it resumed our world pause itself:
        /// a press that met that pause is ours all the same.
        bool Validate(ABioPlayerController* pc, char const* where)
        {
            PhotoMode::Check(G.Paused(), G.World.Object);
            if (G.Paused())
            {
                if (G.World.Live() == nullptr)
                {
                    LEASI_INFO("world pause ended ({}): the WorldInfo we paused is gone (map change or load), thread {}", where, GetCurrentThreadId());
                    ReleaseAudio(pc, "our world pause ended with its world");
                    EndWorldPause();
                    LEASI_FLUSH();
                }
                else if (G.World.Object->Pauser == nullptr)
                {
                    // Besides the Steam overlay closing, the console's "pause" (or the engine's pause on a focus loss, where
                    // it is on: LE1's and LE2's BIOEngine.ini turn it off) clears our CanUnpause too. Nothing of ours hears of
                    // that before this frame's check, after the world has run a frame: a voice line the audio hold still held
                    // may be lost, and only the rest of the audio comes back.
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
                    if (!PhotoMode::On() && pc != nullptr && pc->WorldInfo == G.World.Object)
                    {
                        if (auto const left = PauseLeft(pc))
                        {
                            auto const mode = ModeOf(pc);
                            LEASI_WARN("{} under our world pause over a {} ({}): mode is {} ({}), resuming", left, KindName(G.Kind), where, mode,
                                       ModeName(mode));
                            ResumeWorld(left);
                            LEASI_FLUSH();
                            return true;
                        }
                    }
                }
            }
            else if (G.AudioHeld)
            {
                ReleaseAudio(pc, "our world pause is over and the end of its sound did not play: tried again");
                LEASI_FLUSH();
            }
            return false;
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

        /// A fresh press of Esc or Start, outside the open console: it leaves our photo mode, else it is the fullscreen movie's,
        /// else it resumes our world pause, else pauses the world in a conversation or a cutscene, or on a minigame's board
        /// while its clock runs. Returns what it did, or null for a press left to the game.
        char const* Press(UConsole* console, UFunction* function, Key key)
        {
            auto const pc = LocalController(console);
            // Noted before Validate: a session it ends now (the game left photo mode, or the world pause ended without us)
            // was this press's to end, and the press must not then resume or pause the world as well.
            auto const wasPhoto = PhotoMode::On();
            auto const resumed = Validate(pc, KeyName(key));
            auto const mode = ModeOf(pc);
            auto const world = pc != nullptr ? pc->WorldInfo : nullptr;
            auto const pauser = world != nullptr ? world->Pauser : nullptr;
            auto const movies = Bink::OpenMovies();
            auto const movie = MovieMode(pc);
            // Read only in the GUI mode, where a minigame shows; outside its playing state (the screen before the game, where
            // Esc cancels it, and its result) the press stays the game's.
            auto const board = mode == ModeGui && !G.Paused() ? Minigame::Find(pc) : Minigame::Board{};

            char const* action = nullptr;
            // Photo mode before the movie: a session runs under our world pause, with no fullscreen movie, and a stall in it
            // (a screenshot) can make the HUD look silent to the movie code.
            if (PhotoMode::On())
            {
                // Photo mode's own Esc and Start (ShowMenuGui) would open the pause menu over the conversation.
                PhotoMode::Drop(KeyName(key), false);
                action = "left photo mode, the world stays paused";
            }
            else if (wasPhoto)
                action = "taken: photo mode had ended before this press";
            // Validate resumed the world on this press: the game must not act on it too (open its menu, skip).
            else if (resumed)
                action = "taken: the world resumed, what it held had left";
            else if (Bink::OwnsKeys())
            {
                // Esc isn't toggled here: the movie's thread saw this press too and acts on it, so one press is one toggle.
                // That thread can't read Start. A tap it misses (between two polls of a paused movie that calls BinkWait
                // rarely, or within 150 ms of its last press), or one the two threads judge on either side of the HUD's
                // 250 ms or BinkWait's 500 ms edge, toggles nothing, and the player presses again: toggling here when its
                // tick is not fresh would make a press it reads a moment later two toggles.
                if (key == Key::Esc) action = "taken for the fullscreen movie, its thread toggles the movie pause";
                else action = Bink::Toggle("Start pressed") ? "toggled the movie pause" : "taken for the fullscreen movie, nothing to toggle";
            }
            else if (G.Paused())
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
            else if (board.Playing && world != nullptr && pauser == nullptr)
            {
                if (PauseWorld(pc, mode, &board))
                {
                    G.PausedByStart = key == Key::Start;
                    action = "paused the world over the minigame";
                }
                // Taken all the same: there the game's Esc is the LE2 Bypass's interrupt, which loses the game, and the
                // player asked for a pause (PauseWorld logged the refusal).
                else
                    action = "taken: the world pause was refused over the minigame";
            }

            LEASI_INFO("{} pressed: mode {} ({}), movie mode {}, modes on {}, Pauser {} ({}), {} open movie(s), minigame {}, {}: {} (function {}, thread {})",
                       KeyName(key), mode, ModeName(mode), MovieModeText(movie), ActiveModesText(pc), PathOf(pauser), static_cast<void const*>(pauser), movies,
                       Minigame::Describe(board), PathOf(pc), action != nullptr ? action : "vanilla", PathOf(function), GetCurrentThreadId());
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
                if (k.Held != Hold::Ours)
                {
                    if (ConsoleOpen(function) || !Bink::OwnsKeys()) return false;
                    if (p->Event == IE_Released) LEASI_INFO("Esc released during a fullscreen movie, its press unseen here: taken, the movie's thread acts on Esc");
                }
                if (p->Event == IE_Released)
                {
                    SetHold(k, Hold::None, now);
                    k.ReleasedAt = now;
                }
                p->ReturnValue = 1;
                return true;
            }

            // A fresh press: a release we never saw (focus lost) ends the last one here, and so does the grace after one we
            // saw. Only a press we take sets the guard again: a vanilla press must keep its GuiKey and skips. The other keys'
            // guards stay (Start's press does the same), since their binds' late OnRelease may still come: a vanilla press
            // within 250 ms of another key's taken release, or of an Esc or Space the movie's thread acted on, loses its
            // GuiKey Escape or Start events. Clearing them all here would let that late OnRelease skip.
            SetHold(k, Hold::None, now);
            k.ReleasedAt = 0;
            if (ConsoleOpen(function))
            {
                LEASI_INFO("Esc pressed in {}: the console is open, vanilla", PathOf(function));
                return false;
            }
            // A press the movie's thread toggled on, reaching us once its movie is gone (it closed right after, or during a
            // stall of this thread): that press acted already, and the game must not act on it too (its pause menu, our
            // world pause). The atomic first: OwnsKeys takes Bink.cpp's lock.
            if (TakenLately(Bink::EscSeenDown(), now) && !Bink::OwnsKeys())
            {
                LEASI_INFO("Esc pressed: taken, the movie's thread acted on this press and its movie is gone (thread {})", GetCurrentThreadId());
                LEASI_FLUSH();
                SetHold(k, Hold::Ours, now);
                p->ReturnValue = 1;
                return true;
            }
            if (Press(console, function, Key::Esc) == nullptr) return false;
            SetHold(k, Hold::Ours, now);
            p->ReturnValue = 1;
            return true;
        }

        /// Start: its press does what Esc's does. During a fullscreen movie the engine's movie input takes the press (as it
        /// takes Esc's), and its first repeat or its release, whichever comes first, toggles the movie pause here instead.
        /// Returns true when the event is ours.
        bool TakeStart(UConsole* console, UFunction* function, UConsole_execInputKey_Parms* p, ULONGLONG now)
        {
            auto& k = G.Start;
            if (KeyRules::StartStale(k, now, G.StallEndedAt)) SetHold(k, Hold::None, now);
            k.LastEventAt = now;

            if (p->Event == IE_Pressed)
            {
                SetHold(k, Hold::None, now);
                k.ReleasedAt = 0;
                if (ConsoleOpen(function) || Press(console, function, Key::Start) == nullptr)
                {
                    SetHold(k, Hold::Vanilla, now);
                    return false;
                }
                SetHold(k, Hold::Ours, now);
                p->ReturnValue = 1;
                return true;
            }

            auto const released = p->Event == IE_Released;
            if (k.Held == Hold::Vanilla)
            {
                if (released) SetHold(k, Hold::None, now);
                return false;
            }
            if (k.Held != Hold::Ours)
            {
                // A press never seen here: the engine's movie input took it. Not a press held into the movie (Vanilla).
                if (ConsoleOpen(function) || !Bink::OwnsKeys()) return false;
                // Its first event here comes at most StartRepeatMs after it, so the movie Toggle would pause may have opened
                // after the press: the engine's movie input then skipped the movie before with it, and that one isn't paused
                // (Toggle logs it). Its events stay ours all the same, so they skip nothing. A resume of our movie pause is
                // always the press's.
                Bink::Toggle(released ? "Start released during a fullscreen movie, its press unseen here"
                                      : "Start held during a fullscreen movie, its press unseen here",
                             now - StartRepeatMs);
                if (!released) SetHold(k, Hold::Ours, now);
            }
            if (released)
            {
                SetHold(k, Hold::None, now);
                k.ReleasedAt = now;
            }
            p->ReturnValue = 1;
            return true;
        }

        void PublishOverlay(ABioPlayerController* pc);

        /// A fresh press of Y or F7, outside the open console: in our photo mode it leaves it, under our pause of a
        /// conversation or a cutscene it enters it. Anywhere else, during a fullscreen movie, and once the per-frame checks are
        /// off for the session, the key is the game's.
        char const* PressPhoto(UConsole* console, UFunction* function, Key key)
        {
            auto const pc = LocalController(console);
            // As in Press: a session Validate ends now was this press's to end, and the press must not open a new one.
            auto const wasPhoto = PhotoMode::On();
            Validate(pc, KeyName(key));
            auto const mode = ModeOf(pc);
            char const* action = nullptr;
            // Photo mode before the movie, as in Press.
            if (PhotoMode::On())
            {
                PhotoMode::Drop(KeyName(key), false);
                action = "left photo mode, the world stays paused";
            }
            else if (wasPhoto)
                action = "taken: photo mode had ended before this press";
            else if (Bink::OwnsKeys())
                action = nullptr;
            // Never over a minigame: photo mode lifts the pause around the GUI tick, which would run the minigame's clock (in
            // LE2 the handler's Update feeds it) and its board.
            else if (G.Kind == PauseKind::Scene && pc != nullptr && pc == G.Controller.Live() && (mode == ModeConversation || mode == ModeCinematic))
            {
                if (G.FrameFaults >= MaxFrameFaults)
                {
                    // Nothing would watch the session: its checks and its per-frame work run after BioHUD.PostRender.
                    LEASI_WARN("{} pressed: photo mode refused, the per-frame checks are off for the session", KeyName(key));
                }
                else
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
            }
            LEASI_INFO("{} pressed: mode {} ({}), world paused by us {}, {}: {} (function {}, thread {})", KeyName(key), mode, ModeName(mode), G.Paused(),
                       PathOf(pc), action != nullptr ? action : "vanilla", PathOf(function), GetCurrentThreadId());
            LEASI_FLUSH();
            return action;
        }

        /// Y and F7: a press we acted on takes its repeats and its release too. Returns true when the event is ours.
        bool TakePhotoKey(UConsole* console, UFunction* function, UConsole_execInputKey_Parms* p, KeyState& k, Key key, ULONGLONG now)
        {
            if (p->Event != IE_Pressed)
            {
                if (k.Held != Hold::Ours) return false;
                if (p->Event == IE_Released)
                {
                    SetHold(k, Hold::None, now);
                    k.ReleasedAt = now;
                }
                p->ReturnValue = 1;
                return true;
            }
            SetHold(k, Hold::None, now);
            k.ReleasedAt = 0;
            if (ConsoleOpen(function) || PressPhoto(console, function, key) == nullptr) return false;
            SetHold(k, Hold::Ours, now);
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
                SetHold(k, Hold::None, now);
                k.ReleasedAt = 0;
            }
            else if (k.Held == Hold::Ours)
            {
                if (released)
                {
                    SetHold(k, Hold::None, now);
                    k.ReleasedAt = now;
                }
                p->ReturnValue = 1;
                return true;
            }
            if (ConsoleOpen(function)) return false;

            char const* action = nullptr;
            auto const space = key == Key::Space;
            // The pause first, then Space's tick (KeyRules::MeetMoviePause says why).
            auto const movie = KeyRules::MeetMoviePause(
                space, [] { return Bink::PauseWanted(); }, [] { return Bink::OwnsKeys(); }, [now] { return TakenLately(Bink::SpaceSeenDown(), now); });
            if (movie == KeyRules::MoviePress::Paused)
            {
                Bink::RequestResume(pressed ? (space ? "Space pressed" : "A pressed")
                                            : (space ? "Space during a fullscreen movie, its press unseen here" : "A during a fullscreen movie, its press unseen here"));
                action = "resumed the movie";
            }
            else if (movie == KeyRules::MoviePress::Resumed)
                action = "taken: the movie's thread resumed the movie on it";
            else if (pressed && G.Paused() && !PhotoMode::On())
            {
                auto const pc = LocalController(console);
                // Validate resumed the world on this press: the game must not act on it too.
                if (Validate(pc, KeyName(key)))
                    action = "taken: the world resumed, what it held had left";
                else if (G.Paused())
                {
                    ResumeWorld(KeyName(key), true);
                    action = "resumed the world";
                }
            }
            if (action == nullptr) return false;
            LEASI_INFO("{} {}: {} (function {}, thread {})", KeyName(key), pressed ? "pressed" : released ? "released" : "repeated", action, PathOf(function),
                       GetCurrentThreadId());
            LEASI_FLUSH();
            if (!released) SetHold(k, Hold::Ours, now);
            else
                k.ReleasedAt = now;
            p->ReturnValue = 1;
            return true;
        }

        /// Returns true when the key is ours: the caller skips the original, which tells the viewport the key was handled.
        /// <paramref name="watched"/> is set to that key's state when it is one of ours, for a fault after its press acted.
        bool TakeKey(UConsole* console, UFunction* function, UConsole_execInputKey_Parms* p, KeyState*& watched)
        {
            static SFXName const Escape{ L"Escape", 0 };
            static SFXName const Start{ L"XboxTypeS_Start", 0 };
            static SFXName const PadY{ L"XboxTypeS_Y", 0 };
            static SFXName const F7{ L"F7", 0 };
            static SFXName const SpaceBar{ L"SpaceBar", 0 };
            static SFXName const PadA{ L"XboxTypeS_A", 0 };
            watched = p->Key == Escape ? &G.Esc : p->Key == Start ? &G.Start : p->Key == PadY ? &G.Y : p->Key == F7 ? &G.F7
                    : p->Key == SpaceBar ? &G.Space : p->Key == PadA ? &G.A : nullptr;
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

        /// Which of our keys are down (or were a moment ago), as KeyRules::Guards: 0 for none, whether or not Console.InputKey's
        /// TRUE kept them from PlayerInput. A press of Esc the movie's thread acted on counts too, seen here or not: Bink.cpp
        /// keeps its tick fresh while it is down.
        KeyRules::Guards KeyGuard(ULONGLONG now)
        {
            using KeyRules::Guard;
            using KeyRules::Bit;
            KeyRules::Guards guards = 0;
            if (TakenLately(Bink::EscSeenDown(), now) || KeyRules::Guarding(G.Esc, now)) guards |= Bit(Guard::Esc);
            if (TakenLately(Bink::SpaceSeenDown(), now) || KeyRules::Guarding(G.Space, now)) guards |= Bit(Guard::Space);
            if (KeyRules::Guarding(G.Start, now)) guards |= Bit(Guard::Start);
            if (KeyRules::Guarding(G.A, now)) guards |= Bit(Guard::A);
            return guards;
        }

        /// The GuiKey events the skip guard knows (game/LE<n>.hpp).
        constexpr KeyRules::GuiEvents GameGuiEvents{ GuiStart, GuiEscape, GuiStartRelease, GuiEscapeRelease, GuiA, GuiARelease, GuiSpace, GuiSpaceRelease };

        struct BoardCheck
        {
            bool Open = false;
        };

        void BoardCheckEntry(void* context)
        {
            auto const pc = LocalController(nullptr);
            static_cast<BoardCheck*>(context)->Open = ModeOf(pc) == ModeGui && Minigame::Open(pc);
        }

        /// A minigame's board is open: the GUI mode, with a minigame panel on the GUI manager. Read only when a resume key's
        /// own event is on its way to a game mode, under the crash guard: the skip guard runs straight from the ProcessEvent
        /// hook. A fault reads as no board, and the event goes to the game.
        bool OnMinigameBoard()
        {
            BoardCheck check;
            if (Guarded(BoardCheckEntry, &check)) return check.Open;
            LEASI_ERROR("access violation while looking for a minigame's board: the key's event passed");
            return false;
        }

        /// SkipMovie (PC_MovieSkip: Escape and SpaceBar), SkipCinematic (PC_CineSkip), the static binds' GuiKey Escape/Start,
        /// and on a minigame's board the GuiKey of the resume key itself (KeyRules::DropGuiKey), on an SFXGameMode. Returns
        /// true to drop the call.
        bool DropSkip(UObject* mode, UFunction* function, void* parms, bool guiKey)
        {
            // The movie pause before the guard's ticks: the movie's thread stores Esc's tick before it changes the pause, so
            // a pause asked by a press is seen here with that press's tick, and its SkipMovie doesn't resume it.
            auto const wanted = Bink::PauseWanted();
            auto const ours = KeyGuard(GetTickCount64());
            if (guiKey)
            {
                if (parms == nullptr || ours == 0) return false;
                auto const event = *static_cast<unsigned char const*>(parms);
                // The board only for an event that it alone would drop: the Escape and Start events go anywhere.
                auto const anywhere = KeyRules::DropGuiKey(event, ours, false, GameGuiEvents);
                auto const onBoard = !anywhere && KeyRules::DropGuiKey(event, ours, true, GameGuiEvents) && OnMinigameBoard();
                if (!anywhere && !onBoard) return false;
                LEASI_INFO("GuiKey {} on {} dropped: our {}{}", event, PathOf(mode), KeyRules::GuardsText(ours), onBoard ? ", on a minigame's board" : "");
                return true;
            }

            auto const name = function->Name.Instanced();
            auto const dropped = ours != 0;
            LEASI_INFO("{} on {}: {}{}", ToUtf8(std::wstring_view(name.Chars(), name.Length())), PathOf(mode), dropped ? "dropped, our " : "passed",
                       dropped ? KeyRules::GuardsText(ours) : std::string{});
            static SFXName const SkipMovie{ L"SkipMovie", 0 };
            if (!dropped && function->Name == SkipMovie && wanted) Bink::RequestResume("SkipMovie");
            return dropped;
        }

        /// One of game/LE<n>.hpp's MinigameSpaceExecs: what the space bar's bind runs on a minigame's board (LE2's
        /// DecryptSelect, which picks a code in the Hack).
        bool IsSpaceExec(SFXName const& name)
        {
            static std::vector<SFXName> const names = []
            {
                std::vector<SFXName> list;
                for (auto const exec : MinigameSpaceExecs) list.push_back(SFXName{ exec, 0 });
                return list;
            }();
            for (auto const& n : names)
            {
                if (n == name) return true;
            }
            return false;
        }

        /// Such an exec on a game mode, while the press of Space that resumed a minigame is down or was a moment ago: dropped
        /// as DropSkip drops Space's GuiKey (KeyRules::DropSpaceExec). Returns true to drop the call.
        bool DropSpaceExec(UObject* mode, UFunction* function)
        {
            auto const ours = KeyGuard(GetTickCount64());
            if (!KeyRules::DropSpaceExec(true, ours, KeyRules::Has(ours, KeyRules::Guard::Space) && OnMinigameBoard())) return false;
            LEASI_INFO("{} on {} dropped: our Space, on a minigame's board", NameText(function->Name), PathOf(mode));
            return true;
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
                // As below: the object read first, so a fault in it clears nothing.
                auto const path = PathOf(object);
                auto const cleared = MovieSound::Unlatch(movie);
                LEASI_INFO("movie event {}: {} (Deactivated), {}, thread {}", cleared ? "cleared" : "not cleared, the latch is not this movie's", movie, path,
                           GetCurrentThreadId());
                LEASI_FLUSH();
                return;
            }
            // The latch comes last, after all that reads the game's objects for the line: a fault on the way latches nothing.
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
            auto const path = PathOf(object);
            MovieSound::Latch(movie, action.EventId, action.Beside != nullptr);
            LEASI_INFO("movie event latched: {} id {} ({}), {}, thread {}", movie, action.EventId, check, path, GetCurrentThreadId());
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

        /// The banner's words (TextRules::BannerWords): a picked word that is not all Latin-1 is shown in the built-in text of
        /// the language, else of English, logged once per word, since the Canvas font has those 256 characters only.
        TextRules::BannerRow BannerWords(bool pad, bool photo)
        {
            using TextRules::Word;
            auto const row = TextRules::BannerWords(G.Texts, G.Language);
            for (auto const word : { Word::Title, Word::Resume, Word::Photo, pad ? Word::Start : Word::Esc, Word::Prompt })
            {
                if (word == Word::Photo && !photo) continue;
                auto const i = TextRules::IndexOf(word);
                auto const bit = 1u << i;
                if (row[i] == G.Texts[i].Text || (G.BannerStandInLogged & bit) != 0) continue;
                G.BannerStandInLogged |= bit;
                LEASI_WARN("banner: {} \"{}\" ({}) is not Latin-1, the Canvas font's range: built-in {} \"{}\" instead", TextRules::NameOf(word),
                           G.Texts[i].Text, TextRules::Describe(G.Texts[i]), TextRules::NameOf(TextRules::BannerStandIn(G.Language, word)), row[i]);
            }
            return row;
        }

        /// The title near the top centre, and the keys under it with their words: Start and Y when Start paused or the player
        /// uses a gamepad (PlayerInput's bUsingGamepad, which the game's own button prompts follow), else Esc and F7, the photo
        /// mode key only where photo mode opens. The HUD's own font, position and colour are put back.
        void DrawBanner(ABioHUD* hud)
        {
            // With the per-frame checks off our state isn't kept up: as the pause screen then, only while the world we
            // paused still exists and is paused.
            if (G.FrameFaults >= MaxFrameFaults && (G.World.Live() == nullptr || G.World.Object->Pauser == nullptr)) return;
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
            auto const input = hud->PlayerOwner != nullptr ? hud->PlayerOwner->PlayerInput : nullptr;
            auto const gamepad = G.PausedByStart || (input != nullptr && input->bUsingGamepad != 0);
            // Photo mode's key only where it opens: its hooks are in, the per-frame checks that watch it are on, and the pause
            // holds a scene (never a minigame: PressPhoto).
            auto const photo = G.Kind == PauseKind::Scene && PhotoMode::Answers() && G.FrameFaults < MaxFrameFaults;
            auto const words = BannerWords(gamepad, photo);
            auto const title = ToWide(words[TextRules::IndexOf(TextRules::Word::Title)]);
            auto const keys = ToWide(TextRules::BannerKeys(words, gamepad, photo));
            DrawCentered(canvas, title.c_str(), height * 0.08f, 2.5f * scale, Color(255, 255, 255, 235));
            DrawCentered(canvas, keys.c_str(), height * 0.08f + 48.0f * scale, 1.1f * scale, Color(200, 200, 200, 220));

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
            auto const hidden = G.Paused() && (photoOn || G.PhotoHideFrames > 0 || ConsoleIsOpen());
            auto const mode = ModeOf(pc);
            auto const photo = G.Kind == PauseKind::Scene && PhotoMode::Answers() && pc != nullptr && pc == G.Controller.Live() &&
                               (mode == ModeConversation || mode == ModeCinematic);
            Overlay::Publish(G.Paused() && !hidden, G.Paused() && !hidden && photo, hidden);
        }

        /// The one change of State::TextRead.
        void SetTextRead(TextRules::TextRead read) { G.TextRead = read; }

        /// What GetSimpleString answered: Called is false when the call did not run (the class has no such function).
        struct Answer
        {
            bool Called = false;
            std::string Text;
        };

        /// One string of the string table, in the game's language: GetSimpleString, a native that any game object of that
        /// class runs. It answers from the talk files of DLC mods first (a translation mod's), then the game's. An id no talk
        /// file defines gives "". The FString it returns is the game's allocation, left to it: a few bytes, once a session.
        Answer GameString(StringGame* game, int id)
        {
            static CachedFunction cache;
            GetSimpleStringParms parms;
            parms.StrRef = id;
            parms.bParse = 0;
            if (!Call(game, L"GetSimpleString", cache, parms)) return {};
            return { true, ToUtf8(std::wstring_view(parms.ReturnValue.Chars(), parms.ReturnValue.Length())) };
        }

        /// Once the world has a game object (the main menu's first HUD): the pause screen's words, each from our id, the game's
        /// string (game/LE<n>.hpp's GameWords) or the built-in text (TextRules::Resolve), for the banner and Overlay.cpp.
        /// Tried again on the next frames while GetSimpleString does not run, MaxTextTries times at most. CheckTexts logs a
        /// talk file that loads after this.
        void ReadTexts(ABioPlayerController* pc)
        {
            using namespace TextRules;
            auto const world = pc != nullptr ? pc->WorldInfo : nullptr;
            auto const game = world != nullptr ? world->Game : nullptr;
            if (game == nullptr || !game->IsA(StringGame::StaticClass())) return;
            // Counted first: a fault in the calls costs a try, not a frame's checks each frame.
            G.TextTries++;
            auto const sp = static_cast<StringGame*>(game);
            std::array<Answer, WordCount> ours;
            std::array<Answer, WordCount> theirs;
            auto called = true;
            for (auto const word : Words)
            {
                auto const i = IndexOf(word);
                ours[i] = GameString(sp, IdOf(word));
                theirs[i] = GameWords[i] != 0 ? GameString(sp, GameWords[i]) : Answer{ true, {} };
                called = ours[i].Called && theirs[i].Called;
                if (!called) break;
            }
            SetTextRead(AfterTry(called, G.TextTries));
            if (G.TextRead == TextRead::Waiting) return;
            if (G.TextRead == TextRead::Off)
            {
                LEASI_WARN("texts: GetSimpleString did not run in {} tries: the built-in {} text for the session", MaxTextTries, NameOf(G.Language));
                return;
            }
            std::string line;
            for (auto const word : Words)
            {
                auto const i = IndexOf(word);
                G.Texts[i] = Resolve(word, ours[i].Text, theirs[i].Text, GameWords[i], G.Language);
                if (!ours[i].Text.empty() && G.Texts[i].From != Source::Ours)
                    LEASI_WARN("texts: {} {} \"{}\" skipped: a pattern holds %1 and %2 once each", NameOf(word), IdOf(word), ours[i].Text);
                line += std::format("{}{} \"{}\" {}", line.empty() ? "" : "; ", NameOf(word), G.Texts[i].Text, Describe(G.Texts[i]));
            }
            LEASI_INFO("texts: {}", line);
            Overlay::UseTexts(G.Texts);
        }

        void ReadTextsEntry(void* context) { ReadTexts(static_cast<ABioPlayerController*>(context)); }

        /// At the first pause after the read, once a session: our ids asked again. One that answers now with a text the read
        /// did not take means a talk file loaded after the read, so a warning names it. The words stay as read, and the
        /// fonts stay cut to them, for the session. Checked first, so a fault ends it.
        void CheckTexts(ABioPlayerController* pc)
        {
            using namespace TextRules;
            SetTextRead(TextRead::Checked);
            auto const world = pc != nullptr ? pc->WorldInfo : nullptr;
            auto const game = world != nullptr ? world->Game : nullptr;
            if (game == nullptr || !game->IsA(StringGame::StaticClass())) return;
            auto const sp = static_cast<StringGame*>(game);
            std::string late;
            for (auto const word : Words)
            {
                auto const i = IndexOf(word);
                auto const now = GameString(sp, IdOf(word));
                if (!now.Called)
                {
                    LEASI_WARN("texts: GetSimpleString did not run at the first pause: not asked again");
                    return;
                }
                if (!AnswersLate(word, G.Texts[i], now.Text)) continue;
                late += std::format("{}{} {} \"{}\" (the read took {} \"{}\")", late.empty() ? "" : "; ", NameOf(word), IdOf(word), now.Text,
                                    Describe(G.Texts[i]), G.Texts[i].Text);
            }
            if (late.empty())
                LEASI_INFO("texts: asked again at the first pause: no new answer");
            else
                LEASI_WARN("texts: asked again at the first pause, our ids answer otherwise: {}. A talk file loaded after the read: the words stay as read "
                           "until the game restarts",
                           late);
        }

        void CheckTextsEntry(void* context) { CheckTexts(static_cast<ABioPlayerController*>(context)); }

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

        /// Frames on which ReadGameIcons may try: the textures load with the game, before the first HUD, but the renderer may
        /// make their views some frames later. About a second, once a session; each try searches the object list twice.
        constexpr unsigned MaxGameIconTries = 60;

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
            // Cut to the words picked, and to the built-in text the screen falls back to.
            auto const letters = TextRules::ScreenLetters(G.Texts, G.Language);
            Overlay::UseGameFonts({ light, PauseFonts.LightFont }, { bold, PauseFonts.BoldFont }, letters);
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
            if (G.TextRead == TextRules::TextRead::Waiting && !Guarded(ReadTextsEntry, pc))
            {
                SetTextRead(TextRules::TextRead::Off);
                LEASI_ERROR("access violation while reading the string table: the built-in {} text for the session", TextRules::NameOf(G.Language));
            }
            if (G.GameIconTries < MaxGameIconTries && !Guarded(ReadGameIconsEntry, nullptr))
            {
                G.GameIconTries = MaxGameIconTries;
                LEASI_ERROR("access violation while reading the game's gamepad icons: the pause screen draws its own");
            }
            // At the first pause after the read: a talk file that loaded after it is logged (CheckTexts).
            if (G.TextRead == TextRules::TextRead::Done && G.Paused() && !Guarded(CheckTextsEntry, pc))
            {
                SetTextRead(TextRules::TextRead::Checked);
                LEASI_ERROR("access violation while asking the string table again: the words stay as read");
            }
            // After the words: the fonts are cut to their letters.
            if (G.TextRead != TextRules::TextRead::Waiting && G.GameFontTries < MaxGameFontTries && !Guarded(ReadGameFontsEntry, pc))
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
            /// The key's state, when it is one of the keys we watch.
            KeyState* Watched;
        };

        void TakeKeyEntry(void* context)
        {
            auto const c = static_cast<KeyCall*>(context);
            c->Taken = TakeKey(c->Console, c->Function, c->Parms, c->Watched);
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

        /// For KeysElsewhere: the Steam overlay or the console is open now.
        void NoteKeysElsewhereEntry(void*)
        {
            auto const pc = LocalController(nullptr);
            Elsewhere.store((pc != nullptr && pc->bIsExternalUIOpen) || ConsoleIsOpen(), std::memory_order_relaxed);
        }

        void PublishBareEntry(void*)
        {
            auto const photoOn = PhotoMode::On();
            Overlay::Publish(G.Paused() && !photoOn && G.World.Live() != nullptr && G.World.Object->Pauser != nullptr, false, photoOn);
        }

        /// After an access violation in the per-frame checks: our state may be what faulted, so it ends here, photo mode
        /// first, and a world we paused is resumed (under its own guard) rather than left paused with nothing of ours to
        /// resume it.
        void DropState()
        {
            PhotoMode::Drop("fault", false);
            if (G.Paused() && !Guarded(ResumeWorldEntry, nullptr))
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
            // The game thread's heartbeat for Bink.cpp's movie reports, written only when the tick count moved, and its stalls
            // for Start and TakenLately. At each such tick too, what the movie's thread can't read: the Steam overlay and the
            // console (a fullscreen movie draws no HUD, so the per-frame checks don't run under it).
            if (GetCurrentThreadId() == GameThreadId.load(std::memory_order_relaxed))
            {
                auto const now = GetTickCount64();
                auto const last = GameThreadTick.load(std::memory_order_relaxed);
                if (last != now)
                {
                    if (last != 0 && now - last > KeyRules::GameStallMs)
                    {
                        G.StallStartedAt = last;
                        G.StallEndedAt = now;
                    }
                    GameThreadTick.store(now, std::memory_order_relaxed);
                    if (!G.ElsewhereOff && !Guarded(NoteKeysElsewhereEntry, nullptr))
                    {
                        G.ElsewhereOff = true;
                        Elsewhere.store(false, std::memory_order_relaxed);
                        LEASI_ERROR("access violation while reading the Steam overlay's and the console's state: off for the session, the movie's "
                                    "thread reads Esc and Space there too");
                    }
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
                    KeyCall call{ static_cast<UConsole*>(Context), Function, static_cast<UConsole_execInputKey_Parms*>(Parms), false, nullptr };
                    auto const wasPaused = G.Paused();
                    if (!Guarded(TakeKeyEntry, &call))
                    {
                        // A fault after the press paused or resumed the world: that press acted, so it stays ours with its
                        // repeats and its release (and the skip guard), or the game would act on it too.
                        call.Taken = G.Paused() != wasPaused;
                        LEASI_ERROR("access violation while reading a key: {}{}", call.Taken ? "taken, its press paused or resumed the world" : "passed to the game",
                                    PhotoMode::On() ? ", photo mode left" : "");
                        if (call.Taken)
                        {
                            call.Parms->ReturnValue = 1;
                            if (call.Watched != nullptr) SetHold(*call.Watched, Hold::Ours, GetTickCount64());
                        }
                        PhotoMode::Drop("fault while reading a key", false);
                    }
                    if (call.Taken) return;
                }
            }
            else if (name == SkipMovie || name == SkipCinematic || name == GuiKey)
            {
                if (Context->IsA(USFXGameModeBase::StaticClass()) && DropSkip(Context, Function, Parms, name == GuiKey)) return;
            }
            else if (!MinigameSpaceExecs.empty() && IsSpaceExec(name))
            {
                // LE2's SFXGameModeGUI.DecryptSelect: the Hack's space bar, which picks the code under the cursor.
                if (Context->IsA(USFXGameModeBase::StaticClass()) && DropSpaceExec(Context, Function)) return;
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
                if (!Overlay::Drawing() && G.Paused() && !PhotoMode::On() && !G.BannerOff && !Guarded(DrawBannerEntry, Context))
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

    bool InstallHooks(::LESDK::Initializer& Init)
    {
        // Before the hook: the banner may draw before the string table is read.
        G.Language = TextRules::LanguageOfCode(LaunchLanguageCode());
        G.Texts = TextRules::BuiltInPicks(G.Language);
        auto const target = Init.ResolveTyped<t_UObject_ProcessEvent>(BUILTIN_PROCESSEVENT_RVA);
        CHECK_RESOLVED(target);
        auto const orig = static_cast<t_UObject_ProcessEvent*>(Init.InstallHook("UObject::ProcessEvent", target, ProcessEvent_hook));
        // The loader names a hook after the plugin name compiled into the file, so a copy of this file under another name
        // asks for the same name, and the loader refuses it. The copy that loaded first keeps working.
        if (orig == nullptr)
        {
            LEASI_ERROR("the loader refused the ProcessEvent hook: is a second copy of " SDK_TARGET_NAME_A ASI_NAME_NO_SPACE_A
                        " in the ASI folder under another name? Keep only one. This copy stays off");
            LEASI_FLUSH();
            return false;
        }
        ProcessEvent_orig.store(orig, std::memory_order_release);
        LEASI_INFO("ProcessEvent hooked");

        PhotoMode::InstallHooks(Init);
        return true;
    }

    DWORD GameThread() { return GameThreadId.load(std::memory_order_relaxed); }

    ULONGLONG GameThreadSeen() { return GameThreadTick.load(std::memory_order_relaxed); }

    ULONGLONG HudSeen() { return HudTick.load(std::memory_order_relaxed); }

    bool KeysElsewhere()
    {
        if (!Elsewhere.load(std::memory_order_relaxed)) return false;
        // Only while the game thread ticks, so the flag is a tick old at most. A silent one (a load, a wait for this very
        // movie) may have read it before the overlay or the console closed. Kept, it would refuse every Esc and Space while
        // nothing else could resume the movie. Keys typed into the overlay then reach the movie, as without this check.
        auto const now = GetTickCount64();
        auto const beat = GameThreadTick.load(std::memory_order_relaxed);
        return beat != 0 && (now > beat ? now - beat : 0) <= KeyRules::GameStallMs;
    }

    bool TakenLately(ULONGLONG tick, ULONGLONG now)
    {
        // The heartbeat and the stall are written on this thread only (ProcessEvent_hook), so reading them ahead of the
        // tests that may not need them reads the same values.
        auto const lately = KeyRules::TakenLately(tick, now, GameThreadTick.load(std::memory_order_relaxed), { G.StallStartedAt, G.StallEndedAt });
        if (!lately.Taken || lately.StallStart == 0) return lately.Taken;
        // Once a stall: each skip the guard drops after it asks again.
        static ULONGLONG logged = 0;
        if (logged != lately.StallStart)
        {
            logged = lately.StallStart;
            LEASI_INFO("a press the movie's thread acted on {} ms ago reaches the game thread after its stall of {} ms: still ours", now - tick,
                       lately.StallLength);
        }
        return true;
    }
}
