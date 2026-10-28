#include "MelePause/Pause.hpp"
#include "Common/Objects.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
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
// vanilla behavior. Everything here runs on the game thread, inside the ProcessEvent calls it reacts to.
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
        constexpr unsigned char ModePhoto = 11;
        constexpr unsigned char NoMode = 0xFF;

        /// SFXGameModeBase.EModePriority.ModePriority_Menus: above Conversation (2) and Cinematic (Base, 0).
        constexpr unsigned char PriorityMenus = 3;

        // Engine.Interaction.EInputEvent. IE_Repeat (2) and the rest only matter as "not a press".
        constexpr unsigned char IE_Pressed = 0;
        constexpr unsigned char IE_Released = 1;

        // BioGuiEvents the always-on Escape and Start binds send to Scaleform: Start and Escape, pressed and released.
        constexpr unsigned char GuiStart = 21;
        constexpr unsigned char GuiEscape = 24;
        constexpr unsigned char GuiStartRelease = 43;
        constexpr unsigned char GuiEscapeRelease = 46;

        /// After a press we took, the skips Esc is bound to are dropped until its release, for 2 s at most (a release lost to
        /// a focus change must not keep Space's SkipMovie away)...
        constexpr ULONGLONG GuardMs = 2000;
        /// ...and a moment after it: the OnRelease half of a bind may run after Console.InputKey has seen the release.
        constexpr ULONGLONG ReleaseGraceMs = 250;

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

        char const* ModeName(unsigned char mode)
        {
            static char const* const Names[] = { "Default", "Vehicle", "PowerWheel", "WeaponWheel", "Command", "Conversation",
                                                 "Cinematic", "GUI", "Movie", "Galaxy", "Minigame", "Photo", "FlyCam" };
            return static_cast<size_t>(mode) < std::size(Names) ? Names[mode] : "none";
        }

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

        /// A function of an object's class, looked up again when the class changes.
        struct CachedFunction
        {
            UClass* Class = nullptr;
            UFunction* Function = nullptr;
        };

        UFunction* FunctionOf(UObject* object, wchar_t const* name, CachedFunction& cache)
        {
            if (object->Class != cache.Class)
            {
                cache.Class = object->Class;
                cache.Function = FindFunction(object->Class, SFXName{ name, 0 });
                if (cache.Function == nullptr) LEASI_ERROR("no {} function in {}", ToUtf8(name), PathOf(object->Class));
            }
            return cache.Function;
        }

        std::string NameText(SFXName const& name)
        {
            auto const text = name.Instanced();
            return ToUtf8(std::wstring_view(text.Chars(), text.Length()));
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

        // ! Photo mode
        // ========================================

        // The game's own photo mode, over our world pause. Vanilla opens it only from the pause menu, which the game refuses in
        // a conversation or a cutscene; its real entry, MassEffectGuiManager.ShowPhotoMode, has no gate. The world doesn't tick
        // under the pause: our controller is made to (bAlwaysTick: PlayerInput, then PlayerPhotoMode.PlayerMove), and the
        // camera is updated from here once a frame. What stands in the way is set aside for the session and given back at its
        // end: photo mode's priority (below Conversation's), the conversation panel (it eats the sticks, and shows the wheel
        // and the subtitle), the ignore counters, the controller's timers, a cutscene's director camera. The exit is the
        // game's own (HidePhotoMode), then ours.

        /// A game object we changed, with its object index: checked again before each write back.
        template <class T>
        struct Kept
        {
            T* Object = nullptr;
            int Index = -1;

            void Set(T* object)
            {
                Object = object;
                Index = object != nullptr ? object->ObjectInternalInteger : -1;
            }

            T* Live() const { return Alive(Object, Index) ? Object : nullptr; }
        };

        /// What the session changed, one bit per step: the exit gives back only those, and clears each bit once done.
        enum : unsigned
        {
            DonePriority = 1u << 0,
            DoneShown = 1u << 1,      // ShowPhotoMode ran: EnableMode(Photo) and the photo panel
            DonePanel = 1u << 2,      // the conversation panel's input and picture
            DoneInput = 1u << 3,      // the ignore counters zeroed
            DoneTicking = 1u << 4,    // bAlwaysTick
            DoneViewTarget = 1u << 5, // a cutscene's CameraActor swapped for the controller
            DoneTether = 1u << 6,
            DoneCamEnabled = 1u << 7, // the camera's bDisabled cleared
        };

        struct SavedBind
        {
            SFXName Name;
            std::wstring Command;
        };

        struct HeldTimer
        {
            SFXName FuncName;
            UObject* TimerObj;
        };

        struct PhotoSession
        {
            bool On = false;
            unsigned Done = 0;
            /// The mode under it: Conversation or Cinematic.
            unsigned char Mode = NoMode;
            Kept<ABioPlayerController> Pc;
            Kept<ASFXPlayerCamera> Camera;
            Kept<USFXGameModeManager> Manager;
            Kept<USFXGameModePhoto> Photo;
            Kept<UMassEffectGuiManager> Gui;
            Kept<UBioSFPanel> Panel;
            Kept<AActor> OldTarget;
            Kept<AWorldInfo> World;
            Kept<UBioSubtitles> Subtitles;
            unsigned char OldPriority = 0;
            unsigned char OldIgnoreLook = 0;
            unsigned char OldIgnoreMove = 0;
            bool OldAlwaysTick = false;
            bool OldPlayersOnly = false;
            bool OldCamDisabled = false;
            bool OldPanelInputDisabled = false;
            bool OldPanelHandleInput = false;
            bool OldPanelVisible = false;
            float OldTether = 0.0f;
            float OldVehicleTether = 0.0f;
            FVector OldPcLocation{};
            FRotator OldPcRotation{};
            /// The shot on screen when the key was pressed.
            FTPOV EnterPov{};
            /// The subtitle line on screen: photo mode's Activated clears it.
            std::wstring Subtitle;
            float SubtitleTime = 0.0f;
            /// The static console binds: photo mode's Deactivated puts the defaults back, the conversation's stick swap lost.
            std::vector<SavedBind> ConsoleBinds;
            /// A frozen camera's (bDisabled) aspect, which the session's camera updates overwrite.
            float OldAspectRatio = 0.0f;
            bool OldConstrainAspect = false;
            std::vector<HeldTimer> Timers;
            /// Our controller's PlayerInput, whose key and axis events get past the world pause (see PauseLift), and how many
            /// did. A Pauser that changed during one of them is put back all the same: counted for the log.
            Kept<UPlayerInput> Input;
            unsigned InputKeys = 0;
            unsigned InputAxes = 0;
            /// The GUI manager's ticks run past the pause: photo mode's panel answers (tabs, options, prompts, sounds).
            unsigned GuiTicks = 0;
            unsigned InputPauserChanged = 0;
            /// The GUI manager's sounds made realtime so they play under the pause (see PlayGuiSound_hook).
            unsigned GuiSounds = 0;
            ULONGLONG EnteredAt = 0;
            LARGE_INTEGER LastCameraAt{};
            /// PlayerTick drove the camera since the last BioHUD.PostRender.
            bool CameraThisFrame = false;
            unsigned TickSteps = 0;
            unsigned HudSteps = 0;
        };

        PhotoSession Photo;
        /// The session being left: Photo is already over while the exit runs.
        PhotoSession PhotoExit;

        struct PhotoFunctions
        {
            CachedFunction GetScaleFormManager;
            CachedFunction ShowPhotoMode;
            CachedFunction HidePhotoMode;
            CachedFunction UpdateCurrentMode;
            CachedFunction DisableMode;
            CachedFunction SetLocation;
            CachedFunction SetRotation;
            CachedFunction UpdateCamera;
            CachedFunction DisplaySubtitle;
            CachedFunction SetStaticConsoleBinding;
        };
        PhotoFunctions PFn;
        /// The input hooks are in (see PauseLift): without them photo mode's camera gets no input under the world pause.
        bool InputHooked = false;
        /// The GUI tick hook is in (see PauseLift): without it photo mode's panel stands still under the world pause.
        bool GuiTickHooked = false;
        /// The GUI sound hooks are in (see PlayGuiSound_hook): without them photo mode's sounds wait for the pause's end.
        bool SoundHooked = false;

        /// Calls that function of the object, when its class has one (logged once per class when not).
        template <class Parms>
        bool Call(UObject* object, wchar_t const* name, CachedFunction& cache, Parms& parms)
        {
            auto const function = FunctionOf(object, name, cache);
            if (function == nullptr) return false;
            CallGame(object, function, &parms);
            return true;
        }

        USFXGameModePhoto* PhotoMode(USFXGameModeManager* manager)
        {
            auto const& modes = manager->GameModes;
            for (UINT i = 0; i < modes.Count(); i++)
            {
                auto const mode = modes.GetData()[(i + ModePhoto) % modes.Count()];
                if (mode != nullptr && mode->IsA(USFXGameModePhoto::StaticClass())) return static_cast<USFXGameModePhoto*>(mode);
            }
            return nullptr;
        }

        UMassEffectGuiManager* GuiOf(ABioPlayerController* pc)
        {
            APlayerController_execGetScaleFormManager_Parms p;
            if (!Call(pc, L"GetScaleFormManager", PFn.GetScaleFormManager, p)) return nullptr;
            auto const gui = p.ReturnValue;
            return gui != nullptr && gui->IsA(UMassEffectGuiManager::StaticClass()) ? static_cast<UMassEffectGuiManager*>(gui) : nullptr;
        }

        UBioPlayerInput* BioInput(APlayerController* pc)
        {
            auto const input = pc->PlayerInput;
            return input != nullptr && input->IsA(UBioPlayerInput::StaticClass()) ? static_cast<UBioPlayerInput*>(input) : nullptr;
        }

        void UpdateCurrentMode(USFXGameModeManager* manager)
        {
            USFXGameModeManager_execUpdateCurrentMode_Parms p;
            Call(manager, L"UpdateCurrentMode", PFn.UpdateCurrentMode, p);
        }

        /// Camera.UpdateCamera: the view target, the camera mode's Tick, CameraCache. The world tick doesn't run it under the
        /// pause: we do.
        void UpdateCamera(ASFXPlayerCamera* camera, float dt)
        {
            ACamera_eventUpdateCamera_Parms p;
            p.DeltaTime = dt;
            Call(camera, L"UpdateCamera", PFn.UpdateCamera, p);
        }

        /// PickCameraMode only switches modes when GameTimeSeconds - LastTransitionTime > 0.1 s, and GameTimeSeconds stands
        /// still under the pause: a switch just before it would keep the photo camera from ever being picked.
        void AllowCameraSwitch(ASFXPlayerCamera* camera, AWorldInfo* world) { camera->LastTransitionTime = world->GameTimeSeconds - 1.0f; }

        void SetPcPlace(ABioPlayerController* pc, FVector const& location, FRotator const& rotation)
        {
            AActor_execSetLocation_Parms at;
            at.NewLocation = location;
            Call(pc, L"SetLocation", PFn.SetLocation, at);
            AActor_execSetRotation_Parms to;
            to.NewRotation = rotation;
            Call(pc, L"SetRotation", PFn.SetRotation, to);
        }

        /// The photo cameras start at 54.4 degrees: a conversation's shot is often much narrower, and seen wider it looks
        /// like another shot.
        void SetPhotoFov(USFXCameraMode_Photo* mode, float fov)
        {
            if (mode == nullptr) return;
            mode->CurrentFOV = fov;
            mode->TargetFOV = fov;
        }

        float Distance(FVector const& a, FVector const& b)
        {
            auto const x = a.X - b.X;
            auto const y = a.Y - b.Y;
            auto const z = a.Z - b.Z;
            return std::sqrt(x * x + y * y + z * z);
        }

        /// Seconds since the last call (0 for the first), at most 0.1.
        float SecondsSince(LARGE_INTEGER& last)
        {
            LARGE_INTEGER now;
            LARGE_INTEGER frequency;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&frequency);
            auto const dt = last.QuadPart != 0 ? static_cast<float>(now.QuadPart - last.QuadPart) / static_cast<float>(frequency.QuadPart) : 0.0f;
            last = now;
            return std::clamp(dt, 0.0f, 0.1f);
        }

        std::vector<SavedBind> SaveBinds(TArray<FStaticKeyBind> const& binds)
        {
            std::vector<SavedBind> saved;
            saved.reserve(binds.Count());
            for (UINT i = 0; i < binds.Count(); i++)
            {
                auto const& bind = binds.GetData()[i];
                saved.push_back({ bind.Name, std::wstring(bind.Command.Chars(), bind.Command.Length()) });
            }
            return saved;
        }

        /// The static console binds photo mode's Deactivated reset to the defaults (the conversation's stick swap, for a
        /// southpaw profile), set back through the manager's own setter. The setter edits the first bind of a name, so a name
        /// held more than once (told apart by Ctrl, Shift or Alt) is left alone. The PC binds aren't: neither a conversation
        /// nor a cutscene changes them, and their defaults hold such pairs (Enter and Alt+Enter). Returns how many were set back.
        unsigned RestoreConsoleBinds(USFXGameModeManager* manager, std::vector<SavedBind> const& saved, TArray<FStaticKeyBind> const& binds)
        {
            unsigned restored = 0;
            for (auto const& s : saved)
            {
                auto const savedCount = std::count_if(saved.begin(), saved.end(), [&](SavedBind const& other) { return other.Name == s.Name; });
                FStaticKeyBind const* live = nullptr;
                auto liveCount = 0;
                for (UINT i = 0; i < binds.Count(); i++)
                {
                    if (binds.GetData()[i].Name != s.Name) continue;
                    if (liveCount++ == 0) live = &binds.GetData()[i];
                }
                if (savedCount != 1 || liveCount > 1) continue;
                if (live != nullptr && std::wstring_view(live->Command.Chars(), live->Command.Length()) == s.Command) continue;
                FString const command{ s.Command.c_str() };
                USFXGameModeManager_execHelper_SetStaticConsoleBinding_Parms p;
                p.Key = s.Name;
                std::memcpy(&p.Command, &command, sizeof(FString));
                Call(manager, L"Helper_SetStaticConsoleBinding", PFn.SetStaticConsoleBinding, p);
                restored++;
            }
            return restored;
        }

        /// Our controller ticks under the pause (bAlwaysTick), and so do its timers: the conversation's SkipNode (0.3 s, set by
        /// BioSFHandler_Conversation.Update) would move the conversation on. Every running timer of the controller is held,
        /// as the pause holds them without bAlwaysTick; the exit lets go of those held here.
        void HoldTimers(ABioPlayerController* pc, PhotoSession& s)
        {
            auto& timers = pc->Timers;
            for (UINT i = 0; i < timers.Count(); i++)
            {
                auto& timer = timers.GetData()[i];
                if (timer.bPaused) continue;
                timer.bPaused = 1;
                auto const known = std::any_of(s.Timers.begin(), s.Timers.end(), [&](HeldTimer const& held) {
                    return held.FuncName == timer.FuncName && held.TimerObj == timer.TimerObj;
                });
                if (!known) s.Timers.push_back({ timer.FuncName, timer.TimerObj });
            }
        }

        void ReleaseTimers(ABioPlayerController* pc, PhotoSession& s)
        {
            auto& timers = pc->Timers;
            for (UINT i = 0; i < timers.Count(); i++)
            {
                auto& timer = timers.GetData()[i];
                auto const held = std::any_of(s.Timers.begin(), s.Timers.end(), [&](HeldTimer const& h) {
                    return h.FuncName == timer.FuncName && h.TimerObj == timer.TimerObj;
                });
                if (held) timer.bPaused = 0;
            }
            s.Timers.clear();
        }

        /// The subtitle line Activated cleared, unless another one shows now.
        void RestoreSubtitle(PhotoSession& s)
        {
            auto const subtitles = s.Subtitles.Live();
            if (subtitles == nullptr || s.Subtitle.empty() || subtitles->m_sSubtitle.Length() != 0) return;
            FString const text{ s.Subtitle.c_str() };
            UBioSubtitles_execDisplaySubtitle_Parms p;
            std::memcpy(&p.sSubtitle, &text, sizeof(FString));
            p.fDuration = s.SubtitleTime;
            Call(subtitles, L"DisplaySubtitle", PFn.DisplaySubtitle, p);
        }

        unsigned char AddCounter(unsigned char a, unsigned char b) { return static_cast<unsigned char>(std::min(255, a + b)); }

        /// Y or F7 under our pause of a conversation or a cutscene. Returns what it did, or null when refused. The session is
        /// recorded before anything changes: when this stops midway, DropPhoto gives back what it got to.
        char const* EnterPhoto(ABioPlayerController* pc, unsigned char mode, bool gamepad)
        {
            auto const world = pc->WorldInfo;
            auto const manager = pc->GameModeManager;
            auto const camera = pc->PlayerCamera != nullptr && pc->PlayerCamera->IsA(ASFXPlayerCamera::StaticClass())
                                    ? static_cast<ASFXPlayerCamera*>(pc->PlayerCamera)
                                    : nullptr;
            auto const photo = manager != nullptr ? PhotoMode(manager) : nullptr;
            auto const gui = GuiOf(pc);
            if (world == nullptr || manager == nullptr || camera == nullptr || photo == nullptr || gui == nullptr || photo->bIsActive != 0)
            {
                LEASI_WARN("photo mode refused: world {}, mode manager {}, camera {}, photo mode {} (active {}), GUI manager {}", PathOf(world),
                           PathOf(manager), PathOf(camera), PathOf(photo), photo != nullptr && photo->bIsActive != 0, PathOf(gui));
                return nullptr;
            }

            auto& s = Photo;
            s = {};
            s.On = true;
            s.Mode = mode;
            s.EnteredAt = GetTickCount64();
            s.Pc.Set(pc);
            s.Camera.Set(camera);
            s.Manager.Set(manager);
            s.Photo.Set(photo);
            s.Gui.Set(gui);
            s.World.Set(world);
            s.Input.Set(pc->PlayerInput);
            s.EnterPov = camera->CameraCache.POV;
            s.OldPcLocation = pc->Location;
            s.OldPcRotation = pc->Rotation;
            s.OldPlayersOnly = world->bPlayersOnly != 0;
            if (world->IsA(ABioWorldInfo::StaticClass()))
            {
                if (auto const subtitles = static_cast<ABioWorldInfo*>(world)->m_Subtitles)
                {
                    s.Subtitles.Set(subtitles);
                    s.Subtitle.assign(subtitles->m_sSubtitle.Chars(), subtitles->m_sSubtitle.Length());
                    s.SubtitleTime = subtitles->m_fTimeRemaining;
                }
            }
            if (auto const input = BioInput(pc)) s.ConsoleBinds = SaveBinds(input->StaticConsoleBinds);
            auto const& pov = s.EnterPov;
            LEASI_INFO("photo mode enter, mode {} ({}): view target {} (pending {}), CameraStyle {}, camera mode {}, camera disabled {}, shot at ({:.0f} "
                       "{:.0f} {:.0f}) FOV {:.1f}, controller at ({:.0f} {:.0f} {:.0f}), pawn {}, subtitle {} chars",
                       mode, ModeName(mode), PathOf(camera->ViewTarget.Target), PathOf(camera->PendingViewTarget.Target), NameText(camera->CameraStyle),
                       PathOf(camera->CurrentCameraMode), camera->bDisabled != 0, pov.Location.X, pov.Location.Y, pov.Location.Z, pov.FOV,
                       pc->Location.X, pc->Location.Y, pc->Location.Z, PathOf(pc->Pawn), s.Subtitle.size());

            // 1. The controller's timers stand still, as the pause holds them.
            HoldTimers(pc, s);

            // 2. The conversation panel: no input (it would eat the sticks, and A or Enter would pick a reply), and hidden (the
            //    wheel, the subtitle). Activated does the input part itself, for ambient conversations only.
            if (auto const panel = gui->m_oConversationPanel)
            {
                s.Panel.Set(panel);
                s.OldPanelInputDisabled = panel->bInputDisabled != 0;
                s.OldPanelHandleInput = panel->bHandleInputThisFrame != 0;
                s.OldPanelVisible = panel->IsVisible != 0;
                panel->bInputDisabled = 1;
                panel->bHandleInputThisFrame = 1;
                panel->IsVisible = 0;
                s.Done |= DonePanel;
            }

            // 3. Photo above Conversation and Cinematic: EnableMode's own UpdateCurrentMode then makes it current.
            s.OldPriority = photo->Priority;
            photo->Priority = PriorityMenus;
            s.Done |= DonePriority;

            // 4. A cutscene's director camera: SFXPlayerCamera.UpdateViewTarget renders a CameraActor target as it is, past the
            //    camera modes. The controller is the target for the session (written raw: SetViewTarget would blend).
            if (auto const target = camera->ViewTarget.Target; target != nullptr && target->IsA(ACameraActor::StaticClass()))
            {
                s.OldTarget.Set(target);
                camera->ViewTarget.Target = pc;
                s.Done |= DoneViewTarget;
            }
            // A frozen camera (BioConversation.DisableCamera) answers CameraCache and never runs the photo camera.
            if (camera->bDisabled)
            {
                s.OldCamDisabled = true;
                s.OldAspectRatio = camera->m_fLastAspectRatio;
                s.OldConstrainAspect = camera->m_bLastConstrainAspect != 0;
                camera->bDisabled = 0;
                s.Done |= DoneCamEnabled;
            }

            // 5. PlayerPhotoMode.PlayerMove keeps the camera within PhotoModeTetherDistance (1000) of the pawn: a cutscene's shot
            //    may be farther. No pawn: of the origin.
            auto const away = pc->Pawn != nullptr ? Distance(pov.Location, pc->Pawn->Location) : Distance(pov.Location, FVector{});
            if (away + 500.0f > pc->PhotoModeTetherDistance)
            {
                s.OldTether = pc->PhotoModeTetherDistance;
                s.OldVehicleTether = pc->VehiclePhotoModeTetherDistance;
                pc->PhotoModeTetherDistance = away + 1000.0f;
                pc->VehiclePhotoModeTetherDistance = std::max(pc->VehiclePhotoModeTetherDistance, away + 1000.0f);
                s.Done |= DoneTether;
            }

            // The key we took never reaches PlayerInput, whose bUsingGamepad (the last input's device) picks photo mode's button
            // prompts and lets the sticks drive its camera: set as that key would have.
            if (pc->PlayerInput != nullptr) pc->PlayerInput->bUsingGamepad = gamepad ? 1 : 0;

            // 6. The game's own entry: EnableMode(Photo) -> Activated (PushState PlayerPhotoMode at CameraCache's shot,
            //    PauseGame(TRUE), post process, static binds, subtitle cleared) -> UpdateCurrentMode; then the photo panel.
            auto const show = FunctionOf(gui, L"ShowPhotoMode", PFn.ShowPhotoMode);
            if (show == nullptr) return nullptr;
            // Before the call: a fault inside it still gets the game's exit.
            s.Done |= DoneShown;
            UMassEffectGuiManager_execShowPhotoMode_Parms shown;
            CallGame(gui, show, &shown);
            if (manager->CurrentMode != ModePhoto) UpdateCurrentMode(manager);
            if (manager->CurrentMode != ModePhoto)
            {
                LEASI_WARN("photo mode refused: the mode is {} ({}) after ShowPhotoMode", manager->CurrentMode, ModeName(manager->CurrentMode));
                return nullptr;
            }
            // Activated cleared the subtitle line: back now, in the hidden conversation panel. The GUI ticks let past the pause
            // run the panel's handler, whose Update hides the panel's line when the text changes, and nothing shows it again
            // before the world resumes. Only with the panel there: without it, the GUI manager's Update would open one.
            if (s.Done & DonePanel) RestoreSubtitle(s);

            // 7. The conversation's ignore counters (1 each) would zero the sticks: kept, zeroed, given back at the exit.
            s.OldIgnoreLook = pc->bIgnoreLookInput;
            s.OldIgnoreMove = pc->bIgnoreMoveInput;
            pc->bIgnoreLookInput = 0;
            pc->bIgnoreMoveInput = 0;
            s.Done |= DoneInput;

            // 8. PlayerTick under the pause: PlayerInput, then PlayerPhotoMode.PlayerMove moves the controller.
            s.OldAlwaysTick = pc->bAlwaysTick != 0;
            pc->bAlwaysTick = 1;
            s.Done |= DoneTicking;

            // 9. The photo camera now (PickCameraMode -> PhotoCamFree), at the shot's field of view.
            AllowCameraSwitch(camera, world);
            UpdateCamera(camera, 0.0f);
            if (pov.FOV > 1.0f && pov.FOV < 170.0f)
            {
                SetPhotoFov(photo->PhotoCamFree, pov.FOV);
                SetPhotoFov(photo->PhotoCamOrbit, pov.FOV);
            }
            auto const pcOff = Distance(pc->Location, pov.Location);
            if (pcOff > 1.0f) SetPcPlace(pc, pov.Location, pov.Rotation);
            UpdateCamera(camera, 0.0f);
            QueryPerformanceCounter(&s.LastCameraAt);
            LEASI_INFO("photo mode on: camera mode {}, controller {:.0f} uu from the shot{}, view {:.0f} uu from it, FOV {:.1f}, photo handler {}, {} timer(s) "
                       "held, ignore counters were look {} move {}, {} prompts, input {}, panel {}, GUI sounds {}",
                       PathOf(camera->CurrentCameraMode), pcOff, pcOff > 1.0f ? " (put on it)" : "",
                       Distance(camera->CameraCache.POV.Location, pov.Location), camera->CameraCache.POV.FOV, PathOf(shown.ReturnValue), s.Timers.size(),
                       s.OldIgnoreLook, s.OldIgnoreMove, gamepad ? "pad" : "keyboard",
                       InputHooked ? "let past the pause" : "NOT hooked (the camera won't answer: see the start of this log)",
                       GuiTickHooked ? "let past the pause" : "NOT hooked (tabs, options and prompts won't answer: see the start of this log)",
                       SoundHooked ? "made realtime" : "NOT hooked (they play when the pause ends: see the start of this log)");
            return "entered photo mode";
        }

        /// After PlayerController.PlayerTick on our controller: PlayerMove has moved it, so the camera follows in the same
        /// frame, before the view is drawn.
        void PhotoTick(ABioPlayerController* pc, float dt)
        {
            auto& s = Photo;
            auto const camera = s.Camera.Live();
            if (!s.On || pc != s.Pc.Object || camera == nullptr) return;
            auto const measured = SecondsSince(s.LastCameraAt);
            UpdateCamera(camera, dt > 0.0f && dt < 0.1f ? dt : measured);
            s.CameraThisFrame = true;
            s.TickSteps++;
        }

        /// Each frame, after Validate: what the game may have set again (an ignore counter, a timer, bAlwaysTick), and the
        /// camera when no PlayerTick drove it this frame (seen one frame late then).
        void PhotoFrame()
        {
            auto& s = Photo;
            if (!s.On) return;
            auto const pc = s.Pc.Live();
            if (pc == nullptr) return;
            // Counted in for the give-back.
            if (pc->bIgnoreLookInput != 0)
            {
                s.OldIgnoreLook = AddCounter(s.OldIgnoreLook, pc->bIgnoreLookInput);
                pc->bIgnoreLookInput = 0;
            }
            if (pc->bIgnoreMoveInput != 0)
            {
                s.OldIgnoreMove = AddCounter(s.OldIgnoreMove, pc->bIgnoreMoveInput);
                pc->bIgnoreMoveInput = 0;
            }
            if (!pc->bAlwaysTick) pc->bAlwaysTick = 1;
            HoldTimers(pc, s);
            if (!s.CameraThisFrame)
            {
                if (auto const camera = s.Camera.Live())
                {
                    if (s.HudSteps++ == 0) LEASI_WARN("photo mode: no PlayerTick on our controller this frame, the camera is driven from BioHUD.PostRender");
                    UpdateCamera(camera, SecondsSince(s.LastCameraAt));
                }
            }
            s.CameraThisFrame = false;
        }

        /// A camera the conversation froze (BioConversation.DisableCamera) is frozen again on its own shot: a frozen camera
        /// shows CameraCache as it is, and the session filled it with photo mode's views.
        void RefreezeCamera(PhotoSession const& s, ASFXPlayerCamera* camera)
        {
            camera->bDisabled = 1;
            camera->CameraCache.POV = s.EnterPov;
            camera->m_fLastAspectRatio = s.OldAspectRatio;
            camera->m_bLastConstrainAspect = s.OldConstrainAspect ? 1 : 0;
        }

        /// Field writes on what is still alive; with calls, the controller's place too. Each step clears its bit once done.
        void RestoreFields(PhotoSession& s, bool calls)
        {
            auto const pc = s.Pc.Live();
            auto const camera = s.Camera.Live();
            auto const photo = s.Photo.Live();
            auto const panel = s.Panel.Live();
            auto const world = s.World.Live();
            if ((s.Done & DonePriority) && photo != nullptr) photo->Priority = s.OldPriority;
            s.Done &= ~DonePriority;
            if ((s.Done & DoneInput) && pc != nullptr)
            {
                LEASI_INFO("photo mode exit: ignore counters look {} move {}, given back look +{} move +{}", pc->bIgnoreLookInput, pc->bIgnoreMoveInput,
                           s.OldIgnoreLook, s.OldIgnoreMove);
                pc->bIgnoreLookInput = AddCounter(pc->bIgnoreLookInput, s.OldIgnoreLook);
                pc->bIgnoreMoveInput = AddCounter(pc->bIgnoreMoveInput, s.OldIgnoreMove);
            }
            s.Done &= ~DoneInput;
            if ((s.Done & DonePanel) && panel != nullptr)
            {
                panel->bInputDisabled = s.OldPanelInputDisabled ? 1 : 0;
                panel->bHandleInputThisFrame = s.OldPanelHandleInput ? 1 : 0;
                panel->IsVisible = s.OldPanelVisible ? 1 : 0;
            }
            s.Done &= ~DonePanel;
            if ((s.Done & DoneViewTarget) && camera != nullptr) camera->ViewTarget.Target = s.OldTarget.Live() != nullptr ? s.OldTarget.Object : pc;
            s.Done &= ~DoneViewTarget;
            if ((s.Done & DoneCamEnabled) && camera != nullptr) RefreezeCamera(s, camera);
            s.Done &= ~DoneCamEnabled;
            if ((s.Done & DoneTether) && pc != nullptr)
            {
                pc->PhotoModeTetherDistance = s.OldTether;
                pc->VehiclePhotoModeTetherDistance = s.OldVehicleTether;
            }
            s.Done &= ~DoneTether;
            if ((s.Done & DoneTicking) && pc != nullptr) pc->bAlwaysTick = s.OldAlwaysTick ? 1 : 0;
            s.Done &= ~DoneTicking;
            if (pc != nullptr)
            {
                ReleaseTimers(pc, s);
                // The right mouse button's hold, if it was down at the exit.
                pc->bPhotoModeCameraUnlocked = 0;
            }
            // Deactivated's PauseGame(FALSE) wrote 0; Activated's TRUE stays when Deactivated never ran.
            if (world != nullptr) world->bPlayersOnly = s.OldPlayersOnly ? 1 : 0;
            if (calls && pc != nullptr) SetPcPlace(pc, s.OldPcLocation, s.OldPcRotation);
        }

        /// The game's own exit, then our fields, then one camera update: the paused shot is on screen again. Works on PhotoExit.
        void ExitPhoto(char const* why)
        {
            auto& s = PhotoExit;
            auto const gui = s.Gui.Live();
            auto const manager = s.Manager.Live();
            auto const photo = s.Photo.Live();
            auto const camera = s.Camera.Live();
            auto const world = s.World.Live();
            auto const pc = s.Pc.Live();

            // 1. RemovePanel + DisableMode(Photo): Deactivated (PopState -> PoppedState: the rotation back; PauseGame(FALSE); the
            //    static binds back to the defaults) -> UpdateCurrentMode. HidePhotoMode only disables the mode while its panel
            //    is there.
            unsigned binds = 0;
            if (s.Done & DoneShown)
            {
                if (gui != nullptr)
                {
                    UMassEffectGuiManager_execHidePhotoMode_Parms p;
                    Call(gui, L"HidePhotoMode", PFn.HidePhotoMode, p);
                }
                if (manager != nullptr && photo != nullptr && photo->bIsActive)
                {
                    USFXGameModeManager_eventDisableMode_Parms p;
                    p.Mode = ModePhoto;
                    Call(manager, L"DisableMode", PFn.DisableMode, p);
                }
                s.Done &= ~DoneShown;
                auto const input = pc != nullptr ? BioInput(pc) : nullptr;
                if (manager != nullptr && input != nullptr) binds = RestoreConsoleBinds(manager, s.ConsoleBinds, input->StaticConsoleBinds);
            }

            // 2. Our fields: Priority first, then the conversation or the cutscene is current again. A frozen camera is frozen
            //    again only after step 3: frozen, it would skip PickCameraMode and keep the photo camera.
            auto const refreeze = (s.Done & DoneCamEnabled) != 0;
            s.Done &= ~DoneCamEnabled;
            RestoreFields(s, true);
            RestoreSubtitle(s);
            if (manager != nullptr && manager->CurrentMode == ModePhoto) UpdateCurrentMode(manager);

            // 3. One camera update: PickCameraMode picks the conversation's (or the director's) camera again.
            if (camera != nullptr && world != nullptr)
            {
                AllowCameraSwitch(camera, world);
                UpdateCamera(camera, 0.0f);
            }
            if (refreeze && camera != nullptr) RefreezeCamera(s, camera);
            LEASI_INFO("photo mode left ({}) after {:.1f} s: mode {} ({}), camera mode {}, {} static bind(s) set back, {} PlayerTick and {} HUD camera "
                       "steps, let past the pause: {} key and {} axis event(s), {} GUI tick(s) ({} changed the Pauser: put back), {} GUI sound(s)",
                       why, HeldFor(s.EnteredAt), manager != nullptr ? manager->CurrentMode : NoMode,
                       manager != nullptr ? ModeName(manager->CurrentMode) : "none", camera != nullptr ? PathOf(camera->CurrentCameraMode) : "None", binds,
                       s.TickSteps, s.HudSteps, s.InputKeys, s.InputAxes, s.GuiTicks, s.InputPauserChanged, s.GuiSounds);
        }

        void ExitPhotoEntry(void* why) { ExitPhoto(static_cast<char const*>(why)); }

        void RestoreFieldsEntry(void*) { RestoreFields(PhotoExit, false); }

        /// Ends the session: the game's exit and ours, under a guard (worldGone: field writes only, on what is still alive,
        /// nothing called in a world being torn down). After a fault, the fields are given back raw.
        void DropPhoto(char const* why, bool worldGone)
        {
            if (!Photo.On) return;
            PhotoExit = std::move(Photo);
            Photo = {};
            if (worldGone)
            {
                if (!Guarded(RestoreFieldsEntry, nullptr)) LEASI_ERROR("access violation while giving back photo mode's fields ({})", why);
                LEASI_INFO("photo mode dropped ({}): fields given back on what is still alive", why);
            }
            else if (!Guarded(ExitPhotoEntry, const_cast<char*>(why)))
            {
                LEASI_ERROR("access violation while leaving photo mode ({}): fields given back raw, photo mode may stay on", why);
                if (!Guarded(RestoreFieldsEntry, nullptr)) LEASI_ERROR("access violation while giving back photo mode's fields");
            }
            PhotoExit = {};
            LEASI_FLUSH();
        }

        // ! World pause: checks
        // ========================================

        /// Every frame and every key of ours: our world pause ends when someone else ended it (Pauser None: the game, a load,
        /// the Steam overlay closing clears every CanUnpause that agrees), when its WorldInfo is gone, or when the mode left
        /// Conversation and Cinematic under it (Photo is ours while our photo session runs). Our photo session ends first,
        /// with the world or the mode it stands on. The movie pause isn't checked here: Bink.cpp ends it (close, Esc, its
        /// movie gone silent), and SkipMovie and the HUD drawing again do.
        void Validate(ABioPlayerController* pc, char const* where)
        {
            if (Photo.On)
            {
                auto const photoPc = Photo.Pc.Live();
                if (Photo.World.Live() == nullptr || photoPc == nullptr) DropPhoto("its world or controller is gone", true);
                else if (!G.WorldPaused || photoPc->WorldInfo != G.World || G.World->Pauser == nullptr) DropPhoto("the world pause ended", false);
                else if (ModeOf(photoPc) != ModePhoto) DropPhoto("the mode left Photo", false);
            }
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
                else if (!Photo.On && pc != nullptr && pc->WorldInfo == G.World)
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
            LEASI_INFO("Console.InputKey runs while movie #{} is open: key {} event {}, mode {} ({}), movie mode {}, pause keys {}, thread {}", serial,
                       ToUtf8(std::wstring_view(key.Chars(), key.Length())), p->Event, mode, ModeName(mode), MovieModeText(MovieMode(pc)),
                       Bink::OwnsKeys() ? "the movie's" : "vanilla", GetCurrentThreadId());
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
            else if (Photo.On)
            {
                // Photo mode's own Esc and Start (ShowMenuGui) would open the pause menu over the conversation.
                DropPhoto(KeyName(key), false);
                action = "left photo mode, the world stays paused";
            }
            else if (G.WorldPaused)
            {
                ResumeWorld(KeyName(key));
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

            LEASI_INFO("{} pressed: mode {} ({}), movie mode {}, Pauser {} ({}), {} open movie(s), {}: {} (function {}, thread {})", KeyName(key), mode,
                       ModeName(mode), MovieModeText(movie), PathOf(pauser), static_cast<void const*>(pauser), movies, PathOf(pc),
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
            else if (Photo.On)
            {
                DropPhoto(KeyName(key), false);
                action = "left photo mode, the world stays paused";
            }
            else if (G.WorldPaused && pc != nullptr && pc == G.Controller && (mode == ModeConversation || mode == ModeCinematic))
            {
                // The pause screen goes before photo mode shows anything: its first screenshot must not catch it.
                G.PhotoHideFrames = PhotoHideGrace;
                Overlay::Publish(false, false, true);
                action = EnterPhoto(pc, mode, key == Key::Y);
                // What Enter got to before it stopped. The pause screen comes back this frame, not three frames later.
                if (action == nullptr)
                {
                    DropPhoto("refused", false);
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
            auto const movieSpace = key == Key::Space ? Bink::SpaceSeenDown() : 0;
            if (Bink::PauseWanted() && Bink::OwnsKeys())
            {
                auto const space = key == Key::Space;
                Bink::RequestResume(pressed ? (space ? "Space pressed" : "A pressed")
                                            : (space ? "Space during a fullscreen movie, its press unseen here" : "A during a fullscreen movie, its press unseen here"));
                action = "resumed the movie";
            }
            else if (movieSpace != 0 && (movieSpace >= now || now - movieSpace < ReleaseGraceMs))
                action = "taken: the movie's thread resumed the movie on it";
            else if (pressed && G.WorldPaused && !Photo.On)
            {
                auto const pc = LocalController(console);
                Validate(pc, KeyName(key));
                if (G.WorldPaused)
                {
                    ResumeWorld(KeyName(key));
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
            auto const movieEsc = Bink::EscSeenDown();
            if (movieEsc != 0 && (movieEsc >= now || now - movieEsc < ReleaseGraceMs)) return "Esc";
            auto const movieSpace = Bink::SpaceSeenDown();
            if (movieSpace != 0 && (movieSpace >= now || now - movieSpace < ReleaseGraceMs)) return "Space";
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
            DrawCentered(canvas, gamepad ? L"Start: resume     Y: photo mode" : L"Esc: resume     F7: photo mode", height * 0.08f + 48.0f * scale,
                         1.1f * scale, Color(200, 200, 200, 220));

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
            if (Photo.On) G.PhotoHideFrames = PhotoHideGrace;
            else if (G.PhotoHideFrames > 0) G.PhotoHideFrames--;
            auto const hidden = G.WorldPaused && (Photo.On || G.PhotoHideFrames > 0 || ConsoleIsOpen());
            auto const mode = ModeOf(pc);
            auto const photo = InputHooked && GuiTickHooked && pc != nullptr && pc == G.Controller && (mode == ModeConversation || mode == ModeCinematic);
            Overlay::Publish(G.WorldPaused && !hidden, G.WorldPaused && !hidden && photo, hidden);
        }

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
            PhotoFrame();
            PublishOverlay(pc);
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

        struct TickCall
        {
            ABioPlayerController* Pc;
            float DeltaTime;
        };

        void PhotoTickEntry(void* context)
        {
            auto const c = static_cast<TickCall*>(context);
            PhotoTick(c->Pc, c->DeltaTime);
        }

        constexpr unsigned MaxFrameFaults = 3;

        void PublishBareEntry(void*)
        {
            Overlay::Publish(G.WorldPaused && !Photo.On && Alive(G.World, G.WorldIndex) && G.World->Pauser != nullptr, false, Photo.On);
        }

        /// After an access violation in the per-frame checks: our state may be what faulted, so it ends here, photo mode
        /// first, and a world we paused is resumed (under its own guard) rather than left paused with nothing of ours to
        /// resume it.
        void DropState()
        {
            DropPhoto("fault", false);
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

            static SFXName const InputKey{ L"InputKey", 0 };
            static SFXName const PostRender{ L"PostRender", 0 };
            static SFXName const SkipMovie{ L"SkipMovie", 0 };
            static SFXName const SkipCinematic{ L"SkipCinematic", 0 };
            static SFXName const GuiKey{ L"GuiKey", 0 };
            static SFXName const ShowMenuGui{ L"ShowMenuGui", 0 };
            static SFXName const PlayerTick{ L"PlayerTick", 0 };

            auto const& name = Function->Name;
            if (name == InputKey)
            {
                if (Parms != nullptr && Context->IsA(UConsole::StaticClass()))
                {
                    KeyCall call{ static_cast<UConsole*>(Context), Function, static_cast<UConsole_execInputKey_Parms*>(Parms), false };
                    if (!Guarded(TakeKeyEntry, &call))
                    {
                        LEASI_ERROR("access violation while reading a key: passed to the game{}", Photo.On ? ", photo mode left" : "");
                        call.Taken = false;
                        DropPhoto("fault while reading a key", false);
                    }
                    if (call.Taken) return;
                }
            }
            else if (name == SkipMovie || name == SkipCinematic || name == GuiKey)
            {
                if (Context->IsA(USFXGameModeBase::StaticClass()) && DropSkip(Context, Function, Parms, name == GuiKey)) return;
            }
            else if (name == ShowMenuGui)
            {
                // Photo mode's Esc and Start binds: they would hide it and open the pause menu over the conversation. Ours
                // take those keys first in Console.InputKey; this is for a bind that gets past it.
                if (Photo.On && Context->IsA(USFXGameModePhoto::StaticClass()))
                {
                    LEASI_INFO("ShowMenuGui on {} dropped: our photo mode is left with Esc, Start, Y or F7", PathOf(Context));
                    return;
                }
            }

            orig(Context, Function, Parms, Result);

            if (name == PlayerTick && Photo.On && Context == Photo.Pc.Object && Parms != nullptr)
            {
                TickCall call{ static_cast<ABioPlayerController*>(Context), static_cast<APlayerController_eventPlayerTick_Parms*>(Parms)->DeltaTime };
                if (!Guarded(PhotoTickEntry, &call))
                {
                    LEASI_ERROR("access violation while driving the photo camera: photo mode left");
                    DropPhoto("fault in the photo camera", false);
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
                if (!Overlay::Drawing() && G.WorldPaused && !Photo.On && !G.BannerOff && !Guarded(DrawBannerEntry, Context))
                {
                    LEASI_ERROR("access violation while drawing the banner: banner off");
                    G.BannerOff = true;
                }
            }
        }

        // ! Input under the world pause
        // ========================================

        // The engine drops input while WorldInfo.Pauser is set: UInput::InputKey keeps only the releases of keys held from
        // before, and UPlayerInput::InputAxis skips the axis binds. The GUI manager's tick stops too: no panel advances (its
        // movie doesn't play what it is told: tabs, options shown or hidden), no focus change, no button prompt refresh, no
        // GUI sound. Vanilla photo mode never meets it (it pauses with bPlayersOnly). While ours is on, its controller's key
        // and axis events and its GUI manager's ticks run with the Pauser cleared, put back as the call returns: the game
        // thread runs them between the world's ticks, so nothing else sees the world unpaused.

        /// BioPlayerInput::InputKey (it ends in UPlayerInput::InputKey, then UInput::InputKey) and UPlayerInput::InputAxis
        /// (BioPlayerInput::InputAxis ends in a jump to it), with their first bytes in this build of MassEffect1.exe. The key
        /// is an 8-byte FName, passed in a register.
        using t_InputKey = unsigned long(UObject* Input, int ControllerId, unsigned long long Key, int Event, float Amount, unsigned long bGamepad);
        using t_InputAxis = unsigned long(UObject* Input, int ControllerId, unsigned long long Key, float Delta, float DeltaTime, unsigned long bGamepad);

        constexpr unsigned long long InputKeyRva = 0xc6de70;
        constexpr unsigned char InputKeyStart[] = { 0x4C, 0x89, 0x44, 0x24, 0x18, 0x53, 0x55, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57 };
        constexpr unsigned long long InputAxisRva = 0x68bc40;
        constexpr unsigned char InputAxisStart[] = { 0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x70 };

        /// UBioSFManager::Tick: the GUI manager's frame (event cooldowns, its script Update, each panel's movie, the focus,
        /// the UI world). Under the Pauser it returns right after marking which panels take input.
        using t_GuiTick = void(UObject* Manager, float DeltaTime);

        constexpr unsigned long long GuiTickRva = 0x193bc0;
        constexpr unsigned char GuiTickStart[] = { 0x40, 0x53, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x50 };

        std::atomic<t_InputKey*> InputKey_orig{ nullptr };
        std::atomic<t_InputAxis*> InputAxis_orig{ nullptr };
        std::atomic<t_GuiTick*> GuiTick_orig{ nullptr };

        /// As for ProcessEvent: a call that arrives before InstallHook handed back the original waits for it.
        template <typename T>
        T* OriginalOf(std::atomic<T*>& orig)
        {
            auto f = orig.load(std::memory_order_acquire);
            while (f == nullptr)
            {
                YieldProcessor();
                f = orig.load(std::memory_order_acquire);
            }
            return f;
        }

        /// For one call: clears the world's Pauser when the call is on our photo session's object, on the game thread, and
        /// puts it back after.
        class PauseLift
        {
        public:
            PauseLift(UObject* object, UObject* ours, unsigned& count)
            {
                if (!Photo.On || object != ours || GetCurrentThreadId() != GameThreadId.load(std::memory_order_relaxed)) return;
                auto const world = Photo.World.Live();
                if (world == nullptr || world->Pauser == nullptr) return;
                World.Set(world);
                Pauser = world->Pauser;
                world->Pauser = nullptr;
                count++;
            }

            ~PauseLift()
            {
                auto const world = World.Live();
                if (world == nullptr) return;
                if (world->Pauser != nullptr && world->Pauser != Pauser) Photo.InputPauserChanged++;
                world->Pauser = Pauser;
            }

            PauseLift(PauseLift const&) = delete;
            PauseLift& operator=(PauseLift const&) = delete;

        private:
            Kept<AWorldInfo> World;
            APlayerReplicationInfo* Pauser = nullptr;
        };

        unsigned long InputKey_hook(UObject* input, int controllerId, unsigned long long key, int event, float amount, unsigned long gamepad)
        {
            auto const orig = OriginalOf(InputKey_orig);
            PauseLift lift(input, Photo.Input.Object, Photo.InputKeys);
            return orig(input, controllerId, key, event, amount, gamepad);
        }

        unsigned long InputAxis_hook(UObject* input, int controllerId, unsigned long long key, float delta, float deltaTime, unsigned long gamepad)
        {
            auto const orig = OriginalOf(InputAxis_orig);
            PauseLift lift(input, Photo.Input.Object, Photo.InputAxes);
            return orig(input, controllerId, key, delta, deltaTime, gamepad);
        }

        void GuiTick_hook(UObject* manager, float deltaTime)
        {
            auto const orig = OriginalOf(GuiTick_orig);
            PauseLift lift(manager, Photo.Gui.Object, Photo.GuiTicks);
            orig(manager, deltaTime);
        }

        // ! GUI sounds under the world pause
        // ========================================

        // While the world is paused, the audio device (its Update gets bGameTicking off) starts no new sound and advances
        // none, except those whose AudioComponent is bNonRealtime. The GUI manager's sounds aren't: under our pause, photo
        // mode's click (TakePhoto), tab and option sounds would wait for the pause's end and all play then. While ours is
        // on, the components our GUI manager's PlayGuiSound makes are marked bNonRealtime: they play at once.

        /// UBioSFManager::PlayGuiSound (virtual): its GuiSounds 2DA's row (an FName, in a register) gives the cue it plays,
        /// with no owner, through UAudioDevice::CreateComponent(Cue, Scene, Actor, bPlay, bStopWhenOwnerDestroyed, Location).
        using t_PlayGuiSound = void(UObject* Manager, unsigned long long Sound);
        using t_CreateAudioComponent = UAudioComponent*(USoundCue* Cue, void* Scene, AActor* Actor, unsigned long bPlay,
                                                        unsigned long bStopWhenOwnerDestroyed, FVector* Location);

        constexpr unsigned long long PlayGuiSoundRva = 0x191ee0;
        constexpr unsigned char PlayGuiSoundStart[] = { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0xC7, 0x44, 0x24, 0x30, 0xFE, 0xFF, 0xFF, 0xFF };
        constexpr unsigned long long CreateAudioComponentRva = 0x2ff470;
        constexpr unsigned char CreateAudioComponentStart[] = { 0x48, 0x8B, 0xC4, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xA0 };

        std::atomic<t_PlayGuiSound*> PlayGuiSound_orig{ nullptr };
        std::atomic<t_CreateAudioComponent*> CreateAudioComponent_orig{ nullptr };

        /// Our session's GUI manager is in PlayGuiSound (game thread only).
        unsigned GuiSoundDepth = 0;

        void PlayGuiSound_hook(UObject* manager, unsigned long long sound)
        {
            auto const orig = OriginalOf(PlayGuiSound_orig);
            auto const ours = Photo.On && manager == Photo.Gui.Object && GetCurrentThreadId() == GameThreadId.load(std::memory_order_relaxed);
            if (ours) GuiSoundDepth++;
            orig(manager, sound);
            if (ours) GuiSoundDepth--;
        }

        UAudioComponent* CreateAudioComponent_hook(USoundCue* cue, void* scene, AActor* actor, unsigned long play, unsigned long stopWhenOwnerDestroyed,
                                                   FVector* location)
        {
            auto const orig = OriginalOf(CreateAudioComponent_orig);
            auto const component = orig(cue, scene, actor, play, stopWhenOwnerDestroyed, location);
            if (component != nullptr && GetCurrentThreadId() == GameThreadId.load(std::memory_order_relaxed) && GuiSoundDepth > 0)
            {
                component->bNonRealtime = 1;
                Photo.GuiSounds++;
            }
            return component;
        }

        /// Hooks one native after checking its first bytes: another build of the game gets a warning (saying what photo mode
        /// loses), not a patch.
        template <typename T>
        bool HookNative(::LESDK::Initializer& Init, char const* name, unsigned long long rva, unsigned char const* start, size_t size, T* detour,
                        std::atomic<T*>& orig, char const* loss)
        {
            auto const target = Init.ResolveTyped<T>(::LESDK::Address::FromOffset(rva));
            if (target == nullptr || std::memcmp(reinterpret_cast<void const*>(target), start, size) != 0)
            {
                LEASI_WARN("{} not found at RVA {:#x} (another build of the game?): {}", name, rva, loss);
                return false;
            }
            orig.store(reinterpret_cast<T*>(Init.InstallHook(name, reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour))),
                       std::memory_order_release);
            if (orig.load() == nullptr)
            {
                LEASI_WARN("hooking {} failed: {}", name, loss);
                return false;
            }
            return true;
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

        constexpr auto noInput = "photo mode's camera under the pause won't answer it";
        auto const keys = HookNative(Init, "BioPlayerInput::InputKey", InputKeyRva, InputKeyStart, sizeof(InputKeyStart), InputKey_hook, InputKey_orig,
                                     noInput);
        auto const axes = HookNative(Init, "UPlayerInput::InputAxis", InputAxisRva, InputAxisStart, sizeof(InputAxisStart), InputAxis_hook,
                                     InputAxis_orig, noInput);
        InputHooked = keys && axes;
        if (InputHooked) LEASI_INFO("input hooked: photo mode's keys and axes get past the world pause");

        GuiTickHooked = HookNative(Init, "UBioSFManager::Tick", GuiTickRva, GuiTickStart, sizeof(GuiTickStart), GuiTick_hook, GuiTick_orig,
                                   "photo mode's panel under the pause won't answer (tabs, options, prompts)");
        if (GuiTickHooked) LEASI_INFO("GUI tick hooked: photo mode's panel gets past the world pause");

        constexpr auto noSound = "photo mode's GUI sounds under the pause wait for its end";
        auto const play = HookNative(Init, "UBioSFManager::PlayGuiSound", PlayGuiSoundRva, PlayGuiSoundStart, sizeof(PlayGuiSoundStart),
                                     PlayGuiSound_hook, PlayGuiSound_orig, noSound);
        SoundHooked = play && HookNative(Init, "UAudioDevice::CreateComponent", CreateAudioComponentRva, CreateAudioComponentStart,
                                         sizeof(CreateAudioComponentStart), CreateAudioComponent_hook, CreateAudioComponent_orig, noSound);
        if (SoundHooked) LEASI_INFO("GUI sounds hooked: photo mode's sounds play under the world pause");
    }

    DWORD GameThread() { return GameThreadId.load(std::memory_order_relaxed); }

    ULONGLONG GameThreadSeen() { return GameThreadTick.load(std::memory_order_relaxed); }

    ULONGLONG HudSeen() { return HudTick.load(std::memory_order_relaxed); }
}
