#include "MelePause/Pause.hpp"
#include "Common/Objects.hpp"

#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

// Esc pauses a conversation or an in-engine cutscene with the world pause the "pause" console command sets
// (PlayerController.Pause -> ServerPause -> GameInfo.SetPause: WorldInfo.Pauser), which stops the picture, the voice-over,
// the music and the lips together and resumes them in sync. Not WorldInfo.bPlayersOnly (the pause menu's and photo mode's):
// the conversation's clock and the music keep running under it. A Bink movie does not stop under either: Bink.cpp pauses it,
// and reads Esc itself on the movie's thread (a fullscreen movie's Esc press never reaches Console.InputKey); here Esc is only
// kept from the game while a fullscreen movie plays. Anywhere else Esc keeps its vanilla behavior. Everything here runs on the
// game thread, inside the ProcessEvent calls it reacts to.
namespace MelePause
{
    namespace
    {
        // ! Game facts
        // ========================================

        // SFXGameModeManager.EGameModes (SFXGame_classes.hpp).
        constexpr unsigned char ModeConversation = 5;
        constexpr unsigned char ModeCinematic = 6;
        constexpr unsigned char ModeMovie = 8;
        constexpr unsigned char NoMode = 0xFF;

        // Engine.Interaction.EInputEvent. IE_Repeat (2) and the rest only matter as "not a press".
        constexpr unsigned char IE_Pressed = 0;
        constexpr unsigned char IE_Released = 1;

        // BioGuiEvents the always-on Escape bind sends to Scaleform: Start and Escape, pressed and released.
        constexpr unsigned char GuiStart = 21;
        constexpr unsigned char GuiEscape = 24;
        constexpr unsigned char GuiStartRelease = 43;
        constexpr unsigned char GuiEscapeRelease = 46;

        /// After a press we took, the skips Esc is bound to are dropped until its release, for 2 s at most (a release lost to
        /// a focus change must not keep Space's SkipMovie away)...
        constexpr ULONGLONG GuardMs = 2000;
        /// ...and a moment after it: the OnRelease half of a bind may run after Console.InputKey has seen the release.
        constexpr ULONGLONG ReleaseGraceMs = 250;

        /// A movie pause is lifted once the HUD has drawn this long without a gap: no fullscreen movie covers the screen.
        constexpr ULONGLONG HudBackMs = 1000;

        char const* ModeName(unsigned char mode)
        {
            static char const* const Names[] = { "Default", "Vehicle", "PowerWheel", "WeaponWheel", "Command", "Conversation",
                                                 "Cinematic", "GUI", "Movie", "Galaxy", "Minigame", "Photo", "FlyCam" };
            return static_cast<size_t>(mode) < std::size(Names) ? Names[mode] : "none";
        }

        // ! State
        // ========================================

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

            /// A press of Esc we acted on: its repeats and its release are ours too.
            bool EscHeld = false;
            ULONGLONG EscPressedAt = 0;
            ULONGLONG EscReleasedAt = 0;

            /// Turned off for the session by an access violation while drawing, or a missing Canvas function.
            bool BannerOff = false;
            /// Access violations in the per-frame checks: each drops our pause state, and the third turns them off for the
            /// session (Esc still checks the state), rather than fault and log at frame rate.
            unsigned FrameFaults = 0;

            /// The first BioHUD.PostRender after a gap longer than HudSilentMs: since then the HUD has drawn every frame.
            ULONGLONG HudDrawsSince = 0;

            /// Diagnostics, once each: the local player, and per movie whether input and the HUD still run during it.
            bool PlayerChecked = false;
            unsigned InputMovieSerial = 0;
            unsigned HudMovieSerial = 0;
        };
        State G;

        std::atomic<DWORD> GameThreadId{ 0 };
        std::atomic<ULONGLONG> GameThreadTick{ 0 };
        /// The end of the latest BioHUD.PostRender: Bink.cpp's movie threads read it.
        std::atomic<ULONGLONG> HudTick{ 0 };

        /// Set while we call into the game (Pause, Canvas): those calls only pass through our hook.
        thread_local bool InOwnCall = false;

        // ! Helpers
        // ========================================

        std::string ToUtf8(std::wstring_view text)
        {
            if (text.empty()) return {};
            auto const size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
            return out;
        }

        std::string PathOf(UObject const* object)
        {
            if (object == nullptr) return "None";
            auto const path = object->GetFullPath();
            return ToUtf8(std::wstring_view(path.Chars(), path.Length()));
        }

        /// Still the object the index pointed at: not destroyed, not replaced by a map change.
        bool Alive(UObject* object, int index)
        {
            auto& objects = *UObject::GObjObjects;
            return object != nullptr && index >= 0 && static_cast<UINT>(index) < objects.Count() && objects.GetData()[index] == object
                && !Common::IsDestroyed(object);
        }

