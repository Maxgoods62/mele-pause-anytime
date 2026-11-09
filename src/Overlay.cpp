#include "PauseAnytime/Api.hpp"
#include "PauseAnytime/Blur.hpp"
#include "PauseAnytime/GameFont.hpp"
#include "PauseAnytime/OverlayRules.hpp"
#include "PauseAnytime/Resources.h"
#include "PauseAnytime/TextRules.hpp"

#include <d3d11.h>
#include <dxgi1_4.h>
#include <tlhelp32.h>

#pragma warning(push, 0)
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_dx11.h"
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cstddef>
#include <exception>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The pause screen, drawn into the game's back buffer from a hooked IDXGISwapChain::Present, on the game's window only
// (Start), by one thread at a time (The gate): over our world pause and our movie pause the frame is blurred and darkened
// (Blur.cpp), in the game's menu colours (OverlayLook, NativeLook): the title on a band at the centre, and at the bottom left
// each key left of its label (resume, and photo mode where it opens), for the gamepad or the keyboard, whichever pressed our
// keys last. The game thread says what to show (Publish, UsePadPrompts, UseTexts, UseGameIcons, UseGameFonts); nothing
// here touches a game object, only the D3D11 views of two of its textures and, on the game thread, the bytes of its fonts'
// movies. The words are the ones Pause.cpp picked (a translation mod's, the game's or the built-in text of the launcher's
// language, TextRules.hpp), set in the game's own fonts, turned into TrueType in memory (GameFont.cpp) and never written
// anywhere, or in Exo 2 (SIL OFL 1.1, fonts/OFL.txt), compiled into the ASI (PauseAnytime.rc). Without the hook, before the
// game's swap chain is taken, or after a fault, the Canvas banner of Pause.cpp is drawn instead (Drawing).
namespace PauseAnytime::Overlay
{
    namespace
    {
        using t_Present = HRESULT STDMETHODCALLTYPE(IDXGISwapChain* chain, UINT sync, UINT flags);
        using t_Present1 = HRESULT STDMETHODCALLTYPE(IDXGISwapChain1* chain, UINT sync, UINT flags, DXGI_PRESENT_PARAMETERS const* parameters);
        using t_ResizeBuffers = HRESULT STDMETHODCALLTYPE(IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
        using t_ResizeBuffers1 = HRESULT STDMETHODCALLTYPE(IDXGISwapChain3* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
                                                           UINT const* nodeMasks, IUnknown* const* queues);

        /// The loader makes a hook live before InstallHook hands back the original: a call arriving first waits for it.
        std::atomic<t_Present*> Present_orig{ nullptr };
        std::atomic<t_Present1*> Present1_orig{ nullptr };
        std::atomic<t_ResizeBuffers*> ResizeBuffers_orig{ nullptr };
        std::atomic<t_ResizeBuffers1*> ResizeBuffers1_orig{ nullptr };

        template <class T>
        T* Wait(std::atomic<T*> const& slot)
        {
            auto orig = slot.load(std::memory_order_acquire);
            while (orig == nullptr)
            {
                YieldProcessor();
                orig = slot.load(std::memory_order_acquire);
            }
            return orig;
        }

        // ! What the game thread publishes
        // ========================================

        constexpr unsigned ShowWorldPause = 1;
        constexpr unsigned OfferPhoto = 2;
        /// Photo mode or the console: gone at once, no fade (photo mode's screenshots, the console's text).
        constexpr unsigned HideNow = 4;
        std::atomic<unsigned> Published{ 0 };
        std::atomic<bool> Pad{ false };
        /// The overlay started on the game's swap chain and hasn't failed since: R's phase is Started, for the threads that
        /// read it without the gate (SetPhase keeps the two in step).
        std::atomic<bool> Live{ false };
        /// Another swap chain presents while the one we draw on has been silent for BannerAfterMs: the game's new one, maybe,
        /// waiting to be taken (AdoptAfterMs). Nothing is drawn meanwhile, so Drawing is false and the banner shows. Cleared
        /// when ours presents again or the new one is taken. Not set by a mere hitch, after which ours presents first.
        std::atomic<bool> AdoptPending{ false };
        using OverlayRules::BannerAfterMs;
        /// Install's, set before the hooks go live.
        std::atomic<OverlayLook> Look{ OverlayLook::LE1 };

        /// UseTexts', UTF-8. Never freed: the render thread draws from it.
        std::atomic<TextRules::Picks const*> PickedTexts{ nullptr };

        /// UseGameIcons', not checked yet. Never freed, like PickedTexts.
        struct GameIcons
        {
            void* PadA;
            void* PadY;
        };
        std::atomic<GameIcons const*> GameIcon{ nullptr };

        /// One of the game's fonts as TrueType, with what that file can't give ImGui.
        struct GameFontFile
        {
            std::vector<unsigned char> Ttf;
            GameFont::Layout Layout;
        };

        /// UseGameFonts'. Never freed: ImGui reads the files for as long as its atlas lives.
        struct GameFonts
        {
            GameFontFile Light;
            GameFontFile Bold;
        };
        std::atomic<GameFonts const*> GameFontFiles{ nullptr };

        GameFontFile ReadGameFont(std::span<unsigned char const> movie, std::string_view name, std::span<char32_t const> letters)
        {
            return { GameFont::Build(movie, name, letters), GameFont::ReadLayout(movie, name, letters) };
        }

        // ! Render thread state
        // ========================================

        constexpr float FadeInSeconds = 0.2f;
        constexpr float FadeOutSeconds = 0.15f;
        /// Frames in a row the pause screen should show and can't draw (no view on the back buffer): then it is off for the
        /// session and the Canvas banner shows instead.
        constexpr unsigned MaxDrawFailures = 10;
        /// MovieIdle's presents a second, at most.
        constexpr LONGLONG IdleRate = 60;
        /// MovieIdle presents only once the game has presented nothing for this long.
        constexpr LONGLONG IdleQuietMs = 100;
        /// A present from another swap chain may be the game's new one (a display mode change) once the one we draw on has
        /// been silent this long (OverlayRules::Readopt).
        using OverlayRules::AdoptAfterMs;
        /// How long a resize waits for a holder of the gate away in a call we don't control, and for one in our own work,
        /// before it takes the gate from it (OverlayRules::EnterWaiting).
        using OverlayRules::ResizeWaitMs;
        using OverlayRules::ResizeWaitOwnMs;

        struct Fonts
        {
            ImFont* Regular = nullptr;
            ImFont* SemiBold = nullptr;
            bool Embedded = false;
        };

        /// Exo 2's capitals stand 690 units above the baseline, and ImGui's font size spans its ascent to its descent (999 +
        /// 201 units): the capitals are this share of the size.
        constexpr float CapHeight = 690.f / 1200.f;

        /// How a line of text is set, letter by letter.
        struct LineStyle
        {
            ImFont* Font = nullptr;
            /// ImGui's size and the space between letters, for 1080 lines.
            float Size = 0;
            float Spacing = 0;
            /// The capitals' height as a share of Size.
            float Caps = CapHeight;
            /// ImGui has no kerning: the font's pairs, in EM, and ImGui's size for one EM.
            std::span<GameFont::KerningPair const> Kerning;
            float SizePerEm = 1;
            /// The font's name in the log.
            char const* Name = "Exo 2";
        };

        /// The lines of a game's screen: in Exo 2 until the game's fonts come (TakeGameFonts).
        struct NativeStyles
        {
            LineStyle Title;
            LineStyle Label;
            LineStyle Key;
        };

        // ! The games' looks
        // ========================================

        struct Rgb
        {
            int R, G, B;
        };

        /// A line as the game's movies set it, at 1080 lines (their 720-line stage times 1.5): in its bold font or its light one.
        struct GameLine
        {
            bool Bold;
            float Em;
            float Spacing;
        };

        /// Where a gamepad button's disc lies in its square texture, in texels.
        struct ButtonArt
        {
            float Size;
            float CentreX;
            float CentreY;
            float Disc;
        };

        /// What a game's pause screen takes from its menus. Colours and sizes read from the game's files; nothing of them is
        /// shipped.
        struct NativeLook
        {
            char const* Name;
            Rgb Title;
            Rgb Label;
            /// The band's rules.
            Rgb Line;
            Rgb PanelTop;
            Rgb PanelBottom;
            /// A cast over the blurred frame, toward the game's menus.
            Blur::Settings Blur;
            GameLine TitleLine;
            GameLine LabelLine;
            GameLine KeyLine;
            /// The height of the game's fonts' H, in EM: what CapsOf falls back on when a font gives none it can trust.
            float LightCaps;
            float BoldCaps;
            ButtonArt Button;
        };

        // LE1's menus (the GUI_SF movies): titles #CCFFFF, labels and rules #3199C6, panels a gradient from #174868 to #226E8F at
        // about half opacity. The title as the paused box of WindowPop sets it, in $AeroLight (Tigensu_jpn) at 45; the labels as
        // PC_MainWheel sets these two strings beside its B and Y buttons, in $BioMass (Biomass_jpn) at 15.21, with the 0.585 of
        // its Resume field for both (its Photo Mode field states none); a key's name as the key bindings of PCOptions set it, in
        // $AeroLight at 12 with a spacing of 0.5.
        NativeLook const Le1Look{ .Name = "LE1",
                                  .Title = { 0xCC, 0xFF, 0xFF },
                                  .Label = { 0x31, 0x99, 0xC6 },
                                  .Line = { 0x31, 0x99, 0xC6 },
                                  .PanelTop = { 0x17, 0x48, 0x68 },
                                  .PanelBottom = { 0x22, 0x6E, 0x8F },
                                  .Blur = { .Tint = { 0.88f, 0.97f, 1.06f } },
                                  .TitleLine = { false, 67.5f, 0 },
                                  .LabelLine = { true, 22.8f, 0.9f },
                                  .KeyLine = { false, 18.f, 0.75f },
                                  .LightCaps = 0.8018f,
                                  .BoldCaps = 0.8003f,
                                  .Button = { 128, 64, 64, 95 } };

        // LE2's pause wheel (PC_MainWheel): its title #FFCC66, the labels of its B and Y fields #FF9142, in $BioMass (here
        // Biomass+DFGSoGei-W7), and its bands: on screen brown from #5F2C0C to #843E14 with an edge of #FF9C5B. The paused box
        // of WindowPop is LE1's, left behind with no string: the title takes the wheel's font instead, at 1.5 times its 33, with
        // its spacing. The labels as in LE1, which LE2's fields repeat. The keys in the light font, here Myriad: LE2 has no
        // key bindings screen to copy, so at LE1's size.
        NativeLook const Le2Look{ .Name = "LE2",
                                  .Title = { 0xFF, 0xCC, 0x66 },
                                  .Label = { 0xFF, 0x91, 0x42 },
                                  .Line = { 0xFF, 0x9C, 0x5B },
                                  .PanelTop = { 0x5F, 0x2C, 0x0C },
                                  .PanelBottom = { 0x84, 0x3E, 0x14 },
                                  .Blur = { .Tint = { 1.06f, 0.97f, 0.88f } },
                                  .TitleLine = { true, 49.5f, 1.5f },
                                  .LabelLine = { true, 22.8f, 0.9f },
                                  .KeyLine = { false, 18.f, 0.75f },
                                  .LightCaps = 0.6743f,
                                  .BoldCaps = 0.8003f,
                                  .Button = { 128, 61, 55.5f, 93 } };

        // LE3's pause wheel (MainWheel): its title #FFFFFF in $BioMass (Biomass+DFGSoGei-W7 here too), at 1.5 times its 42 as in
        // LE2, its buttons' words #ACF5FE, its bars from #08759C to #084E7E, the edge of its bar PC_SharedAssets_I46 #0896F7. The
        // labels and keys keep LE2's fonts and sizes, in the colour of its buttons; its own prompts are $AeroLight at 30, #FFBA65.
        NativeLook const Le3Look{ .Name = "LE3",
                                  .Title = { 0xFF, 0xFF, 0xFF },
                                  .Label = { 0xAC, 0xF5, 0xFE },
                                  .Line = { 0x08, 0x96, 0xF7 },
                                  .PanelTop = { 0x08, 0x75, 0x9C },
                                  .PanelBottom = { 0x08, 0x4E, 0x7E },
                                  .Blur = { .Tint = { 0.88f, 0.97f, 1.06f } },
                                  .TitleLine = { true, 63.f, 1.5f },
                                  .LabelLine = { true, 22.8f, 0.9f },
                                  .KeyLine = { false, 18.f, 0.75f },
                                  .LightCaps = 0.6743f,
                                  .BoldCaps = 0.8003f,
                                  .Button = { 256, 128, 128, 108 } };

        NativeLook const& NativeLookOf(OverlayLook look)
        {
            if (look == OverlayLook::LE2) return Le2Look;
            if (look == OverlayLook::LE3) return Le3Look;
            return Le1Look;
        }

        /// The overlay's life: no swap chain taken yet (each present is a candidate, Start), drawing on the game's, or off for
        /// the session after a failure (the Canvas banner instead). Changed only by SetPhase.
        enum class Phase : unsigned char { Waiting, Started, Off };

        /// What the words drawn are judged from: the words picked (UseTexts), each style's font, and the language.
        struct WordsKey
        {
            TextRules::Picks const* Picked = nullptr;
            ImFont const* Title = nullptr;
            ImFont const* Label = nullptr;
            ImFont const* Key = nullptr;
            TextRules::Language Language = TextRules::Language::English;
            bool operator==(WordsKey const&) const = default;
        };

        struct RenderState
        {
            Phase At = Phase::Waiting;
            /// While Waiting: the presents Start refused, and GetTickCount64 at the first of them once the game thread was known,
            /// from which StartAnywayMs counts (OverlayRules::Judge).
            OverlayRules::StartClock Clock;
            ID3D11Device* Device = nullptr;
            ID3D11DeviceContext* Context = nullptr;
            /// Identity only: no reference is held through it.
            IDXGISwapChain* GameChain = nullptr;
            ULONGLONG GameChainSeen = 0;
            /// The window of the swap chain we draw on. A swap chain on it or on another window IsGameWindow takes may be the
            /// game's new one, and for an overlay taken anyway one on the foreground window once this one is gone (Adopt).
            HWND GameWindow = nullptr;
            /// How the overlay came to the swap chain it draws on, set by Start and Adopt: Game, by IsGameWindow, or Anyway
            /// (OverlayRules::Judge, Readopt).
            OverlayRules::Take Took = OverlayRules::Take::Game;
            bool ForeignLogged = false;
            ID3D11Texture2D* Back = nullptr;
            ID3D11RenderTargetView* Target = nullptr;
            LARGE_INTEGER LastFrame{};
            float Fade = 0;
            /// What the screen shows, kept while it fades out.
            bool Photo = false;
            bool WasShown = false;
            bool BlurLogged = false;
            unsigned DrawFailures = 0;
            /// While a fullscreen movie plays: the game's last frame, copied before anything is drawn on it. A paused movie
            /// presents nothing, so MovieIdle presents this frame with the pause screen over it.
            ID3D11Texture2D* Clean = nullptr;
            /// The size and format no copy could be made for (CreateTexture2D failed): not tried again at every frame, only
            /// once they change or at the next movie (ReleaseMovie).
            D3D11_TEXTURE2D_DESC CleanRefused{};
            bool HaveCleanRefused = false;
            /// The swap chain MovieIdle presents to: referenced in one of the game's own presents to it while its frames are
            /// copied into Clean, so that it is still alive when the movie pauses and the game stops presenting. Held for the
            /// whole movie (see Frame). Dropped with the copy, once the game presents with no such movie, or when another
            /// swap chain is taken: then the game may replace its swap chain freely.
            IDXGISwapChain* MovieChain = nullptr;
            /// The game's last own present (not MovieIdle's).
            LARGE_INTEGER LastGamePresent{};
            bool IdleLogged = false;
            bool CleanLogged = false;
            /// Set when the overlay starts, before any draw.
            NativeLook const* Native = nullptr;
            /// The launcher's language, set when the overlay starts.
            TextRules::Language Language = TextRules::Language::English;
            /// The words drawn (TakeTexts): of TextRules::ScreenWords, each the first its font draws of the word picked, the
            /// language's built-in text and the English one. Judged again whenever what they were judged from changes
            /// (WordsKey); none before the first judging.
            std::array<std::string, TextRules::WordCount> Words;
            std::optional<WordsKey> WordsFrom;
            /// Per word, the fallback last logged, so that each is logged once.
            std::array<std::string, TextRules::WordCount> WordsLogged;
            /// The game's gamepad buttons, referenced from when they passed TakeView: both or neither.
            ID3D11ShaderResourceView* IconA = nullptr;
            ID3D11ShaderResourceView* IconY = nullptr;
            bool GameIconsSeen = false;
            bool GameFontsSeen = false;
            Fonts Font;
            NativeStyles Style;
        };
        /// Leaked on purpose: no D3D Release from DllMain at process exit, after d3d11.dll may have cleaned up. Used only by the
        /// gate's holder (see The gate).
        RenderState& R = *new RenderState{};
        thread_local bool InPresent = false;
        /// MovieIdle's own present: the back buffer holds Clean already.
        thread_local bool SelfPresent = false;
        /// The swap chain whose present on this thread came back DXGI_ERROR_WAS_STILL_DRAWING (DXGI_PRESENT_DO_NOT_WAIT):
        /// nothing was shown and its back buffer holds our drawing already, so the present that retries it is not drawn on
        /// again (the screen would be blurred and darkened twice). Cleared by that chain's next present with any other result.
        thread_local IDXGISwapChain* NotShown = nullptr;
        /// The thread that presents the game's frames: the only one that may use the device's immediate context with us.
        std::atomic<DWORD> PresentThread{ 0 };
        /// The swap chain of the latest present of all but MovieIdle's, whoever made it and whether or not it found the gate
        /// free: PrepareIdle's "nothing presented elsewhere since". Identity only.
        std::atomic<IDXGISwapChain*> LastChain{ nullptr };
        /// The process's first thread (FindMainThread, at Install), 0 if it couldn't be read.
        std::atomic<DWORD> MainThread{ 0 };
        /// R.GameChain while the overlay is Started, null otherwise and while Adopt changes it, and R.GameChainSeen: for the
        /// threads that ask without the gate whether a present has anything for us (PassesBy) or a resize must let go of our
        /// views (EnterWaiting). Identity only. DrawnSeen's release store publishes the DrawnChain it was stored for.
        std::atomic<IDXGISwapChain*> DrawnChain{ nullptr };
        std::atomic<ULONGLONG> DrawnSeen{ 0 };
        /// The resizes waiting for the gate (EnterWaiting): MovieIdle takes none meanwhile (OverlayRules::EnterIdle).
        std::atomic<unsigned> ResizesWaiting{ 0 };

        void SetPhase(Phase phase)
        {
            R.At = phase;
            DrawnChain.store(phase == Phase::Started ? R.GameChain : nullptr, std::memory_order_relaxed);
            Live.store(phase == Phase::Started, std::memory_order_release);
        }

        // ! The gate
        // ========================================

        // R, ImGui's context, the blur and our use of the device's immediate context belong to one thread at a time, the one
        // GateOwner names (OverlayRules' gate, which its tests run on real threads). Our work runs on whichever thread calls
        // us (the game's present thread, a movie's thread in MovieIdle, a thread that resizes, any other that presents), so
        // each call takes the gate first. A present never waits for it: one that finds it held passes through untouched, a
        // frame without the pause screen at worst. Only a resize waits, for the present in flight at most (EnterWaiting). A
        // holder that keeps the gate across an original call (MovieIdle's present, a resize of the swap chain we draw on)
        // marks itself away for that call (GoAway), and only from such a holder may a resize take the gate. Not a
        // std::mutex: the frames that take the gate have __try blocks and hold no C++ object, so it is given back in their
        // __finally blocks, which run on every way out, an exception unwinding through them included; a fault that Guarded
        // takes returns through them as usual. Taken per call, never pinned to a thread: the engine may start its render
        // thread again (MovieIdle).
        std::atomic<OverlayRules::Word> GateOwner{ 0 };

        using OverlayRules::Gate;

        std::uint32_t Self() { return static_cast<std::uint32_t>(GetCurrentThreadId()); }

        Gate Enter() { return OverlayRules::Enter(GateOwner, Self()); }

        /// Enter for a resize of <paramref name="chain"/> (OverlayRules::EnterWaiting): MovieIdle takes no gate while it
        /// waits, so the wait ends with the present in flight. A holder found away at every try for ResizeWaitMs is stuck in
        /// a call we don't control (a full-screen present that waits on this thread's window, a driver), and the gate is
        /// taken from it if this is the swap chain we draw on (see BeforeResize). One in our own work is waited for, up to
        /// ResizeWaitOwnMs.
        OverlayRules::Waited EnterWaiting(IDXGISwapChain* chain)
        {
            return OverlayRules::EnterWaiting(
                GateOwner, ResizesWaiting, Self(), ResizeWaitMs, ResizeWaitOwnMs, [] { return GetTickCount64(); },
                [](unsigned tries) { Sleep(tries < 64 ? 0 : 1); }, [chain] { return chain == DrawnChain.load(std::memory_order_acquire); });
        }

        /// Gives the gate back if this call took it and this thread still holds it: a second Leave, after an early one, does
        /// nothing. A Reclaimed gate goes back to the away call further up this thread's stack.
        void Leave(Gate gate) { OverlayRules::Leave(GateOwner, gate, Self()); }

        /// Before an original call this frame makes with a gate it took or reclaimed, after which it does nothing more with
        /// R (OverlayRules::GoAway). True if marked away: GoneBack is then due on every way out of the call.
        bool GoAway(Gate gate) { return (gate == Gate::Taken || gate == Gate::Reclaimed) && OverlayRules::GoAway(GateOwner, Self()); }

        /// Back from that call: false if a resize took the gate meanwhile, and R is that resize's.
        bool GoneBack() { return OverlayRules::ComeBack(GateOwner, Self()); }

        /// A fault in MovieIdle's present, made away (GuardedSelfPresent), turns the overlay off: at once if the gate is still
        /// this thread's, else at the next call that takes it (Frame, PrepareIdle), since R is then a resize's.
        std::atomic<bool> OffWanted{ false };

        std::atomic<bool> BusyPresentLogged{ false };
        std::atomic<bool> BusyIdleLogged{ false };

        void LogBusy(std::atomic<bool>& logged, char const* what)
        {
            if (logged.exchange(true, std::memory_order_relaxed)) return;
            auto const word = GateOwner.load(std::memory_order_relaxed);
            LEASI_INFO("overlay: {} on thread {} found the gate held by thread {}{} (drawing, presenting a paused movie or resizing; 0: a resize waits for "
                       "it): skipped (logged once)",
                       what, GetCurrentThreadId(), OverlayRules::HolderOf(word), OverlayRules::IsAway(word) ? ", away in a present or resize" : "");
        }

        // ! The game's window
        // ========================================

        /// The process's main thread: of its threads, the one made first (the earliest creation time; a tie goes to the one
        /// Toolhelp lists first). 0 if none could be read.
        DWORD FindMainThread()
        {
            auto const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE) return 0;
            auto const process = GetCurrentProcessId();
            DWORD found = 0;
            ULONGLONG earliest = ~0ull;
            THREADENTRY32 entry{};
            entry.dwSize = sizeof entry;
            for (auto more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
            {
                auto const owned = entry.dwSize >= offsetof(THREADENTRY32, th32OwnerProcessID) + sizeof entry.th32OwnerProcessID;
                auto const id = entry.th32ThreadID;
                entry.dwSize = sizeof entry;
                if (!owned || entry.th32OwnerProcessID != process) continue;
                auto const thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
                if (thread == nullptr) continue;
                FILETIME created{}, exited{}, kernel{}, user{};
                if (GetThreadTimes(thread, &created, &exited, &kernel, &user))
                {
                    auto const at = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
                    if (at < earliest)
                    {
                        earliest = at;
                        found = id;
                    }
                }
                CloseHandle(thread);
            }
            CloseHandle(snapshot);
            return found;
        }

        /// Who made the window: whether it is this process's (none is for a null window), and the thread that made it.
        struct Maker
        {
            bool Ours = false;
            DWORD Thread = 0;
        };

        Maker MakerOf(HWND window)
        {
            DWORD process = 0;
            auto const thread = window != nullptr ? GetWindowThreadProcessId(window, &process) : 0;
            return { process == GetCurrentProcessId(), thread };
        }

        /// The game's window, by OverlayRules::IsGameWindow: one made by the process's main thread, or by the game thread once
        /// Pause.cpp knows it. The start line logs the window's maker beside both threads, so a run shows whether the rule
        /// holds. It needs nothing of Start's, so a swap chain is judged before the overlay starts.
        bool IsGameWindow(HWND window)
        {
            auto const maker = MakerOf(window);
            return OverlayRules::IsGameWindow(maker.Ours, static_cast<std::uint32_t>(maker.Thread), static_cast<std::uint32_t>(MainThread.load(std::memory_order_relaxed)),
                                              static_cast<std::uint32_t>(GameThread()));
        }

        // ! Fonts
        // ========================================

        /// One of the fonts compiled into the ASI (PauseAnytime.rc), or null.
        ImFont* AddEmbedded(int id)
        {
            static int const anchor = 0;
            HMODULE self = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&anchor),
                                    &self))
                return nullptr;
            auto const info = FindResourceW(self, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
            auto const loaded = info != nullptr ? LoadResource(self, info) : nullptr;
            auto const bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
            auto const size = info != nullptr ? SizeofResource(self, info) : 0;
            if (bytes == nullptr || size == 0) return nullptr;
            ImFontConfig config;
            // The resource lives as long as the ASI: the atlas must not free it.
            config.FontDataOwnedByAtlas = false;
            return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<void*>(bytes), static_cast<int>(size), 32.f, &config);
        }

        void LoadFonts()
        {
            R.Font.Regular = AddEmbedded(PAUSEANYTIME_FONT_REGULAR);
            R.Font.SemiBold = AddEmbedded(PAUSEANYTIME_FONT_SEMIBOLD);
            R.Font.Embedded = R.Font.Regular != nullptr && R.Font.SemiBold != nullptr;
            if (!R.Font.Embedded)
            {
                LEASI_WARN("overlay: the embedded Exo 2 fonts did not load: ImGui's default font instead");
                auto const fallback = ImGui::GetIO().Fonts->AddFontDefault();
                R.Font = { fallback, fallback, false };
            }
            R.Style = { { R.Font.Regular, 58, 18 }, { R.Font.Regular, 28, 0 }, { R.Font.SemiBold, 19.55f, 0 } };
            if (!R.Font.Embedded)
                for (auto* style : { &R.Style.Title, &R.Style.Label, &R.Style.Key }) style->Name = "ImGui default";
        }

        /// The font has every letter of a text that has some.
        bool Drawable(ImFont* font, std::string_view text)
        {
            if (text.empty()) return false;
            for (auto p = text.data(), end = text.data() + text.size(); p < end;)
            {
                unsigned c = 0;
                p += ImTextCharFromUtf8(&c, p, end);
                if (c < 0x20 || c > IM_UNICODE_CODEPOINT_MAX || !font->IsGlyphInFont(static_cast<ImWchar>(c))) return false;
            }
            return true;
        }

        /// The style a word of the screen is set in: the title's, the labels' (resume, photo mode) or the keys' (space).
        LineStyle const& StyleOf(NativeStyles const& styles, TextRules::Word word)
        {
            if (word == TextRules::Word::Title) return styles.Title;
            return word == TextRules::Word::Space ? styles.Key : styles.Label;
        }

        /// Between ImGui's frames, after TakeGameFonts: each word of the screen in the first text its font draws, of the word
        /// UseTexts gave (before it, the built-in text of the language), the language's built-in text and the English one.
        /// A fallback is logged once per word.
        void TakeTexts()
        {
            using namespace TextRules;
            auto const picked = PickedTexts.load(std::memory_order_acquire);
            WordsKey const key{ picked, R.Style.Title.Font, R.Style.Label.Font, R.Style.Key.Font, R.Language };
            if (R.WordsFrom == key) return;
            R.WordsFrom = key;
            for (auto const word : ScreenWords)
            {
                auto const i = IndexOf(word);
                auto const& style = StyleOf(R.Style, word);
                Pick const candidates[] = { picked != nullptr ? (*picked)[i] : BuiltInPick(R.Language, word), BuiltInPick(R.Language, word),
                                            BuiltInPick(Language::English, word) };
                auto chosen = &candidates[2];
                for (auto const& candidate : candidates)
                {
                    if (!Drawable(style.Font, candidate.Text)) continue;
                    chosen = &candidate;
                    break;
                }
                R.Words[i] = chosen->Text;
                if (chosen == &candidates[0])
                {
                    R.WordsLogged[i].clear();
                    continue;
                }
                auto const fallback = std::format("{} \"{}\"", Describe(*chosen), chosen->Text);
                if (fallback == R.WordsLogged[i]) continue;
                R.WordsLogged[i] = fallback;
                LEASI_WARN("overlay: {} \"{}\" ({}) has letters the {} font lacks: {} instead", NameOf(word), candidates[0].Text, Describe(candidates[0]),
                           style.Name, fallback);
            }
        }

        /// Null if ImGui can't read it.
        ImFont* AddGameFont(GameFontFile const& file)
        {
            ImFontConfig config;
            // Kept for good, as GameFonts says: the atlas must not free it.
            config.FontDataOwnedByAtlas = false;
            return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(file.Ttf.data()), static_cast<int>(file.Ttf.size()), 32.f, &config);
        }

        /// The height of the font's H in EM: its own, since a font mod may redraw the capitals (Myriad Pro Begone puts LE2's and
        /// LE3's light font's H 0.13 EM higher), else the look's. Only a height from 0.5 to 1 EM is taken, so an H that is
        /// missing or wild falls back to the look; a stray point inside that range still moves the line.
        float CapsOf(GameFontFile const& file, float look)
        {
            auto const measured = file.Layout.CapsEm;
            return measured >= 0.5f && measured <= 1.f ? measured : look;
        }

        LineStyle GameStyle(ImFont* font, GameFontFile const& file, GameLine const& line, float caps)
        {
            auto const perEm = file.Layout.SizePerEm;
            return { font, line.Em * perEm, line.Spacing, caps / perEm, file.Layout.Kerning, perEm, line.Bold ? "game's bold" : "game's light" };
        }

        /// The screen's words a set of styles draws in the language: the word picked (UseTexts; before it, the language's
        /// built-in text) or the language's built-in text, not the English stand-in.
        unsigned WordsInLanguage(NativeStyles const& styles, TextRules::Picks const* picked)
        {
            unsigned count = 0;
            for (auto const word : TextRules::ScreenWords)
            {
                auto const font = StyleOf(styles, word).Font;
                if ((picked != nullptr && Drawable(font, (*picked)[TextRules::IndexOf(word)].Text)) || Drawable(font, TextRules::BuiltInText(R.Language, word)))
                    count++;
            }
            return count;
        }

        /// Once UseGameFonts gave them: the game's fonts in place of Exo 2, both or neither. They must draw "F7" and the
        /// English built-in text, the last stand-in for a word (TakeTexts), and at least as many of the screen's words in the
        /// language as Exo 2 does. So they are taken even where they lack a letter of a Japanese word, as Exo 2 has no
        /// Japanese: that word alone falls back. UseTexts came first, on the game thread. Between ImGui's frames, and ahead of
        /// TakeTexts.
        void TakeGameFonts()
        {
            if (R.GameFontsSeen) return;
            auto const game = GameFontFiles.load(std::memory_order_acquire);
            if (game == nullptr) return;
            R.GameFontsSeen = true;
            auto const light = AddGameFont(game->Light);
            auto const bold = AddGameFont(game->Bold);
            if (light == nullptr || bold == nullptr)
            {
                LEASI_WARN("overlay: ImGui did not read the game's fonts: Exo 2 stays");
                return;
            }
            auto const& look = *R.Native;
            auto const style = [&](GameLine const& line) {
                return line.Bold ? GameStyle(bold, game->Bold, line, CapsOf(game->Bold, look.BoldCaps))
                                 : GameStyle(light, game->Light, line, CapsOf(game->Light, look.LightCaps));
            };
            NativeStyles const styles{ style(look.TitleLine), style(look.LabelLine), style(look.KeyLine) };
            auto english = Drawable(styles.Key.Font, "F7");
            for (auto const word : TextRules::ScreenWords)
                english = english && Drawable(StyleOf(styles, word).Font, TextRules::BuiltInText(TextRules::Language::English, word));
            if (!english)
            {
                LEASI_WARN("overlay: the game's fonts lack letters of the pause screen's English built-in text or F7: Exo 2 stays");
                return;
            }
            auto const picked = PickedTexts.load(std::memory_order_acquire);
            auto const theirs = WordsInLanguage(styles, picked);
            auto const exo = WordsInLanguage(R.Style, picked);
            if (theirs < exo)
            {
                LEASI_WARN("overlay: the game's fonts draw {} of the pause screen's {} words in {}, and {} draws {}: {} stays", theirs,
                           TextRules::ScreenWords.size(), TextRules::NameOf(R.Language), R.Style.Title.Name, exo, R.Style.Title.Name);
                return;
            }
            // The words are judged again, by these fonts (TakeTexts' WordsKey).
            R.Style = styles;
            LEASI_INFO("overlay: the game's fonts: {} and {} bytes of TrueType, {} and {} kerning pairs, H measured {:.5f} and {:.5f} EM: used",
                       game->Light.Ttf.size(), game->Bold.Ttf.size(), game->Light.Layout.Kerning.size(), game->Bold.Layout.Kerning.size(),
                       game->Light.Layout.CapsEm, game->Bold.Layout.CapsEm);
        }

        // ! The game's icons
        // ========================================

        /// The first word of what may not be an object, or null where it can't be read. No C++ object may live in this frame.
        void const* TableOf(void const* object)
        {
            __try
            {
                return *static_cast<void const* const*>(object);
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                return nullptr;
            }
        }

        /// What a view of ours shows: its method table and the device it reports.
        struct ViewShape
        {
            void const* Table = nullptr;
            ID3D11Device const* Device = nullptr;
        };

        /// A view of our own on a one-pixel texture, read and let go; an empty shape without one. Any overlay or driver in
        /// between shows here as it shows on the game's views. That includes the device: a layer such as ReShade gives its
        /// own device from the swap chain (R.Device), while a view, ours or the game's, may report the device beneath it. Our
        /// views and the game's are made through the same device, so they report the same one.
        ViewShape OwnViewShape()
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 1;
            desc.Height = 1;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            ID3D11Texture2D* texture = nullptr;
            if (FAILED(R.Device->CreateTexture2D(&desc, nullptr, &texture)) || texture == nullptr) return {};
            ID3D11ShaderResourceView* view = nullptr;
            auto const hr = R.Device->CreateShaderResourceView(texture, nullptr, &view);
            texture->Release();
            if (FAILED(hr) || view == nullptr) return {};
            ViewShape shape;
            shape.Table = TableOf(view);
            ID3D11Device* device = nullptr;
            view->GetDevice(&device);
            if (device != nullptr) device->Release();
            shape.Device = device;
            view->Release();
            return shape;
        }

        /// Why TakeView gave nothing.
        enum class Refusal : unsigned char { None, NoOwnView, NotAView, OtherDevice, NotFlat };

        char const* RefusalText(Refusal why)
        {
            switch (why)
            {
            case Refusal::None: return "fine";
            case Refusal::NoOwnView: return "refused (no view of our own to compare with)";
            case Refusal::NotAView: return "refused (not a view as ours are)";
            case Refusal::OtherDevice: return "refused (on another device than our views)";
            case Refusal::NotFlat: return "refused (not of a 2D texture)";
            }
            return "refused";
        }

        /// The game's pointer, referenced, if it is a view as ours are (OwnViewShape: same method table, same device
        /// reported), of a 2D texture. Nothing of it is called before its method table matched.
        ID3D11ShaderResourceView* TakeView(void* candidate, ViewShape const& own, Refusal& why)
        {
            why = Refusal::NoOwnView;
            if (own.Table == nullptr || own.Device == nullptr) return nullptr;
            why = Refusal::NotAView;
            if (candidate == nullptr || TableOf(candidate) != own.Table) return nullptr;
            auto const view = static_cast<ID3D11ShaderResourceView*>(candidate);
            ID3D11Device* device = nullptr;
            view->GetDevice(&device);
            if (device != nullptr) device->Release();
            if (device != own.Device)
            {
                why = Refusal::OtherDevice;
                return nullptr;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
            view->GetDesc(&desc);
            if (desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
            {
                why = Refusal::NotFlat;
                return nullptr;
            }
            why = Refusal::None;
            view->AddRef();
            return view;
        }

        void ReleaseGameIcons()
        {
            if (R.IconA != nullptr) R.IconA->Release();
            if (R.IconY != nullptr) R.IconY->Release();
            R.IconA = nullptr;
            R.IconY = nullptr;
        }

        /// Once UseGameIcons gave them: the game's buttons in place of the drawn ones, both or neither. A view stays ours from
        /// here on, whatever the game does with its texture.
        void TakeGameIcons()
        {
            auto const game = GameIcon.load(std::memory_order_acquire);
            if (game == nullptr) return;
            R.GameIconsSeen = true;
            auto const own = OwnViewShape();
            if (own.Device != nullptr && own.Device != R.Device)
                LEASI_INFO("overlay: our views report device {}, the swap chain gave device {}: a layer sits between the game and "
                           "D3D11 (ReShade 6.8 does)",
                           static_cast<void const*>(own.Device), static_cast<void const*>(R.Device));
            auto whyA = Refusal::NotAView;
            auto whyY = Refusal::NotAView;
            R.IconA = TakeView(game->PadA, own, whyA);
            R.IconY = TakeView(game->PadY, own, whyY);
            auto const a = R.IconA != nullptr;
            auto const y = R.IconY != nullptr;
            if (!a || !y) ReleaseGameIcons();
            LEASI_INFO("overlay: the game's gamepad icons: A {}, Y {}: {}", RefusalText(whyA), RefusalText(whyY), a && y ? "used" : "ours drawn instead");
        }

        /// A fault in there costs the icons, not the overlay. No C++ object may live in this frame.
        bool GuardedGameIcons()
        {
            __try
            {
                TakeGameIcons();
                return true;
            }
            __except (Fault::Filter(GetExceptionInformation()))
            {
                Fault::Log("overlay, the game's gamepad icons");
                return false;
            }
        }

        // ! Drawing
        // ========================================

        ImU32 Colour(int r, int g, int b, float alpha)
        {
            return IM_COL32(r, g, b, static_cast<int>(std::clamp(alpha, 0.f, 1.f) * 255.f + 0.5f));
        }

        /// Where a line's top goes for its capitals to centre on y.
        float CapsTop(ImFont* font, float size, float y, float caps = CapHeight)
        {
            return y - font->GetFontBaked(size)->Ascent + 0.5f * caps * size;
        }

        float CapsTop(LineStyle const& style, float k, float y) { return CapsTop(style.Font, style.Size * k, y, style.Caps); }

        /// What the font's kerning adds between two letters, at that size.
        float KerningOf(LineStyle const& style, float size, unsigned left, unsigned right)
        {
            for (auto const& pair : style.Kerning)
            {
                if (pair.Left == left && pair.Right == right) return pair.Em * size / style.SizePerEm;
            }
            return 0;
        }

        /// A line with its style's space between its letters and its kerning. ImGui bakes
        /// whole sizes: an advance scales to the size asked for, as in ImGui's own text.
        float SpacedWidth(LineStyle const& style, float k, std::string_view text)
        {
            auto const size = style.Size * k;
            auto const baked = style.Font->GetFontBaked(size);
            auto const scale = size / baked->Size;
            float width = 0;
            int letters = 0;
            unsigned left = 0;
            for (auto p = text.data(), end = text.data() + text.size(); p < end;)
            {
                unsigned c = 0;
                p += ImTextCharFromUtf8(&c, p, end);
                width += baked->GetCharAdvance(static_cast<ImWchar>(c)) * scale + KerningOf(style, size, left, c);
                left = c;
                letters++;
            }
            return width + style.Spacing * k * static_cast<float>(std::max(letters - 1, 0));
        }

        void DrawSpaced(ImDrawList* list, LineStyle const& style, float k, ImVec2 at, ImU32 colour, std::string_view text)
        {
            auto const size = style.Size * k;
            auto const baked = style.Font->GetFontBaked(size);
            auto const scale = size / baked->Size;
            unsigned left = 0;
            for (auto p = text.data(), end = text.data() + text.size(); p < end;)
            {
                unsigned c = 0;
                auto const next = p + ImTextCharFromUtf8(&c, p, end);
                at.x += KerningOf(style, size, left, c);
                list->AddText(style.Font, size, at, colour, p, next);
                at.x += baked->GetCharAdvance(static_cast<ImWchar>(c)) * scale + style.Spacing * k;
                left = c;
                p = next;
            }
        }

        enum class Icon { PadA, PadY, KeySpace, KeyF7 };

        bool IsPad(Icon icon) { return icon == Icon::PadA || icon == Icon::PadY; }

        std::string_view KeyLabel(Icon icon) { return icon == Icon::KeySpace ? std::string_view{ R.Words[TextRules::IndexOf(TextRules::Word::Space)] } : "F7"; }

        float IconWidth(Icon icon, float radius, float k)
        {
            if (IsPad(icon)) return 2 * radius;
            auto const textWidth = SpacedWidth(R.Style.Key, k, KeyLabel(icon));
            // The space bar reads as one: wider than a key, whatever its label.
            return std::max(icon == Icon::KeySpace ? 5 * radius : 2 * radius, textWidth + 22 * k);
        }

        struct Prompt
        {
            Icon What;
            std::string_view Label;
        };

        // ! A game's look
        // ========================================

        // The gamepad's buttons are the game's own textures, drawn from its own views of them (UseGameIcons), or without those
        // drawn to look like them (a disc in the button's colour, a dark rim, a dark letter). The keys are drawn as
        // GUI_SF_PhotoMode's translucent grey keycaps, the same in LE1 and LE2.

        constexpr Rgb DiscA{ 0x7B, 0x9E, 0x3F };
        constexpr Rgb DiscY{ 0xFC, 0xD3, 0x05 };
        constexpr Rgb KeyTop{ 0x47, 0x49, 0x47 };
        constexpr Rgb KeyBottom{ 0x9C, 0x9A, 0x9C };

        ImU32 Colour(Rgb c, float alpha, float shade = 1)
        {
            return Colour(static_cast<int>(static_cast<float>(c.R) * shade), static_cast<int>(static_cast<float>(c.G) * shade),
                          static_cast<int>(static_cast<float>(c.B) * shade), alpha);
        }

        /// A strip coloured from top to bottom, its opacity rising from 0 over <paramref name="fade"/> at each end, as the
        /// game's menu bars fade out.
        void FadedStrip(ImDrawList* list, ImVec2 min, ImVec2 max, float fade, Rgb top, Rgb bottom, float alpha)
        {
            auto const inner = std::min(fade, (max.x - min.x) * 0.5f);
            auto const solidTop = Colour(top, alpha);
            auto const solidBottom = Colour(bottom, alpha);
            auto const clearTop = Colour(top, 0);
            auto const clearBottom = Colour(bottom, 0);
            // The corners: upper left, upper right, bottom right, bottom left.
            list->AddRectFilledMultiColor(min, { min.x + inner, max.y }, clearTop, solidTop, solidBottom, clearBottom);
            list->AddRectFilledMultiColor({ min.x + inner, min.y }, { max.x - inner, max.y }, solidTop, solidTop, solidBottom, solidBottom);
            list->AddRectFilledMultiColor({ max.x - inner, min.y }, max, solidTop, clearTop, clearBottom, solidBottom);
        }

        /// The centre: the title on a band like the game's menu bars, a rule along each edge.
        void DrawBandTitle(ImDrawList* list, float width, float height, float alpha, float k)
        {
            auto const& look = *R.Native;
            auto const& style = R.Style.Title;
            std::string_view const text = R.Words[TextRules::IndexOf(TextRules::Word::Title)];
            auto const textWidth = SpacedWidth(style, k, text);
            ImVec2 const centre{ width * 0.5f, height * 0.47f };

            auto const halfWidth = std::min(textWidth * 0.5f + 150 * k, width * 0.5f - 48 * k);
            auto const halfHeight = 62 * k;
            ImVec2 const min{ centre.x - halfWidth, centre.y - halfHeight };
            ImVec2 const max{ centre.x + halfWidth, centre.y + halfHeight };
            auto const fade = 120 * k;
            auto const rule = std::max(1.f, 2 * k);
            FadedStrip(list, min, max, fade, look.PanelTop, look.PanelBottom, 0.55f * alpha);
            FadedStrip(list, min, { max.x, min.y + rule }, fade, look.Line, look.Line, 0.95f * alpha);
            FadedStrip(list, { min.x, max.y - rule }, max, fade, look.Line, look.Line, 0.95f * alpha);

            ImVec2 const at{ centre.x - textWidth * 0.5f, CapsTop(style, k, centre.y) };
            DrawSpaced(list, style, k, { at.x + 3 * k, at.y + 4 * k }, Colour(0, 0, 0, 0.25f * alpha), text);
            DrawSpaced(list, style, k, { at.x + 2 * k, at.y + 3 * k }, Colour(0, 0, 0, 0.4f * alpha), text);
            DrawSpaced(list, style, k, at, Colour(look.Title, alpha), text);
        }

        /// A key in the game's style, centred on c: a gamepad button (the game's texture of it where we have one, else a disc
        /// in its colour, darker toward a black rim, its letter dark), or a keycap (translucent grey as the art is stored, lighter
        /// toward the bottom).
        void DrawGameIcon(ImDrawList* list, Icon icon, ImVec2 c, float radius, float alpha, float k)
        {
            if (IsPad(icon))
            {
                auto const texture = icon == Icon::PadA ? R.IconA : R.IconY;
                if (texture != nullptr)
                {
                    auto const& art = R.Native->Button;
                    auto const texel = 2 * radius / art.Disc;
                    ImVec2 const min{ c.x - art.CentreX * texel, c.y - art.CentreY * texel };
                    list->AddImage(reinterpret_cast<ImTextureID>(texture), min, { min.x + art.Size * texel, min.y + art.Size * texel }, { 0, 0 }, { 1, 1 },
                                   Colour(255, 255, 255, alpha));
                    return;
                }
                auto const fill = icon == Icon::PadA ? DiscA : DiscY;
                list->AddCircleFilled(c, radius, Colour(0, 0, 0, 0.5f * alpha));
                list->AddCircleFilled(c, 0.95f * radius, Colour(fill, alpha, 0.3f));
                list->AddCircleFilled(c, 0.9f * radius, Colour(fill, alpha, 0.55f));
                list->AddCircleFilled(c, 0.86f * radius, Colour(fill, alpha, 0.75f));
                list->AddCircleFilled(c, 0.82f * radius, Colour(fill, alpha));
                auto const ink = Colour(0, 0, 0, 0.75f * alpha);
                auto const letter = icon == Icon::PadA ? "A" : "Y";
                auto const size = 1.6f * radius;
                auto const width = R.Font.SemiBold->CalcTextSizeA(size, FLT_MAX, 0, letter).x;
                list->AddText(R.Font.SemiBold, size, { c.x - width * 0.5f, CapsTop(R.Font.SemiBold, size, c.y) }, ink, letter);
                return;
            }
            auto const label = KeyLabel(icon);
            auto const half = IconWidth(icon, radius, k) * 0.5f;
            ImVec2 const min{ c.x - half, c.y - radius };
            ImVec2 const max{ c.x + half, c.y + radius };
            auto const rounding = 5 * k;
            list->AddRectFilled({ min.x + k, min.y + 2 * k }, { max.x + k, max.y + 2 * k }, Colour(0, 0, 0, 0.35f * alpha), rounding);
            auto const first = list->VtxBuffer.Size;
            list->AddRectFilled(min, max, Colour(KeyBottom, 0.62f * alpha), rounding);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(list, first, list->VtxBuffer.Size, min, { min.x, max.y }, Colour(KeyTop, 1), Colour(KeyBottom, 1));
            list->AddRect(min, max, Colour(0xBD, 0xBE, 0xBD, 0.45f * alpha), rounding, std::max(1.f, k));
            auto const& style = R.Style.Key;
            auto const width = SpacedWidth(style, k, label);
            DrawSpaced(list, style, k, { c.x - width * 0.5f, CapsTop(style, k, c.y) }, Colour(238, 238, 238, 0.95f * alpha), label);
        }

        /// The bottom left: each key with its label to its right, in the game's label colour, side by side.
        void DrawGamePrompts(ImDrawList* list, float height, float alpha, float k, bool pad)
        {
            // Resume names A and the space bar; Start and Esc, which paused, resume too.
            Prompt const prompts[] = { { pad ? Icon::PadA : Icon::KeySpace, R.Words[TextRules::IndexOf(TextRules::Word::Resume)] },
                                       { pad ? Icon::PadY : Icon::KeyF7, R.Words[TextRules::IndexOf(TextRules::Word::Photo)] } };
            auto const count = R.Photo ? 2 : 1;
            auto const radius = 23 * k;
            auto const& style = R.Style.Label;
            auto const y = height - 119 * k;
            auto x = 88 * k;
            for (int i = 0; i < count; i++)
            {
                auto const& p = prompts[i];
                auto const iconWidth = IconWidth(p.What, radius, k);
                DrawGameIcon(list, p.What, { x + iconWidth * 0.5f, y }, radius, alpha, k);
                auto const labelX = x + iconWidth + 14 * k;
                ImVec2 const at{ labelX, CapsTop(style, k, y) };
                DrawSpaced(list, style, k, { at.x + 2 * k, at.y + 2 * k }, Colour(0, 0, 0, 0.55f * alpha), p.Label);
                DrawSpaced(list, style, k, at, Colour(R.Native->Label, alpha), p.Label);
                x = labelX + SpacedWidth(style, k, p.Label) + 52 * k;
            }
        }

        // ! Frame
        // ========================================

        void ReleaseTarget()
        {
            if (R.Target != nullptr) R.Target->Release();
            if (R.Back != nullptr) R.Back->Release();
            R.Target = nullptr;
            R.Back = nullptr;
        }

        void ReleaseMovie()
        {
            if (R.Clean != nullptr) R.Clean->Release();
            if (R.MovieChain != nullptr) R.MovieChain->Release();
            R.Clean = nullptr;
            R.MovieChain = nullptr;
            // The next movie tries the copy again.
            R.HaveCleanRefused = false;
        }

        /// The same size, format and sampling: a copy of one fits the other.
        bool SameKind(D3D11_TEXTURE2D_DESC const& a, D3D11_TEXTURE2D_DESC const& b)
        {
            return a.Width == b.Width && a.Height == b.Height && a.Format == b.Format && a.SampleDesc.Count == b.SampleDesc.Count &&
                   a.SampleDesc.Quality == b.SampleDesc.Quality;
        }

        /// The game's frame, before we draw on it, copied into Clean (made or made again to the back buffer's size and format).
        void KeepClean(IDXGISwapChain* chain)
        {
            ID3D11Texture2D* back = nullptr;
            if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || back == nullptr) return;
            D3D11_TEXTURE2D_DESC desc{};
            back->GetDesc(&desc);
            if (R.Clean != nullptr)
            {
                D3D11_TEXTURE2D_DESC kept{};
                R.Clean->GetDesc(&kept);
                if (!SameKind(kept, desc))
                {
                    R.Clean->Release();
                    R.Clean = nullptr;
                }
            }
            // A copy that could not be made (short of video memory) is not asked for again at every frame of the movie.
            if (R.Clean == nullptr && !(R.HaveCleanRefused && SameKind(R.CleanRefused, desc)))
            {
                desc.Usage = D3D11_USAGE_DEFAULT;
                // A multisampled texture must be bindable as a render target.
                desc.BindFlags = desc.SampleDesc.Count > 1 ? D3D11_BIND_RENDER_TARGET : 0;
                desc.CPUAccessFlags = 0;
                desc.MiscFlags = 0;
                auto const hr = R.Device->CreateTexture2D(&desc, nullptr, &R.Clean);
                if (FAILED(hr))
                {
                    R.Clean = nullptr;
                    R.CleanRefused = desc;
                    R.HaveCleanRefused = true;
                    if (!R.CleanLogged)
                    {
                        R.CleanLogged = true;
                        LEASI_ERROR("overlay: no copy of the movie's frame (0x{:08X}): a paused movie shows its pause screen only as it pauses",
                                    static_cast<unsigned>(hr));
                    }
                }
            }
            if (R.Clean != nullptr) R.Context->CopyResource(R.Clean, back);
            back->Release();
        }

        bool EnsureTarget(IDXGISwapChain* chain)
        {
            if (R.Target != nullptr) return true;
            if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&R.Back))) || R.Back == nullptr) return false;
            D3D11_TEXTURE2D_DESC back{};
            R.Back->GetDesc(&back);
            // A typeless back buffer needs a format for its view: the one Blur reads it in.
            D3D11_RENDER_TARGET_VIEW_DESC view{};
            view.Format = Blur::TypedFormat(back.Format);
            view.ViewDimension = back.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
            auto const hr = R.Device->CreateRenderTargetView(R.Back, &view, &R.Target);
            if (FAILED(hr))
            {
                LEASI_ERROR("overlay: no render target view on the back buffer (format {}, 0x{:08X})", static_cast<int>(back.Format), static_cast<unsigned>(hr));
                ReleaseTarget();
                return false;
            }
            return true;
        }

        enum class StartResult : unsigned char
        {
            /// Drawn on from now.
            Started,
            /// Not the game's (another window's, or not D3D11): this present passes through as if we weren't there, and a
            /// later one is tried. Nothing of the overlay was set up.
            NotGame,
            /// The game's, and the overlay could not start on it: off for the session.
            Failed,
        };

        /// "thread 1234", or what stands for a thread not known.
        std::string ThreadName(DWORD thread, char const* unknown) { return thread != 0 ? std::format("thread {}", thread) : std::string{ unknown }; }

        /// "window 0x..., made by thread 1234", for the logs.
        std::string WindowText(HWND window)
        {
            if (window == nullptr) return "no window";
            auto const maker = MakerOf(window);
            return std::format("window {}, made by {}", static_cast<void const*>(window), ThreadName(maker.Ours ? maker.Thread : 0, "another process"));
        }

        /// The threads IsGameWindow goes by, for the logs.
        std::string RuleThreads()
        {
            auto const main = MainThread.load(std::memory_order_relaxed);
            auto const game = GameThread();
            return std::format("main thread {}, game thread {}", main != 0 ? std::to_string(main) : std::string{ "not read" },
                               game != 0 ? std::to_string(game) : std::string{ "not known yet" });
        }

        StartResult Start(IDXGISwapChain* chain)
        {
            // Whose swap chain it is comes first, from its window alone, before anything is set up: the game's is taken at its
            // first present, any other is left as it is.
            DXGI_SWAP_CHAIN_DESC desc{};
            ID3D11Device* device = nullptr;
            auto const d3d11 = SUCCEEDED(chain->GetDesc(&desc)) && SUCCEEDED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) &&
                               device != nullptr;
            auto const window = d3d11 ? desc.OutputWindow : nullptr;
            auto const game = d3d11 && IsGameWindow(window);
            // Asked only of one the rule refuses: whether the player looks at its window, the only kind taken anyway.
            auto const foreground = d3d11 && !game && window != nullptr ? GetForegroundWindow() : nullptr;
            auto const front = foreground != nullptr && GetAncestor(window, GA_ROOT) == foreground;
            // The game's, taken anyway after StartAnywayMs of the game's frames, or left alone (OverlayRules::Judge).
            auto const take = OverlayRules::Judge(R.Clock, d3d11, game, front, GameThread() != 0, GetTickCount64());
            if (take == OverlayRules::Take::Game)
                LEASI_INFO("overlay: swap chain {} presents to {}: the game's ({}{})", static_cast<void const*>(chain), WindowText(window), RuleThreads(),
                           R.Clock.Refused > 0 ? std::format(", {} presents refused before", R.Clock.Refused) : "");
            else
            {
                if (R.Clock.Refused == 1)
                    LEASI_INFO("overlay: swap chain {} {}: not the game's, left alone ({}; logged once)", static_cast<void const*>(chain),
                               d3d11 ? std::format("presents to {}", WindowText(window)) : std::string{ "is no D3D11 swap chain" },
                               RuleThreads());
                if (take == OverlayRules::Take::Refuse)
                {
                    if (device != nullptr) device->Release();
                    return StartResult::NotGame;
                }
                LEASI_WARN("overlay: {} presents refused, none to a window of the game's in {} s of its frames ({}): taken anyway, swap chain {} on {}, the "
                           "foreground window",
                           R.Clock.Refused, OverlayRules::StartAnywayMs / 1000, RuleThreads(), static_cast<void const*>(chain), WindowText(window));
            }
            R.Device = device;
            R.Device->GetImmediateContext(&R.Context);

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            // imgui.ini and its log would land next to the game's executable, inside the install. No input: nothing to click.
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.MouseDrawCursor = false;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
            io.BackendPlatformName = "PauseAnytime";
            LoadFonts();
            // ImGui's backend compiles its shaders with D3DCompile, a delay-loaded import (CMakeLists.txt): Windows' copy is
            // loaded first, so the import binds to it, and a machine without it gets the banner, not an ASI that won't load.
            if (LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) == nullptr)
            {
                LEASI_ERROR("overlay: no d3dcompiler_47.dll in System32 (error {})", GetLastError());
                return StartResult::Failed;
            }
            // The shaders compile here, at the game's first frame, not in the first paused one: ImGui's NewFrame would build its
            // own there.
            if (!ImGui_ImplDX11_Init(R.Device, R.Context) || !ImGui_ImplDX11_CreateDeviceObjects()) return StartResult::Failed;
            auto const blur = Blur::Init(R.Device);

            R.GameChain = chain;
            R.GameWindow = desc.OutputWindow;
            R.Took = take;
            R.Native = &NativeLookOf(Look.load(std::memory_order_relaxed));
            R.Language = TextRules::LanguageOfCode(LaunchLanguageCode());
            QueryPerformanceCounter(&R.LastFrame);
            LEASI_INFO("overlay ready: back buffer {}x{} format {}, swap effect {}, present thread {}, {} look, text in {}, {}, {}", desc.BufferDesc.Width,
                       desc.BufferDesc.Height, static_cast<int>(desc.BufferDesc.Format), static_cast<int>(desc.SwapEffect), GetCurrentThreadId(),
                       R.Native->Name, TextRules::NameOf(R.Language), R.Font.Embedded ? "Exo 2 embedded" : "default font", blur ? "blur ready" : "NO blur (plain darkening)");
            LEASI_FLUSH();
            return StartResult::Started;
        }

        /// The game's new swap chain (a display mode change): drawn on from now, on its device.
        void Adopt(IDXGISwapChain* chain)
        {
            // One on the window we draw on, on a window of the game's (IsGameWindow), or, for an overlay taken anyway whose
            // window is gone, on the foreground window (OverlayRules::Readopt): a swap chain of someone else's, once taken,
            // would keep the game's from ever being taken back.
            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(chain->GetDesc(&desc))) return;
            auto const window = desc.OutputWindow;
            auto const alive = IsWindow(R.GameWindow) != FALSE;
            // As Start asks it: the player looks at that window, or at one it is inside.
            auto const foreground = window != nullptr ? GetForegroundWindow() : nullptr;
            auto const front = foreground != nullptr && GetAncestor(window, GA_ROOT) == foreground;
            auto const took = OverlayRules::Readopt(R.Took, window == R.GameWindow, IsGameWindow(window), alive, front);
            if (took == OverlayRules::Take::Refuse)
            {
                if (!R.ForeignLogged)
                {
                    R.ForeignLogged = true;
                    LEASI_INFO("overlay: swap chain {} presents to {}, not a window of the game's ({}; we draw on {}, {}{}): left alone (logged once)",
                               static_cast<void const*>(chain), WindowText(window), RuleThreads(), static_cast<void const*>(R.GameWindow),
                               alive ? "still there" : "gone", R.Took == OverlayRules::Take::Anyway ? ", taken anyway" : "");
                }
                return;
            }
            ID3D11Device* device = nullptr;
            // Not a D3D11 swap chain: not the game's.
            if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || device == nullptr) return;
            ReleaseTarget();
            Blur::Release();
            ReleaseMovie();
            // Nothing of the old swap chain is held now, and nothing of the new one yet. A resize that finds the gate held
            // through the shader builds below, our own work, waits for them and for the frame drawn after (EnterWaiting).
            DrawnChain.store(nullptr, std::memory_order_relaxed);
            if (device != R.Device)
            {
                ImGui_ImplDX11_Shutdown();
                if (R.IconA != nullptr) LEASI_INFO("overlay: the game's gamepad icons were on the old device: ours drawn from now on");
                ReleaseGameIcons();
                R.Context->Release();
                R.Device->Release();
                R.Device = device;
                R.Device->GetImmediateContext(&R.Context);
                if (!ImGui_ImplDX11_Init(R.Device, R.Context) || !ImGui_ImplDX11_CreateDeviceObjects())
                {
                    LEASI_ERROR("overlay: ImGui did not start on the new swap chain's device: overlay off");
                    SetPhase(Phase::Off);
                    return;
                }
                // The blur's shaders too, now, not in the next paused frame; it lets go of the old device. It logs a failure.
                Blur::Init(R.Device);
            }
            else device->Release();
            R.GameChain = chain;
            R.GameWindow = window;
            R.Took = took;
            DrawnChain.store(chain, std::memory_order_relaxed);
            LEASI_INFO("overlay: drawing on swap chain {} now, on {} (the one before went silent){}", static_cast<void const*>(chain), WindowText(window),
                       took == OverlayRules::Take::Anyway ? ", taken anyway: the foreground window" : "");
        }

        /// A fault in MovieIdle's present turned the overlay off while a resize held the gate (OffWanted): done now, under the gate.
        void TakeOffWanted()
        {
            if (OffWanted.load(std::memory_order_relaxed) && OffWanted.exchange(false, std::memory_order_acquire)) SetPhase(Phase::Off);
        }

        /// Under the gate.
        void Frame(IDXGISwapChain* chain)
        {
            TakeOffWanted();
            if (R.At == Phase::Off)
            {
                if (!SelfPresent && (R.Clean != nullptr || R.MovieChain != nullptr)) ReleaseMovie();
                return;
            }
            if (R.At == Phase::Waiting)
            {
                auto const started = Start(chain);
                if (started == StartResult::NotGame) return;
                if (started == StartResult::Failed)
                {
                    SetPhase(Phase::Off);
                    LEASI_ERROR("overlay: could not start on the game's swap chain: the Canvas banner instead");
                    return;
                }
                SetPhase(Phase::Started);
            }
            auto const now = GetTickCount64();
            if (chain != R.GameChain)
            {
                // BeforePresent passes by, without the gate, a present for which this does nothing (OverlayRules::PassBy).
                auto const other = OverlayRules::OtherChain(OverlayRules::Since(now, R.GameChainSeen));
                if (other.Pending) AdoptPending.store(true, std::memory_order_relaxed);
                if (!other.Adopt) return;
                Adopt(chain);
                if (R.At == Phase::Off || chain != R.GameChain) return;
            }
            R.GameChainSeen = now;
            DrawnSeen.store(now, std::memory_order_release);
            AdoptPending.store(false, std::memory_order_relaxed);
            PresentThread.store(GetCurrentThreadId(), std::memory_order_relaxed);

            // As soon as they come, not at the first pause: a reference from here on keeps them.
            if (!R.GameIconsSeen && !GuardedGameIcons())
            {
                ReleaseGameIcons();
                LEASI_ERROR("overlay: access violation while checking the game's gamepad icons: ours drawn instead");
            }

            LARGE_INTEGER time, frequency;
            QueryPerformanceCounter(&time);
            QueryPerformanceFrequency(&frequency);
            auto const seconds = std::clamp(static_cast<float>(time.QuadPart - R.LastFrame.QuadPart) / static_cast<float>(frequency.QuadPart), 0.f, 0.1f);
            R.LastFrame = time;

            // The world pause shows while the HUD draws (a stale flag across a load or a fault shows nothing); the movie pause
            // while it doesn't (a fullscreen movie covers the screen).
            auto const flags = Published.load(std::memory_order_acquire);
            // The game thread may have stored a tick taken after ours: no wrap-around.
            auto const hud = HudSeen();
            auto const hudFresh = hud != 0 && (now > hud ? now - hud : 0) <= HudSilentMs;
            auto const world = (flags & ShowWorldPause) != 0 && hudFresh;
            auto const movie = !world && Bink::PauseWanted() && !hudFresh;
            // A movie Esc could pause, with the HUD silent, may be a fullscreen one: its frame is kept for MovieIdle, before we
            // draw on it. From half the silence Bink.cpp waits for before it takes a pause, so a frame is kept when one comes.
            // A movie that stays the game's (a loading screen, no known sound) costs no copy.
            if (!SelfPresent)
            {
                R.LastGamePresent = time;
                auto const quiet = hud == 0 || (now > hud ? now - hud : 0) > HudSilentMs / 2;
                if (Bink::PausableMovies() > 0 && quiet)
                {
                    KeepClean(chain);
                    // Referenced here, in a present of its own, never from its identity alone: a movie pauses in its BinkWait
                    // with no present in between, and the game may let go of its swap chain once it stops presenting. So the
                    // reference is held for the whole movie, since the pause may come at any of its frames. Should the game
                    // replace its swap chain during the movie, the old one lives on until the game presents with no such
                    // movie; with a flip-model swap chain (one per window, the "overlay ready" line names the model) the new
                    // one could not be made meanwhile. No game is known to do that during a movie.
                    if (R.MovieChain == nullptr)
                    {
                        chain->AddRef();
                        R.MovieChain = chain;
                    }
                }
                else if (R.Clean != nullptr || R.MovieChain != nullptr)
                    ReleaseMovie();
            }
            auto const shown = world || movie;
            if (shown) R.Photo = world && (flags & OfferPhoto) != 0;
            // HideNow is our world pause's, published from the HUD's frames: over a movie pause the HUD is silent, so one seen
            // there is left over from before the movie, and the movie's pause screen shows.
            auto const hideNow = (flags & HideNow) != 0 && !movie;
            R.Fade = hideNow ? 0.f : Blur::Step(R.Fade, shown, seconds, shown ? FadeInSeconds : FadeOutSeconds);
            if (shown != R.WasShown)
            {
                R.WasShown = shown;
                if (shown)
                    LEASI_INFO("overlay shown: {}, {} prompts{}", movie ? "movie pause" : "world pause", Pad.load(std::memory_order_relaxed) ? "gamepad" : "keyboard",
                               R.Photo ? ", photo mode offered" : "");
                else
                    LEASI_INFO("overlay hidden{}", hideNow ? " at once (photo mode or the console)" : "");
            }
            if (R.Fade <= 0)
            {
                // Nothing of the back buffer is held between pauses: the game may resize or replace its swap chain freely.
                if (R.Target != nullptr)
                {
                    ReleaseTarget();
                    Blur::Release();
                }
                R.DrawFailures = 0;
                return;
            }

            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(chain->GetDesc(&desc)) || desc.BufferDesc.Width == 0 || desc.BufferDesc.Height == 0) return;
            if (!EnsureTarget(chain))
            {
                if (++R.DrawFailures >= MaxDrawFailures)
                {
                    LEASI_ERROR("overlay: {} frames in a row without a view on the back buffer: off, the Canvas banner instead", R.DrawFailures);
                    SetPhase(Phase::Off);
                }
                return;
            }
            R.DrawFailures = 0;
            auto const width = static_cast<float>(desc.BufferDesc.Width);
            auto const height = static_cast<float>(desc.BufferDesc.Height);
            auto const alpha = Blur::Ease(R.Fade);

            auto const blurred = Blur::Draw(R.Context, R.Back, R.Target, alpha, R.Native->Blur);
            if (!R.BlurLogged && !blurred)
            {
                R.BlurLogged = true;
                LEASI_WARN("overlay: no blur on this back buffer (see above): plain darkening");
            }

            TakeGameFonts();
            TakeTexts();
            auto& io = ImGui::GetIO();
            io.DisplaySize = { width, height };
            io.DeltaTime = std::max(seconds, 1e-4f);
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            auto const list = ImGui::GetForegroundDrawList();
            if (!blurred) list->AddRectFilled({ 0, 0 }, { width, height }, Colour(0, 0, 0, 0.6f * alpha));
            // Laid out for 1080 lines, like the game's HUD.
            auto const k = height / 1080.f;
            auto const pad = Pad.load(std::memory_order_relaxed);
            DrawBandTitle(list, width, height, alpha, k);
            DrawGamePrompts(list, height, alpha, k, pad);
            ImGui::Render();

            ID3D11RenderTargetView* saved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
            ID3D11DepthStencilView* savedDepth = nullptr;
            R.Context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved, &savedDepth);
            R.Context->OMSetRenderTargets(1, &R.Target, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            R.Context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved, savedDepth);
            for (auto view : saved)
            {
                if (view != nullptr) view->Release();
            }
            if (savedDepth != nullptr) savedDepth->Release();
        }

        void FrameEntry(IDXGISwapChain* chain)
        {
            try
            {
                Frame(chain);
            }
            catch (std::exception const& e)
            {
                SetPhase(Phase::Off);
                LEASI_ERROR("overlay: {}: off, the Canvas banner instead", e.what());
            }
        }

        /// An access violation in the overlay turns it off, not the game. No C++ object may live in this frame.
        void Guarded(IDXGISwapChain* chain)
        {
            __try
            {
                FrameEntry(chain);
            }
            __except (Fault::Filter(GetExceptionInformation()))
            {
                SetPhase(Phase::Off);
                Fault::Log("overlay: off, the Canvas banner instead");
            }
        }

        /// Our work in a present, under the gate: a present that finds another thread at it passes through untouched, and
        /// MovieIdle's own present finds it held by its own thread. Given back before the original present: a resize waits on
        /// our work, never on a present (MovieIdle's excepted, see BeforeResize). No C++ object may live in this frame.
        void GatedFrame(IDXGISwapChain* chain)
        {
            auto const gate = Enter();
            if (gate == Gate::Busy)
            {
                LogBusy(BusyPresentLogged, "a present");
                return;
            }
            __try
            {
                Guarded(chain);
            }
            __finally
            {
                Leave(gate);
            }
        }

        /// A present of another swap chain while ours presents, for which Frame would do nothing (OverlayRules::PassBy). Asked
        /// without the gate, so that such a present never holds the gate when the game's comes, which would then go out
        /// without the pause screen. The tick is read after DrawnSeen, so it is no older.
        bool PassesBy(IDXGISwapChain* chain)
        {
            if (!Live.load(std::memory_order_acquire)) return false;
            auto const seen = DrawnSeen.load(std::memory_order_acquire);
            auto const drawn = DrawnChain.load(std::memory_order_relaxed);
            return OverlayRules::PassBy(true, chain == drawn, OverlayRules::Since(GetTickCount64(), seen));
        }

        /// Before the original present: the pause screen, once a frame. DXGI_PRESENT_TEST only asks whether presenting would
        /// work: nothing is shown.
        void BeforePresent(IDXGISwapChain* chain, UINT flags)
        {
            if (flags & DXGI_PRESENT_TEST) return;
            if (!SelfPresent) LastChain.store(chain, std::memory_order_relaxed);
            if (chain == NotShown || PassesBy(chain)) return;
            GatedFrame(chain);
        }

        void AfterPresent(IDXGISwapChain* chain, UINT flags, HRESULT hr)
        {
            if (flags & DXGI_PRESENT_TEST) return;
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING) NotShown = chain;
            else if (chain == NotShown) NotShown = nullptr;
        }

        /// MovieIdle's present, before its original: GatedIdle holds the gate across it, and does nothing more with R after
        /// it, so the holder is marked away (OverlayRules::GoAway) and a resize stuck behind it may take the gate. Any other
        /// present gave the gate back before its original (GatedFrame). True if marked: GoneBack is then due.
        bool GoAwayToPresent() { return SelfPresent && OverlayRules::GoAway(GateOwner, Self()); }

        // The guard spans the original call too: an overlay or runtime between us and DXGI may present one way through the
        // other (Present1 into Present, or back), and the frame must be drawn once. InPresent is cleared, and an away gate
        // brought back, even when an exception raised below the original goes up through here to a handler above us: left
        // set, no later present on this thread would be drawn on. No C++ object may live in these frames.
        HRESULT STDMETHODCALLTYPE Present_hook(IDXGISwapChain* chain, UINT sync, UINT flags)
        {
            auto const orig = Wait(Present_orig);
            if (InPresent) return orig(chain, sync, flags);
            InPresent = true;
            HRESULT hr = E_FAIL;
            auto away = false;
            __try
            {
                BeforePresent(chain, flags);
                away = GoAwayToPresent();
                hr = orig(chain, sync, flags);
            }
            __finally
            {
                // Its answer is not needed: nothing of R is touched here after the original, and GuardedSelfPresent asks the
                // gate itself.
                if (away) GoneBack();
                InPresent = false;
            }
            AfterPresent(chain, flags, hr);
            return hr;
        }

        HRESULT STDMETHODCALLTYPE Present1_hook(IDXGISwapChain1* chain, UINT sync, UINT flags, DXGI_PRESENT_PARAMETERS const* parameters)
        {
            auto const orig = Wait(Present1_orig);
            if (InPresent) return orig(chain, sync, flags, parameters);
            InPresent = true;
            HRESULT hr = E_FAIL;
            auto away = false;
            __try
            {
                BeforePresent(chain, flags);
                away = GoAwayToPresent();
                hr = orig(chain, sync, flags, parameters);
            }
            __finally
            {
                // Its answer is not needed: nothing of R is touched here after the original, and GuardedSelfPresent asks the
                // gate itself.
                if (away) GoneBack();
                InPresent = false;
            }
            AfterPresent(chain, flags, hr);
            return hr;
        }

        /// A paused movie's present, before it is made: its last frame back into the back buffer. False when there is none to
        /// make: not yet (60 a second at most, counting the game's; the game silent for a moment first, so none slips between
        /// its own frames), or no frame of this back buffer's size kept. Under the gate.
        bool PrepareIdle()
        {
            TakeOffWanted();
            if (R.At != Phase::Started || R.Clean == nullptr || R.MovieChain == nullptr) return false;
            LARGE_INTEGER time, frequency;
            QueryPerformanceCounter(&time);
            QueryPerformanceFrequency(&frequency);
            if (time.QuadPart - R.LastFrame.QuadPart < frequency.QuadPart / IdleRate) return false;
            if (time.QuadPart - R.LastGamePresent.QuadPart < frequency.QuadPart * IdleQuietMs / 1000) return false;
            // The movie presented to the swap chain we draw on (Frame referenced it there), and nothing presented elsewhere since.
            if (R.MovieChain != R.GameChain || LastChain.load(std::memory_order_relaxed) != R.MovieChain) return false;
            ID3D11Texture2D* back = nullptr;
            if (FAILED(R.MovieChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || back == nullptr) return false;
            D3D11_TEXTURE2D_DESC desc{}, kept{};
            back->GetDesc(&desc);
            R.Clean->GetDesc(&kept);
            // Resized while paused: the copy would be dropped, and an undefined buffer blurred.
            auto const same = SameKind(desc, kept);
            if (same) R.Context->CopyResource(back, R.Clean);
            back->Release();
            if (!same) return false;
            if (!R.IdleLogged)
            {
                R.IdleLogged = true;
                LEASI_INFO("overlay: the paused movie presents nothing: its last frame is presented from its BinkWait, with the pause screen");
            }
            return true;
        }

        /// Our own work, before the present GuardedSelfPresent makes.
        bool GuardedPrepareIdle()
        {
            __try
            {
                return PrepareIdle();
            }
            __except (Fault::Filter(GetExceptionInformation()))
            {
                SetPhase(Phase::Off);
                Fault::Log("overlay, a paused movie's frame: off, the Canvas banner instead");
                return false;
            }
        }

        /// MovieIdle's present, which the game never asked for: a fault below it (DXGI, the driver, another overlay) would go up
        /// through BinkWait into the movie player and end the game, so here it turns the overlay off instead. A fault below a
        /// present the game makes is left to the game, since that present would run without us too. This one runs only
        /// because of us, so its fault is taken, as the game thread's guards take one in a game native we call. The price is
        /// the same: the module that faulted may keep a lock or a half-done state, so the game's next present could hang
        /// where, unguarded, the game would have crashed now. No C++ object may live in this frame.
        void GuardedSelfPresent(IDXGISwapChain* chain)
        {
            __try
            {
                __try
                {
                    SelfPresent = true;
                    chain->Present(1, 0);
                }
                __finally
                {
                    // Also for an exception that is no fault, going up past us: left set, every later present on this thread
                    // would count as ours.
                    SelfPresent = false;
                }
            }
            __except (Fault::Filter(GetExceptionInformation()))
            {
                // The original ran away (GoAwayToPresent), and a resize may have taken the gate meanwhile: R is then that
                // resize's, and the next call to take the gate turns the overlay off.
                if (OverlayRules::Holds(GateOwner, Self())) SetPhase(Phase::Off);
                else OffWanted.store(true, std::memory_order_release);
                Fault::Log("overlay, a paused movie's present: off, the Canvas banner instead");
            }
        }

        /// MovieIdle's frame, under the gate from the copy of the movie's frame through its present, the original included: no
        /// resize lets go of the back buffer, or another present draws on it, in between. Not taken while a resize waits for
        /// the gate (OverlayRules::EnterIdle): at some 250 BinkWaits a second, each present held across a vsync, the resize
        /// would otherwise find it held at most of its tries. No C++ object may live in this frame.
        void GatedIdle()
        {
            auto const gate = OverlayRules::EnterIdle(GateOwner, ResizesWaiting, Self());
            if (gate == Gate::Busy)
            {
                // The next BinkWait tries again.
                LogBusy(BusyIdleLogged, "a paused movie's frame");
                return;
            }
            __try
            {
                if (!GuardedPrepareIdle()) __leave;
                // Through our Present hook, which draws the pause screen as the gate's holder; Frame holds the swap chain too, our
                // reference only for the call, dropped also when an exception goes up through here.
                auto const chain = R.MovieChain;
                chain->AddRef();
                __try
                {
                    GuardedSelfPresent(chain);
                }
                __finally
                {
                    chain->Release();
                }
            }
            __finally
            {
                Leave(gate);
            }
        }

        /// ResizeBuffers and ResizeBuffers1, before the original, with the gate (EnterWaiting): our view and the blur's copies
        /// hold the back buffer of the swap chain we draw on, which must be released before it can be resized, and the movie's
        /// kept frame no longer fits it. For that swap chain the hook keeps the gate until the original returns, away for the
        /// original (GoAway), so that no present takes the back buffer again in between (one then passes through untouched,
        /// and MovieIdle skips its frame); for any other swap chain it is given back here.
        ///
        /// No deadlock: only a resize waits for the gate, MovieIdle takes none meanwhile, and the wait ends. A holder in our
        /// own work is waited for: that work waits on no other thread (D3D11's and DXGI's own locks for the length of one
        /// call and the log's for one line, none of them held by a thread waiting here, before its original), and ends. Start
        /// and Adopt build shaders under it, and draw on the swap chain right after: a resize of it waits for both. A holder
        /// away in an original (MovieIdle's present, another resize) may wait on others (DXGI's notes on threads say a
        /// full-screen swap chain may send its window a message, and the resizing thread may be that window's): past
        /// ResizeWaitMs the resize of the swap chain we draw on takes the gate from it, and lets go of the back buffer that
        /// present may still be showing (logged). That holder, back from its original, finds the gate taken (GoneBack) and
        /// does nothing more with R, and its Leave gives nothing back. A resize of any other swap chain goes on without the
        /// gate then (got.Got is Busy), and touches nothing of R. Past ResizeWaitOwnMs, the last resort, the same is done to
        /// a holder in our own work, so that the resize neither hangs nor fails for a back buffer we hold: that holder may
        /// still be at work on R (logged as an error), and a fault it then takes turns the overlay off (Guarded).
        void BeforeResize(IDXGISwapChain* chain, OverlayRules::Waited got, char const* what, UINT width, UINT height)
        {
            // Once, flushed ahead of the resize: which thread resizes, beside the one that presents, for a crash in there.
            static std::atomic<bool> logged{ false };
            if (!logged.exchange(true))
            {
                LEASI_INFO("overlay: the first {}, to {}x{}, runs on thread {}; frames are presented on {}; {}", what, width, height, GetCurrentThreadId(),
                           PresentThread.load(std::memory_order_relaxed),
                           chain == DrawnChain.load(std::memory_order_acquire) ? "the swap chain we draw on" : "another swap chain");
                LEASI_FLUSH();
            }
            if (got.Got == Gate::Busy) return;
            if (chain != R.GameChain)
            {
                // A gate taken from a holder that may still be in its call is kept to the end of the resize: given back now,
                // a third thread could take it while that holder is still at it.
                if (got.TakenFrom == 0) Leave(got.Got);
                return;
            }
            // Here, after the check of the swap chain: once only, so it is kept for the case it is for.
            static std::atomic<bool> warned{ false };
            static std::atomic<bool> warnedOwn{ false };
            if (got.TakenFrom != 0 && got.FromAway && !warned.exchange(true))
            {
                LEASI_WARN("overlay: {} on thread {} waited {} ms for the gate, held by thread {} in a present or resize that did not come back: taken "
                           "from it, the back buffer let go of while that thread may still present it (logged once)",
                           what, GetCurrentThreadId(), ResizeWaitMs, got.TakenFrom);
                LEASI_FLUSH();
            }
            if (got.TakenFrom != 0 && !got.FromAway && !warnedOwn.exchange(true))
            {
                LEASI_ERROR("overlay: {} on thread {} waited {} ms for the gate, held by thread {} in the overlay's own work: taken from it all the "
                            "same so that the resize goes on, while that thread may still use the overlay's state (logged once)",
                            what, GetCurrentThreadId(), ResizeWaitOwnMs, got.TakenFrom);
                LEASI_FLUSH();
            }
            ReleaseTarget();
            Blur::Release();
            if (R.Clean != nullptr) R.Clean->Release();
            R.Clean = nullptr;
        }

        // The gate is taken before the original and given back after it, at the latest (BeforeResize gives it back at once for
        // a swap chain we don't draw on), also when an exception goes up through here, and is away for the original. No C++
        // object may live in these frames.
        HRESULT STDMETHODCALLTYPE ResizeBuffers_hook(IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
        {
            auto const orig = Wait(ResizeBuffers_orig);
            auto const got = EnterWaiting(chain);
            HRESULT hr = E_FAIL;
            auto away = false;
            __try
            {
                BeforeResize(chain, got, "ResizeBuffers", width, height);
                away = GoAway(got.Got);
                hr = orig(chain, count, width, height, format, flags);
            }
            __finally
            {
                // Nothing of R is touched after the original: a gate taken meanwhile is left to its new holder.
                if (away) GoneBack();
                Leave(got.Got);
            }
            return hr;
        }

        HRESULT STDMETHODCALLTYPE ResizeBuffers1_hook(IDXGISwapChain3* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
                                                      UINT const* nodeMasks, IUnknown* const* queues)
        {
            auto const orig = Wait(ResizeBuffers1_orig);
            auto const got = EnterWaiting(chain);
            HRESULT hr = E_FAIL;
            auto away = false;
            __try
            {
                BeforeResize(chain, got, "ResizeBuffers1", width, height);
                away = GoAway(got.Got);
                hr = orig(chain, count, width, height, format, flags, nodeMasks, queues);
            }
            __finally
            {
                // Nothing of R is touched after the original: a gate taken meanwhile is left to its new holder.
                if (away) GoneBack();
                Leave(got.Got);
            }
            return hr;
        }
    }

    void Install(::LESDK::Initializer& Init, OverlayLook look)
    {
        Look.store(look, std::memory_order_relaxed);
        // Before the hooks: IsGameWindow judges the first present by it.
        MainThread.store(FindMainThread(), std::memory_order_relaxed);
        // The swap chain methods' addresses, from a throwaway device on a hidden window: every swap chain shares them.
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof windowClass;
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"PauseAnytimeProbe";
        RegisterClassExW(&windowClass);
        auto const window = CreateWindowExW(0, windowClass.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr, nullptr, windowClass.hInstance, nullptr);

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 1;
        desc.BufferDesc.Width = 16;
        desc.BufferDesc.Height = 16;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = window;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        void* present = nullptr;
        void* present1 = nullptr;
        void* resize = nullptr;
        void* resize1 = nullptr;
        IDXGISwapChain* chain = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        auto hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
        if (FAILED(hr)) hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
        if (SUCCEEDED(hr) && chain != nullptr)
        {
            // IDXGISwapChain: Present is 8, ResizeBuffers 13; IDXGISwapChain1 adds Present1 at 22; IDXGISwapChain3 adds
            // ResizeBuffers1 at 39.
            auto const table = *reinterpret_cast<void***>(chain);
            present = table[8];
            resize = table[13];
            IDXGISwapChain1* chain1 = nullptr;
            if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&chain1))) && chain1 != nullptr)
            {
                present1 = (*reinterpret_cast<void***>(chain1))[22];
                chain1->Release();
            }
            IDXGISwapChain3* chain3 = nullptr;
            if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&chain3))) && chain3 != nullptr)
            {
                resize1 = (*reinterpret_cast<void***>(chain3))[39];
                chain3->Release();
            }
        }
        if (context != nullptr) context->Release();
        if (chain != nullptr) chain->Release();
        if (device != nullptr) device->Release();
        if (window != nullptr) DestroyWindow(window);
        UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);

        if (present == nullptr || resize == nullptr)
        {
            LEASI_ERROR("overlay: no D3D11 device to read the swap chain methods from (0x{:08X}): the Canvas banner instead", static_cast<unsigned>(hr));
            return;
        }
        // ResizeBuffers first: Present may run (and take the back buffer) as soon as its hook is live.
        ResizeBuffers_orig.store(static_cast<t_ResizeBuffers*>(Init.InstallHook("IDXGISwapChain::ResizeBuffers", resize, ResizeBuffers_hook)),
                                 std::memory_order_release);
        if (ResizeBuffers_orig.load() == nullptr)
        {
            LEASI_ERROR("overlay: hooking IDXGISwapChain::ResizeBuffers failed: the Canvas banner instead");
            return;
        }
        // A layer between the game and DXGI may resize through ResizeBuffers1: without this hook, a pause holding the back
        // buffer would make that resize fail. The game itself resizes through ResizeBuffers, so the overlay goes on without it.
        if (resize1 != nullptr && resize1 != resize)
            ResizeBuffers1_orig.store(static_cast<t_ResizeBuffers1*>(Init.InstallHook("IDXGISwapChain3::ResizeBuffers1", resize1, ResizeBuffers1_hook)),
                                      std::memory_order_release);
        Present_orig.store(static_cast<t_Present*>(Init.InstallHook("IDXGISwapChain::Present", present, Present_hook)), std::memory_order_release);
        if (present1 != nullptr && present1 != present)
            Present1_orig.store(static_cast<t_Present1*>(Init.InstallHook("IDXGISwapChain1::Present1", present1, Present1_hook)), std::memory_order_release);
        LEASI_INFO("overlay: swap chain hooks Present {}, Present1 {}, ResizeBuffers true, ResizeBuffers1 {}{}", Present_orig.load() != nullptr,
                   Present1_orig.load() != nullptr, ResizeBuffers1_orig.load() != nullptr,
                   Present_orig.load() == nullptr ? ": no Present, the Canvas banner instead" : "");
    }

    void Publish(bool worldPause, bool offerPhoto, bool hideNow)
    {
        Published.store((worldPause ? ShowWorldPause : 0u) | (offerPhoto ? OfferPhoto : 0u) | (hideNow ? HideNow : 0u), std::memory_order_release);
    }

    void UsePadPrompts(bool pad) { Pad.store(pad, std::memory_order_relaxed); }

    void UseTexts(TextRules::Picks words)
    {
        auto const texts = new TextRules::Picks{ std::move(words) };
        TextRules::Picks const* none = nullptr;
        if (!PickedTexts.compare_exchange_strong(none, texts, std::memory_order_acq_rel)) delete texts;
    }

    void UseGameIcons(void* padA, void* padY)
    {
        auto const icons = new GameIcons{ padA, padY };
        GameIcons const* none = nullptr;
        if (!GameIcon.compare_exchange_strong(none, icons, std::memory_order_acq_rel)) delete icons;
    }

    void UseGameFonts(GameFontSource light, GameFontSource bold, std::span<char32_t const> letters)
    {
        auto const fonts = new GameFonts{ ReadGameFont(light.Movie, light.Name, letters), ReadGameFont(bold.Movie, bold.Name, letters) };
        if (fonts->Light.Ttf.empty() || fonts->Bold.Ttf.empty())
        {
            LEASI_WARN("the game's fonts movies are not what we know ({}: {} bytes of TrueType, {}: {}): the pause screen keeps Exo 2", light.Name, fonts->Light.Ttf.size(),
                       bold.Name, fonts->Bold.Ttf.size());
            delete fonts;
            return;
        }
        GameFonts const* none = nullptr;
        if (!GameFontFiles.compare_exchange_strong(none, fonts, std::memory_order_acq_rel)) delete fonts;
    }

    void MovieIdle()
    {
        if (!Live.load(std::memory_order_acquire) || InPresent) return;
        // The movie's thread is the one that presents: in LE1, and in the 2026-11-08 logs in LE2 (thread 29788) and LE3 (19264)
        // too. Seen in the logs, not a rule of the engine's. On any other, the device's context isn't ours to use.
        auto const presenter = PresentThread.load(std::memory_order_relaxed);
        if (GetCurrentThreadId() != presenter)
        {
            // Once a movie, not once a session: a render thread the engine restarted (a resize) leaves the last presenter's id
            // here until the game presents again, so one movie's line may be stale, and a later movie must still say what it finds.
            static std::atomic<unsigned> loggedMovie{ 0 };
            auto const movie = Bink::MovieSerial();
            if (loggedMovie.exchange(movie, std::memory_order_relaxed) != movie)
                LEASI_WARN("overlay: the paused movie plays on thread {}, frames were last presented on {}: its pause screen shows only as it pauses",
                           GetCurrentThreadId(), presenter);
            return;
        }
        GatedIdle();
    }

    bool Drawing() { return Live.load(std::memory_order_acquire) && !AdoptPending.load(std::memory_order_relaxed); }
}
