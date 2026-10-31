#include "MelePause/Photo.hpp"

// The game's photo mode under the world pause, where game/LE<n>.hpp turns it on (MELEPAUSE_PHOTO). What differs from one
// game to the next comes from there: the natives' addresses and first bytes, the camera switch, and whether the GUI
// sounds need our hooks (MELEPAUSE_PHOTO_GUI_SOUNDS). The GUI side, photo mode's panel and the conversation's, is
// PhotoGui's.
#if MELEPAUSE_PHOTO

#include "MelePause/Engine.hpp"
#include "MelePause/PhotoGui.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace MelePause::PhotoMode
{
    namespace
    {
        using namespace Game;

        /// SFXGameModeBase.EModePriority.ModePriority_Menus: above Conversation (2) and Cinematic (Base, 0).
        constexpr unsigned char PriorityMenus = 3;

        /// A camera step long enough to end an instant transition (SFXCameraMode_Interpolate makes its 0 s 0.00001 s), short
        /// enough to move nothing else.
        constexpr float TransitionStep = 0.001f;

        // ! Photo mode
        // ========================================

        // The game's own photo mode, over our world pause. Vanilla opens it only from the pause menu, which the game refuses in
        // a conversation or a cutscene; its real entry, MassEffectGuiManager.ShowPhotoMode, has no gate. The world doesn't tick
        // under the pause: our controller is made to (bAlwaysTick: PlayerInput, then PlayerPhotoMode.PlayerMove), and the
        // camera is updated from here once a frame. What stands in the way is set aside for the session and given back at its
        // end: photo mode's priority (below Conversation's), the conversation panel (it eats the sticks, and shows the wheel
        // and the subtitle), the ignore counters, the controller's timers, a cutscene's director camera. The exit is the
        // game's own (HidePhotoMode), then ours.

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
            DoneBlend = 1u << 8,      // a view target blend held
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
            /// PhotoGui::Find's: the GUI whose tick runs photo mode's panel.
            Kept<UObject> Gui;
            Kept<AActor> OldTarget;
            /// A blend to another view target (a cutscene's first second), held for the session.
            Kept<AActor> OldPending;
            FTViewTarget OldPendingView{};
            float OldBlendTimeToGo = 0.0f;
            Kept<AWorldInfo> World;
            unsigned char OldPriority = 0;
            unsigned char OldIgnoreLook = 0;
            unsigned char OldIgnoreMove = 0;
            bool OldAlwaysTick = false;
            bool OldPlayersOnly = false;
            bool OldCamDisabled = false;
            float OldTether = 0.0f;
            float OldVehicleTether = 0.0f;
            FVector OldPcLocation{};
            FRotator OldPcRotation{};
            /// The shot on screen when the key was pressed.
            FTPOV EnterPov{};
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
            /// LE3: the last GFx movies' Tick that GfxClock moved the world's clock for.
            LARGE_INTEGER LastGuiAt{};
            /// Frames on which the game showed the conversation's panel again and PhotoGui::Hold hid it.
            unsigned PanelHolds = 0;
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
            CachedFunction UpdateCurrentMode;
            CachedFunction DisableMode;
            CachedFunction SetLocation;
            CachedFunction SetRotation;
            CachedFunction UpdateCamera;
            CachedFunction SetStaticConsoleBinding;
        };
        PhotoFunctions PFn;
        /// The input hooks are in (see PauseLift): without them photo mode's camera gets no input under the world pause.
        bool InputHooked = false;
        /// The GUI tick hook is in (see PauseLift; LE3: GfxClock): without it photo mode's panel stands still under the world
        /// pause.
        bool GuiTickHooked = false;
        /// The GUI sound hooks are in (see PlayGuiSound_hook): without them photo mode's sounds wait for the pause's end.
        bool SoundHooked = false;
#if MELEPAUSE_PHOTO_GUI_SOUNDS
        constexpr auto SoundNotHooked = "NOT hooked (they play when the pause ends: see the start of this log)";
#else
        constexpr auto SoundNotHooked = "not hooked in this build (the game's)";
#endif

        USFXGameModePhoto* PhotoModeOf(USFXGameModeManager* manager)
        {
            auto const& modes = manager->GameModes;
            for (UINT i = 0; i < modes.Count(); i++)
            {
                auto const mode = modes.GetData()[(i + ModePhoto) % modes.Count()];
                if (mode != nullptr && mode->IsA(USFXGameModePhoto::StaticClass())) return static_cast<USFXGameModePhoto*>(mode);
            }
            return nullptr;
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

        /// Ends the camera transition an update just started. PickCameraMode leaves or enters the photo cameras through one of
        /// 0.00001 s (bInstantTransition, the photo mode's InstantTransition), which a step of 0 s leaves at its start, the old
        /// camera's view, and nothing steps it again under the pause. The next update switches to its camera.
        void EndTransition(ASFXPlayerCamera* camera)
        {
            auto const mode = camera->CurrentCameraMode;
            if (mode != nullptr && mode->IsA(USFXCameraMode_Interpolate::StaticClass())) UpdateCamera(camera, TransitionStep);
        }

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

        /// The larger of the yaw and pitch differences, in degrees.
        float Turn(FRotator const& a, FRotator const& b)
        {
            auto const wrap = [](int d) { return std::abs(static_cast<int>(static_cast<short>(d))); };
            return static_cast<float>(std::max(wrap(a.Yaw - b.Yaw), wrap(a.Pitch - b.Pitch))) * (360.0f / 65536.0f);
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
        /// as the pause holds them without bAlwaysTick; the exit lets go of those held here. Photo mode's own run: LE3's
        /// Activated turns the modes' input back on 0.5 s later (EnableInputWrapper).
        void HoldTimers(ABioPlayerController* pc, PhotoSession& s)
        {
            auto& timers = pc->Timers;
            auto const photo = s.Photo.Object;
            for (UINT i = 0; i < timers.Count(); i++)
            {
                auto& timer = timers.GetData()[i];
                if (timer.bPaused || (photo != nullptr && timer.TimerObj == photo)) continue;
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

        unsigned char AddCounter(unsigned char a, unsigned char b) { return static_cast<unsigned char>(std::min(255, a + b)); }

        /// Y or F7 under our pause of a conversation or a cutscene. Returns what it did, or null when refused. The session is
        /// recorded before anything changes: when this stops midway, DropPhoto gives back what it got to.
        char const* EnterPhoto(ABioPlayerController* pc, unsigned char mode, bool gamepad)
        {
            auto const world = pc->WorldInfo;
            auto const manager = ModeManagerOf(pc);
            auto const camera = pc->PlayerCamera != nullptr && pc->PlayerCamera->IsA(ASFXPlayerCamera::StaticClass())
                                    ? static_cast<ASFXPlayerCamera*>(pc->PlayerCamera)
                                    : nullptr;
            auto const photo = manager != nullptr ? PhotoModeOf(manager) : nullptr;
            auto const gui = PhotoGui::Find(pc);
            if (world == nullptr || manager == nullptr || camera == nullptr || photo == nullptr || gui == nullptr || photo->bIsActive != 0)
            {
                LEASI_WARN("photo mode refused: world {}, mode manager {}, camera {}, photo mode {} (active {}), GUI manager {}", PathOf(world),
                           PathOf(manager), PathOf(camera), PathOf(photo), photo != nullptr && photo->bIsActive != 0, PathOf(gui));
                return nullptr;
            }

            auto& s = Photo;
            s = {};
            PhotoGui::Clear();
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
            s.OldPcLocation = LocationOf(pc);
            s.OldPcRotation = pc->Rotation;
            s.OldPlayersOnly = world->bPlayersOnly != 0;
            auto const subtitleChars = PhotoGui::KeepSubtitle(world);
            if (auto const input = BioInput(pc)) s.ConsoleBinds = SaveBinds(input->StaticConsoleBinds);
            auto const& pov = s.EnterPov;
            LEASI_INFO("photo mode enter, mode {} ({}): view target {} (pending {}), CameraStyle {}, camera mode {}, camera disabled {}, shot at ({:.0f} "
                       "{:.0f} {:.0f}) FOV {:.1f}, controller at ({:.0f} {:.0f} {:.0f}), pawn {}, subtitle {} chars",
                       mode, ModeName(mode), PathOf(camera->ViewTarget.Target), PathOf(camera->PendingViewTarget.Target), NameText(camera->CameraStyle),
                       PathOf(camera->CurrentCameraMode), camera->bDisabled != 0, LocationOf(pov).X, LocationOf(pov).Y, LocationOf(pov).Z, pov.FOV,
                       s.OldPcLocation.X, s.OldPcLocation.Y, s.OldPcLocation.Z, PathOf(pc->Pawn), subtitleChars);

            // 1. The controller's timers stand still, as the pause holds them.
            HoldTimers(pc, s);

            // 2. The conversation panel: no input (it would eat the sticks, and A or Enter would pick a reply), and hidden (the
            //    wheel, the subtitle). Activated does the input part itself, for ambient conversations only.
            if (PhotoGui::SetAside(pc, gui)) s.Done |= DonePanel;

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
            //    A blend to a new view target, such as a cutscene's first second blending to the director's camera, is held:
            //    Camera.UpdateCamera mixes the photo view with that target's, and at its end makes that target current (in LE1
            //    a CameraActor there also clears CurrentCameraMode at every update), so photo mode would stop answering.
            if (auto const pending = camera->PendingViewTarget.Target; pending != nullptr)
            {
                s.OldPending.Set(pending);
                s.OldPendingView = camera->PendingViewTarget;
                s.OldBlendTimeToGo = camera->BlendTimeToGo;
                camera->PendingViewTarget.Target = nullptr;
                camera->PendingViewTarget.Controller = nullptr;
                s.Done |= DoneBlend;
                LEASI_INFO("photo mode: the blend to {} held, {:.2f} s to go", PathOf(pending), s.OldBlendTimeToGo);
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
            auto const away = pc->Pawn != nullptr ? Distance(LocationOf(pov), LocationOf(pc->Pawn)) : Distance(LocationOf(pov), FVector{});
            if (away + 500.0f > pc->PhotoModeTetherDistance)
            {
                s.OldTether = pc->PhotoModeTetherDistance;
                pc->PhotoModeTetherDistance = away + 1000.0f;
                if (auto const vehicle = VehicleTetherOf(pc))
                {
                    s.OldVehicleTether = *vehicle;
                    *vehicle = std::max(*vehicle, away + 1000.0f);
                }
                s.Done |= DoneTether;
            }

            // The key we took never reaches PlayerInput, whose bUsingGamepad (the last input's device) picks photo mode's button
            // prompts and lets the sticks drive its camera: set as that key would have.
            if (pc->PlayerInput != nullptr) pc->PlayerInput->bUsingGamepad = gamepad ? 1 : 0;

            // 6. The game's own entry: EnableMode(Photo) -> Activated (PushState PlayerPhotoMode at CameraCache's shot,
            //    bPlayersOnly, post process, static binds; in LE1 the subtitle cleared) -> UpdateCurrentMode; then the photo panel.
            // DoneShown before the call: a fault inside it still gets the game's exit.
            s.Done |= DoneShown;
            UObject* handler = nullptr;
            if (!PhotoGui::Show(pc, gui, handler))
            {
                s.Done &= ~DoneShown;
                return nullptr;
            }
            if (manager->CurrentMode != ModePhoto) UpdateCurrentMode(manager);
            if (manager->CurrentMode != ModePhoto)
            {
                LEASI_WARN("photo mode refused: the mode is {} ({}) after ShowPhotoMode", manager->CurrentMode, ModeName(manager->CurrentMode));
                return nullptr;
            }
            // LE1's Activated cleared the subtitle line: back now, in the hidden conversation panel. The GUI ticks let past the pause
            // run the panel's handler, whose Update hides the panel's line when the text changes, and nothing shows it again
            // before the world resumes. Only with the panel there: without it, the GUI manager's Update would open one.
            if (s.Done & DonePanel) PhotoGui::AfterShow();

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

            // 9. The photo camera now (PickCameraMode -> PhotoCamFree), at the shot's field of view. The switch's transition
            //    (InitializeTransition) turns the controller to the old camera mode's view: in a director's shot that mode is
            //    the one left behind before it (Explore, the gameplay camera), looking elsewhere. The controller goes back on
            //    the shot after it.
            AllowCameraSwitch(camera, world);
            UpdateCamera(camera, 0.0f);
            EndTransition(camera);
            if (pov.FOV > 1.0f && pov.FOV < 170.0f)
            {
                SetPhotoFov(photo->PhotoCamFree, pov.FOV);
                SetPhotoFov(photo->PhotoCamOrbit, pov.FOV);
            }
            auto const pcOff = Distance(LocationOf(pc), LocationOf(pov));
            auto const pcTurn = Turn(pc->Rotation, pov.Rotation);
            SetPcPlace(pc, LocationOf(pov), pov.Rotation);
            UpdateCamera(camera, 0.0f);
            QueryPerformanceCounter(&s.LastCameraAt);
            LEASI_INFO("photo mode on: camera mode {}, controller {:.0f} uu and {:.0f} deg from the shot (put on it), view {:.0f} uu and {:.0f} deg from it, "
                       "FOV {:.1f}, photo handler {}, {} timer(s) held, ignore counters were look {} move {}, {} prompts, input {}, panel {}, GUI sounds {}",
                       PathOf(camera->CurrentCameraMode), pcOff, pcTurn, Distance(LocationOf(camera->CameraCache.POV), LocationOf(pov)),
                       Turn(camera->CameraCache.POV.Rotation, pov.Rotation), camera->CameraCache.POV.FOV, PathOf(handler), s.Timers.size(),
                       s.OldIgnoreLook, s.OldIgnoreMove, gamepad ? "pad" : "keyboard",
                       InputHooked ? "let past the pause" : "NOT hooked (the camera won't answer: see the start of this log)",
                       GuiTickHooked ? "let past the pause" : "NOT hooked (tabs, options and prompts won't answer: see the start of this log)",
                       SoundHooked ? "made realtime" : SoundNotHooked);
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
            if ((s.Done & DonePanel) && PhotoGui::Hold() && s.PanelHolds++ == 0)
                LEASI_INFO("photo mode: the game showed the conversation's panel again, hidden again");
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

        /// The view target blend on again where it stood, and the paused shot on screen until the world resumes: the camera
        /// update without the blend showed the outgoing view alone.
        void RestoreBlend(PhotoSession const& s, ASFXPlayerCamera* camera)
        {
            if (s.OldPending.Live() == nullptr) return;
            camera->PendingViewTarget = s.OldPendingView;
            camera->BlendTimeToGo = s.OldBlendTimeToGo;
            camera->CameraCache.POV = s.EnterPov;
        }

        /// Field writes on what is still alive; with calls, the controller's place too. Each step clears its bit once done.
        void RestoreFields(PhotoSession& s, bool calls)
        {
            auto const pc = s.Pc.Live();
            auto const camera = s.Camera.Live();
            auto const photo = s.Photo.Live();
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
            if (s.Done & DonePanel) PhotoGui::GiveBack(calls);
            s.Done &= ~DonePanel;
            if ((s.Done & DoneViewTarget) && camera != nullptr) camera->ViewTarget.Target = s.OldTarget.Live() != nullptr ? s.OldTarget.Object : pc;
            s.Done &= ~DoneViewTarget;
            if ((s.Done & DoneBlend) && camera != nullptr) RestoreBlend(s, camera);
            s.Done &= ~DoneBlend;
            if ((s.Done & DoneCamEnabled) && camera != nullptr) RefreezeCamera(s, camera);
            s.Done &= ~DoneCamEnabled;
            if ((s.Done & DoneTether) && pc != nullptr)
            {
                pc->PhotoModeTetherDistance = s.OldTether;
                if (auto const vehicle = VehicleTetherOf(pc)) *vehicle = s.OldVehicleTether;
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
                if (gui != nullptr) PhotoGui::Hide(pc, gui);
                if (manager != nullptr && photo != nullptr && photo->bIsActive)
                {
                    auto p = DisableModeParms(ModePhoto);
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
            // A held blend too: on again during step 3, its end would make its target current under the pause.
            auto const reblend = (s.Done & DoneBlend) != 0;
            s.Done &= ~DoneBlend;
            RestoreFields(s, true);
            PhotoGui::RestoreSubtitle();
            if (manager != nullptr && manager->CurrentMode == ModePhoto) UpdateCurrentMode(manager);

            // 3. One camera update: PickCameraMode picks the conversation's (or the director's) camera again, its transition
            //    ended on that camera's view.
            if (camera != nullptr && world != nullptr)
            {
                AllowCameraSwitch(camera, world);
                UpdateCamera(camera, 0.0f);
                EndTransition(camera);
            }
            if (refreeze && camera != nullptr) RefreezeCamera(s, camera);
            if (reblend && camera != nullptr) RestoreBlend(s, camera);
            LEASI_INFO("photo mode left ({}) after {:.1f} s: mode {} ({}), camera mode {}, {} static bind(s) set back, {} PlayerTick and {} HUD camera "
                       "steps, let past the pause: {} key and {} axis event(s), {} GUI tick(s) ({} changed the Pauser: put back), {} GUI sound(s), "
                       "conversation panel hidden again on {} frame(s)",
                       why, HeldFor(s.EnteredAt), manager != nullptr ? manager->CurrentMode : NoMode,
                       manager != nullptr ? ModeName(manager->CurrentMode) : "none", camera != nullptr ? PathOf(camera->CurrentCameraMode) : "None", binds,
                       s.TickSteps, s.HudSteps, s.InputKeys, s.InputAxes, s.GuiTicks, s.InputPauserChanged, s.GuiSounds, s.PanelHolds);
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

        // ! Input under the world pause
        // ========================================

        // The engine drops input while WorldInfo.Pauser is set: UInput::InputKey keeps only the releases of keys held from
        // before, and UPlayerInput::InputAxis skips the axis binds. The GUI manager's tick stops too: no panel advances (its
        // movie doesn't play what it is told: tabs, options shown or hidden), no focus change, no button prompt refresh, no
        // GUI sound. Vanilla photo mode never meets it (it pauses with bPlayersOnly). While ours is on, its controller's key
        // and axis events and its GUI manager's ticks run with the Pauser cleared, put back as the call returns: the game
        // thread runs them between the world's ticks, so nothing else sees the world unpaused.

        /// BioPlayerInput::InputKey (it ends in UPlayerInput::InputKey, then UInput::InputKey) and UPlayerInput::InputAxis, at
        /// game/LE<n>.hpp's addresses. The key is an 8-byte FName, passed in a register.
        using t_InputKey = unsigned long(UObject* Input, int ControllerId, unsigned long long Key, int Event, float Amount, unsigned long bGamepad);
        using t_InputAxis = unsigned long(UObject* Input, int ControllerId, unsigned long long Key, float Delta, float DeltaTime, unsigned long bGamepad);

        /// LE1 and LE2: UBioSFManager::Tick, the GUI manager's frame (event cooldowns, its script Update, each panel's movie,
        /// the focus, the UI world); under the Pauser it returns right after marking which panels take input. LE3: the GFx
        /// movies' Tick (see GfxClock), whose this is no UObject.
        using t_GuiTick = void(UObject* Manager, float DeltaTime);

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
                if (!Photo.On || object != ours || GetCurrentThreadId() != GameThread()) return;
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

#if MELEPAUSE_PHOTO_GFX
        /// For one call of the GFx movies' Tick (LE3): it checks no Pauser, but advances each movie by the time since its last
        /// advance, and most movies run on the world's TimeSeconds, which stands still under the pause. Photo mode's panel
        /// would never load: its movie tells the game it loaded from its own frames. While our session is on, on the game
        /// thread, TimeSeconds moves by the real time since the last call, for this call only; the stamps the call wrote with
        /// that time are then put back to the world's, so the next call measures from the world's clock again.
        class GfxClock
        {
        public:
            GfxClock(UObject* movies, unsigned& count) : Movies(reinterpret_cast<unsigned char*>(movies))
            {
                if (!Photo.On || GetCurrentThreadId() != GameThread()) return;
                auto const world = Photo.World.Live();
                if (world == nullptr || world->Pauser == nullptr) return;
                auto const dt = SecondsSince(Photo.LastGuiAt);
                if (dt <= 0.0f) return;
                World.Set(world);
                Time = world->TimeSeconds;
                Fed = Time + dt;
                world->TimeSeconds = Fed;
                count++;
            }

            ~GfxClock()
            {
                auto const world = World.Live();
                if (world == nullptr) return;
                for (auto const& list : GfxMovieLists)
                {
                    unsigned char** entries = nullptr;
                    int num = 0;
                    std::memcpy(&entries, Movies + list.Data, sizeof entries);
                    std::memcpy(&num, Movies + list.Count, sizeof num);
                    for (int i = 0; entries != nullptr && i < num; i++)
                    {
                        if (entries[i] == nullptr) continue;
                        float stamp = 0.0f;
                        std::memcpy(&stamp, entries[i] + GfxMovieStamp, sizeof stamp);
                        if (stamp == Fed) std::memcpy(entries[i] + GfxMovieStamp, &Time, sizeof Time);
                    }
                }
                world->TimeSeconds = Time;
            }

            GfxClock(GfxClock const&) = delete;
            GfxClock& operator=(GfxClock const&) = delete;

        private:
            unsigned char* Movies;
            Kept<AWorldInfo> World;
            float Time = 0.0f;
            float Fed = 0.0f;
        };
#endif

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
#if MELEPAUSE_PHOTO_GFX
            GfxClock clock(manager, Photo.GuiTicks);
#else
            PauseLift lift(manager, Photo.Gui.Object, Photo.GuiTicks);
#endif
            orig(manager, deltaTime);
        }

        // ! GUI sounds under the world pause
        // ========================================

#if MELEPAUSE_PHOTO_GUI_SOUNDS
        // While the world is paused, the audio device (its Update gets bGameTicking off) starts no new sound and advances
        // none, except those whose AudioComponent is bNonRealtime. The GUI manager's sounds aren't: under our pause, photo
        // mode's click (TakePhoto), tab and option sounds would wait for the pause's end and all play then. While ours is
        // on, the components our GUI manager's PlayGuiSound makes are marked bNonRealtime: they play at once.

        /// UBioSFManager::PlayGuiSound (virtual): its GuiSounds 2DA's row (an FName, in a register) gives the cue it plays,
        /// with no owner, through UAudioDevice::CreateComponent(Cue, Scene, Actor, bPlay, bStopWhenOwnerDestroyed, Location).
        using t_PlayGuiSound = void(UObject* Manager, unsigned long long Sound);
        using t_CreateAudioComponent = UAudioComponent*(USoundCue* Cue, void* Scene, AActor* Actor, unsigned long bPlay,
                                                        unsigned long bStopWhenOwnerDestroyed, FVector* Location);

        std::atomic<t_PlayGuiSound*> PlayGuiSound_orig{ nullptr };
        std::atomic<t_CreateAudioComponent*> CreateAudioComponent_orig{ nullptr };

        /// Our session's GUI manager is in PlayGuiSound (game thread only).
        unsigned GuiSoundDepth = 0;

        void PlayGuiSound_hook(UObject* manager, unsigned long long sound)
        {
            auto const orig = OriginalOf(PlayGuiSound_orig);
            auto const ours = Photo.On && manager == Photo.Gui.Object && GetCurrentThreadId() == GameThread();
            if (ours) GuiSoundDepth++;
            orig(manager, sound);
            if (ours) GuiSoundDepth--;
        }

        UAudioComponent* CreateAudioComponent_hook(USoundCue* cue, void* scene, AActor* actor, unsigned long play, unsigned long stopWhenOwnerDestroyed,
                                                   FVector* location)
        {
            auto const orig = OriginalOf(CreateAudioComponent_orig);
            auto const component = orig(cue, scene, actor, play, stopWhenOwnerDestroyed, location);
            if (component != nullptr && GetCurrentThreadId() == GameThread() && GuiSoundDepth > 0)
            {
                component->bNonRealtime = 1;
                Photo.GuiSounds++;
            }
            return component;
        }

        /// The GUI manager's PlayGuiSound with its sound marked as our photo session's are. The depth is put back in
        /// __finally: a fault caught further up must not leave every later sound bNonRealtime. No C++ object may live in this
        /// frame.
        void CallMarkedGuiSound(UObject* gui, UFunction* function, void* parms)
        {
            GuiSoundDepth++;
            __try
            {
                CallGame(gui, function, parms);
            }
            __finally
            {
                GuiSoundDepth--;
            }
        }
#endif

        /// Hooks one native after checking its first bytes: another build of the game gets a warning (saying what photo mode
        /// loses), not a patch.
        template <typename T>
        bool HookNative(::LESDK::Initializer& Init, char const* name, unsigned long long rva, std::span<unsigned char const> start, T* detour,
                        std::atomic<T*>& orig, char const* loss)
        {
            auto const target = Init.ResolveTyped<T>(::LESDK::Address::FromOffset(rva));
            if (target == nullptr || std::memcmp(reinterpret_cast<void const*>(target), start.data(), start.size()) != 0)
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

    void InstallHooks(::LESDK::Initializer& Init)
    {
        constexpr auto noInput = "photo mode's camera under the pause won't answer it";
        auto const keys = HookNative(Init, "BioPlayerInput::InputKey", PhotoInputKeyRva, PhotoInputKeyStart, InputKey_hook, InputKey_orig, noInput);
        auto const axes = HookNative(Init, "UPlayerInput::InputAxis", PhotoInputAxisRva, PhotoInputAxisStart, InputAxis_hook, InputAxis_orig, noInput);
        InputHooked = keys && axes;
        if (InputHooked) LEASI_INFO("input hooked: photo mode's keys and axes get past the world pause");

        GuiTickHooked = HookNative(Init, MELEPAUSE_PHOTO_GFX ? "the GFx movies' Tick" : "UBioSFManager::Tick", PhotoGuiTickRva, PhotoGuiTickStart,
                                   GuiTick_hook, GuiTick_orig, "photo mode's panel under the pause won't answer (tabs, options, prompts)");
        if (GuiTickHooked) LEASI_INFO("GUI tick hooked: photo mode's panel gets past the world pause");

#if MELEPAUSE_PHOTO_GUI_SOUNDS
        constexpr auto noSound = "photo mode's GUI sounds under the pause wait for its end";
        auto const play = HookNative(Init, "UBioSFManager::PlayGuiSound", PhotoPlayGuiSoundRva, PhotoPlayGuiSoundStart, PlayGuiSound_hook,
                                     PlayGuiSound_orig, noSound);
        SoundHooked = play && HookNative(Init, "UAudioDevice::CreateComponent", PhotoCreateAudioComponentRva, PhotoCreateAudioComponentStart,
                                         CreateAudioComponent_hook, CreateAudioComponent_orig, noSound);
        if (SoundHooked) LEASI_INFO("GUI sounds hooked: photo mode's sounds play under the world pause");
#else
        LEASI_INFO("GUI sounds: not hooked in the {} build, photo mode's sounds under the world pause are the game's", SDK_TARGET_NAME_A);
#endif
    }

    bool Answers() { return InputHooked && GuiTickHooked; }

    bool On() { return Photo.On; }

    bool IsController(UObject* object) { return Photo.On && object == Photo.Pc.Object; }

    char const* Enter(ABioPlayerController* pc, unsigned char mode, bool gamepad) { return EnterPhoto(pc, mode, gamepad); }

    void Drop(char const* why, bool worldGone) { DropPhoto(why, worldGone); }

    void Check(bool worldPaused, AWorldInfo* pausedWorld)
    {
        if (!Photo.On) return;
        auto const photoPc = Photo.Pc.Live();
        if (Photo.World.Live() == nullptr || photoPc == nullptr) DropPhoto("its world or controller is gone", true);
        else if (!worldPaused || photoPc->WorldInfo != pausedWorld || pausedWorld->Pauser == nullptr) DropPhoto("the world pause ended", false);
        else if (ModeOf(photoPc) != ModePhoto) DropPhoto("the mode left Photo", false);
    }

    void Tick(ABioPlayerController* pc, float dt) { PhotoTick(pc, dt); }

    void Frame() { PhotoFrame(); }

    bool PlayGuiSound(ABioPlayerController* pc, wchar_t const* row)
    {
#if MELEPAUSE_PHOTO_GUI_SOUNDS
        auto const gui = SoundHooked && pc != nullptr ? PhotoGui::Find(pc) : nullptr;
        if (gui == nullptr) return false;
        static CachedFunction cache;
        auto const function = FunctionOf(gui, L"PlayGuiSound", cache);
        if (function == nullptr) return false;
        UBioSFManager_execPlayGuiSound_Parms parms;
        parms.nmSound = SFXName{ row, 0 };
        CallMarkedGuiSound(gui, function, &parms);
        return true;
#else
        LEASI_UNUSED_2(pc, row);
        return false;
#endif
    }
}

#endif