        /// The most derived function of that name: what a console command would run.
        UFunction* FindFunction(UClass* cls, SFXName const& name)
        {
            for (UStruct* s = cls; s != nullptr; s = static_cast<UStruct*>(s->SuperField))
            {
                for (UField* f = s->Children; f != nullptr; f = f->Next)
                {
                    if (f->Name == name && f->IsA(UFunction::StaticClass())) return static_cast<UFunction*>(f);
                }
            }
            return nullptr;
        }

        /// ProcessEvent refuses a function with an opcode number (iNative): zeroed for the call, it runs the native like any
        /// other function, as LEX Interop and the bridge do. Opcode and flags are put back as they were; the SDK's generated
        /// wrappers instead leave every flag bit set. No C++ object may live in this frame.
        void CallGame(UObject* object, UFunction* function, void* parms)
        {
            auto const native = function->iNative;
            auto const flags = function->FunctionFlags;
            auto const outer = InOwnCall;
            function->iNative = 0;
            InOwnCall = true;
            __try
            {
                object->ProcessEvent(function, parms, nullptr);
            }
            __finally
            {
                InOwnCall = outer;
                function->iNative = native;
                function->FunctionFlags = flags;
            }
        }

        /// An access violation in our code (a pointer the checks missed) fails that call, not the game. No C++ object may
        /// live in this frame: __try cannot unwind one.
        bool Guarded(void (*work)(void*), void* context)
        {
            __try
            {
                work(context);
                return true;
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                return false;
            }
        }

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

        unsigned char ModeOf(ABioPlayerController* pc)
        {
            auto const manager = pc != nullptr ? pc->GameModeManager : nullptr;
            return manager != nullptr ? manager->CurrentMode : NoMode;
        }

        /// The movie mode: GameModes[8] is SFXGameModeMovie, and the array is searched in case it isn't. For the logs only:
        /// whether a movie is paused doesn't depend on it.
        USFXGameModeMovie* MovieMode(ABioPlayerController* pc)
        {
            auto const manager = pc != nullptr ? pc->GameModeManager : nullptr;
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

        // ! World pause
        // ========================================

        /// PlayerController.Pause, the "pause" console command: ServerPause toggles SetPause(!IsPaused()).
        bool CallPause(ABioPlayerController* pc)
        {
            static SFXName const PauseName{ L"Pause", 0 };
            static UClass* seenClass = nullptr;
            static UFunction* pause = nullptr;
            if (pc->Class != seenClass)
            {
                seenClass = pc->Class;
                pause = FindFunction(pc->Class, PauseName);
                if (pause == nullptr) LEASI_ERROR("no Pause function in {}", PathOf(pc->Class));
            }
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
        }

        double HeldFor(ULONGLONG since) { return static_cast<double>(GetTickCount64() - since) / 1000.0; }

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
            return true;
        }

        /// Toggles the pause back through the controller that paused, only while the world is still paused: Pause on an
        /// unpaused world would pause it again. Our state ends before the call, so a fault inside it can't leave it armed.
        void ResumeWorld(char const* why)
        {
            if (!G.WorldPaused) return;
            auto const mode = G.PausedMode;
            auto const since = G.PausedAt;
            auto const world = G.World;
            auto const controller = G.Controller;
            auto const worldAlive = Alive(G.World, G.WorldIndex);
            auto const controllerAlive = Alive(G.Controller, G.ControllerIndex);
            EndWorldPause();
            if (!worldAlive || world->Pauser == nullptr)
            {
                LEASI_INFO("world resume ({}): nothing to do, the world is gone or no longer paused", why);
                return;
            }
            if (!controllerAlive || controller->WorldInfo != world)
            {
                LEASI_WARN("world resume ({}): the controller that paused is gone, the world stays paused by {}", why, PathOf(world->Pauser));
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
        }

        /// Every frame and every Esc: our world pause ends when someone else ended it (Pauser None: the game, a load, the
        /// Steam overlay closing clears every CanUnpause that agrees), when its WorldInfo is gone, or when the mode left
        /// Conversation and Cinematic under it. The movie pause isn't checked here: Bink.cpp ends it (close, Esc, its movie
        /// gone silent), and SkipMovie and the HUD drawing again do.
        void Validate(ABioPlayerController* pc, char const* where)
        {
            if (G.WorldPaused)
            {
                if (!Alive(G.World, G.WorldIndex))
                {
                    LEASI_INFO("world pause ended ({}): the WorldInfo we paused is gone (map change or load), thread {}", where, GetCurrentThreadId());
                    EndWorldPause();
                    LEASI_FLUSH();
                }
                else if (G.World->Pauser == nullptr)
                {
                    LEASI_INFO("world pause ended ({}) after {:.1f} s: WorldInfo.Pauser became None without us, thread {}", where, HeldFor(G.PausedAt),
                               GetCurrentThreadId());
                    EndWorldPause();
                    LEASI_FLUSH();
                }
                else if (pc != nullptr && pc->WorldInfo == G.World)
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

        // ! Esc
        // ========================================

        /// Console.Typing and Console.Open have their own InputKey, where Esc closes the console: vanilla.
        bool ConsoleOpen(UFunction* function)
        {
            static SFXName const Typing{ L"Typing", 0 };
            static SFXName const Open{ L"Open", 0 };
            auto const outer = function->Outer;
            return outer != nullptr && !outer->IsA(UClass::StaticClass()) && (outer->Name == Typing || outer->Name == Open);
        }

        /// Once per movie: does the game thread still run input while a movie plays, in which game mode, and does Esc belong
        /// to the movie then?
        void NoteInputDuringMovie(UConsole* console, UConsole_execInputKey_Parms const* p)
        {
            auto const serial = Bink::MovieSerial();
            if (Bink::OpenMovies() == 0 || serial == G.InputMovieSerial) return;
            G.InputMovieSerial = serial;
            auto const key = p->Key.Instanced();
            auto const pc = LocalController(console);
            auto const mode = ModeOf(pc);
            LEASI_INFO("Console.InputKey runs while movie #{} is open: key {} event {}, mode {} ({}), movie mode {}, Esc {}, thread {}", serial,
                       ToUtf8(std::wstring_view(key.Chars(), key.Length())), p->Event, mode, ModeName(mode), MovieModeText(MovieMode(pc)),
                       Bink::OwnsEsc() ? "the movie's" : "vanilla", GetCurrentThreadId());
        }

        /// Returns true when the key is ours: the caller skips the original, which tells the viewport the key was handled.
        bool TakeKey(UConsole* console, UFunction* function, UConsole_execInputKey_Parms* p)
        {
            static SFXName const Escape{ L"Escape", 0 };
            GameThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
            NoteInputDuringMovie(console, p);
            if (!(p->Key == Escape)) return false;

            auto const now = GetTickCount64();
            if (p->Event != IE_Pressed)
            {
                // The repeats and the release of a press we took. During a fullscreen movie (or our pause of one), every Esc
                // event, its press seen here or not: the movie's thread reads the key itself and toggles the pause there, and
                // the travel movie's press never comes here (its release did, alone). Not in an open console: it closes on
                // Esc's release, and the engine's movie input takes the press.
                if (!G.EscHeld)
                {
                    if (ConsoleOpen(function) || !Bink::OwnsEsc()) return false;
                    if (p->Event == IE_Released) LEASI_INFO("Esc released during a fullscreen movie, its press unseen here: taken, the movie's thread acts on Esc");
                }
                if (p->Event == IE_Released)
                {
                    G.EscHeld = false;
                    G.EscReleasedAt = now;
                }
                p->ReturnValue = 1;
                return true;
            }

            // A fresh press: a release we never saw (focus lost) ends the last one here, and so does the grace after one we
            // saw. Only a press we take sets the guard again: a vanilla press must keep its GuiKey and skips.
            G.EscHeld = false;
            G.EscReleasedAt = 0;
            if (ConsoleOpen(function))
            {
                LEASI_INFO("Esc pressed in {}: the console is open, vanilla", PathOf(function));
                return false;
            }

            auto const pc = LocalController(console);
            Validate(pc, "Esc");
            auto const mode = ModeOf(pc);
            auto const world = pc != nullptr ? pc->WorldInfo : nullptr;
            auto const pauser = world != nullptr ? world->Pauser : nullptr;
            auto const movies = Bink::OpenMovies();
            auto const movie = MovieMode(pc);

            char const* action = nullptr;
            if (Bink::OwnsEsc())
            {
                // Not toggled here: the movie's thread saw this press too and acts on it, so one press is one toggle.
                action = "taken for the fullscreen movie, its thread toggles the movie pause";
            }
            else if (G.WorldPaused)
            {
                ResumeWorld("Esc");
                action = "resumed the world";
            }
            else if ((mode == ModeConversation || mode == ModeCinematic) && world != nullptr && pauser == nullptr)
            {
                if (PauseWorld(pc, mode)) action = "paused the world";
            }

            LEASI_INFO("Esc pressed: mode {} ({}), movie mode {}, Pauser {} ({}), {} open movie(s), {}: {} (function {}, thread {})", mode,
                       ModeName(mode), MovieModeText(movie), PathOf(pauser), static_cast<void const*>(pauser), movies, PathOf(pc),
                       action != nullptr ? action : "vanilla", PathOf(function), GetCurrentThreadId());
            LEASI_FLUSH();
            if (action == nullptr) return false;

            G.EscHeld = true;
            G.EscPressedAt = now;
            G.EscReleasedAt = 0;
            p->ReturnValue = 1;
            return true;
        }

        /// While our Esc is down (and a moment after), whether or not Console.InputKey's TRUE kept the key from PlayerInput. A
        /// press the movie's thread acted on counts too, seen here or not: Bink.cpp keeps its tick fresh while it is down.
        bool EscGuard(ULONGLONG now)
        {
            auto const movieEsc = Bink::EscSeenDown();
            if (movieEsc != 0 && (movieEsc >= now || now - movieEsc < ReleaseGraceMs)) return true;
            if (G.EscHeld) return now - G.EscPressedAt < GuardMs;
            return G.EscReleasedAt != 0 && now - G.EscReleasedAt < ReleaseGraceMs;
        }

        /// SkipMovie (PC_MovieSkip: Escape and SpaceBar), SkipCinematic (PC_CineSkip) and the static bind's GuiKey Escape/Start,
        /// on an SFXGameMode. Returns true to drop the call.
        bool DropSkip(UObject* mode, UFunction* function, void* parms, bool guiKey)
        {
            auto const now = GetTickCount64();
            if (guiKey)
            {
                auto const event = parms != nullptr ? *static_cast<unsigned char const*>(parms) : 0xFF;
                if (event != GuiStart && event != GuiEscape && event != GuiStartRelease && event != GuiEscapeRelease) return false;
                if (!EscGuard(now)) return false;
                LEASI_INFO("GuiKey {} on {} dropped: our Esc", event, PathOf(mode));
                return true;
            }

            auto const drop = EscGuard(now);
            auto const name = function->Name.Instanced();
            LEASI_INFO("{} on {}: {}", ToUtf8(std::wstring_view(name.Chars(), name.Length())), PathOf(mode),
                       drop ? "dropped, our Esc" : "passed");
            static SFXName const SkipMovie{ L"SkipMovie", 0 };
            if (!drop && function->Name == SkipMovie && Bink::PauseWanted()) Bink::RequestResume("SkipMovie");
            return drop;
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

        /// "PAUSE" near the top centre, and how to resume under it. The HUD's own font, position and colour are put back.
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
            DrawCentered(canvas, L"Esc: resume", height * 0.08f + 48.0f * scale, 1.1f * scale, Color(200, 200, 200, 220));

            canvas->Font = savedFont;
            canvas->DrawColor = savedColor;
            canvas->CurX = savedX;
            canvas->CurY = savedY;
            canvas->bCenter = savedCenter;
        }

        // ! Per frame
        // ========================================

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

        constexpr unsigned MaxFrameFaults = 3;

        /// After an access violation in the per-frame checks: our state may be what faulted, so it ends here, and a world we
        /// paused is resumed (under its own guard) rather than left paused with nothing of ours to resume it.
        void DropState()
        {
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
            // The game thread's heartbeat for Bink.cpp's movie reports, written only when the tick count moved.
            if (GetCurrentThreadId() == GameThreadId.load(std::memory_order_relaxed))
            {
                auto const now = GetTickCount64();
                if (GameThreadTick.load(std::memory_order_relaxed) != now) GameThreadTick.store(now, std::memory_order_relaxed);
            }
            if (InOwnCall || Context == nullptr || Function == nullptr)
            {
                orig(Context, Function, Parms, Result);
                return;
            }

            static SFXName const InputKey{ L"InputKey", 0 };
            static SFXName const PostRender{ L"PostRender", 0 };
            static SFXName const SkipMovie{ L"SkipMovie", 0 };
            static SFXName const SkipCinematic{ L"SkipCinematic", 0 };
            static SFXName const GuiKey{ L"GuiKey", 0 };

            auto const& name = Function->Name;
            if (name == InputKey)
            {
                if (Parms != nullptr && Context->IsA(UConsole::StaticClass()))
                {
                    KeyCall call{ static_cast<UConsole*>(Context), Function, static_cast<UConsole_execInputKey_Parms*>(Parms), false };
                    if (!Guarded(TakeKeyEntry, &call))
                    {
                        LEASI_ERROR("access violation while reading a key: passed to the game");
                        call.Taken = false;
                    }
                    if (call.Taken) return;
                }
            }
            else if (name == SkipMovie || name == SkipCinematic || name == GuiKey)
            {
                if (Context->IsA(USFXGameModeBase::StaticClass()) && DropSkip(Context, Function, Parms, name == GuiKey)) return;
            }

            orig(Context, Function, Parms, Result);

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
                if (G.WorldPaused && !G.BannerOff && !Guarded(DrawBannerEntry, Context))
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
    }

    DWORD GameThread() { return GameThreadId.load(std::memory_order_relaxed); }

    ULONGLONG GameThreadSeen() { return GameThreadTick.load(std::memory_order_relaxed); }

    ULONGLONG HudSeen() { return HudTick.load(std::memory_order_relaxed); }
}
