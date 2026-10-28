#include "MelePause/Pause.hpp"
#include "MelePause/Blur.hpp"
#include "MelePause/Resources.h"

#include <d3d11.h>
#include <dxgi1_2.h>

#pragma warning(push, 0)
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_dx11.h"
#pragma warning(pop)

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cwctype>
#include <exception>
#include <string>
#include <string_view>

// The pause screen, drawn into the game's back buffer from a hooked IDXGISwapChain::Present on the render thread: over our
// world pause and our movie pause the frame is blurred and darkened (Blur.cpp), "PAUSE" stands in a bracketed frame at the
// centre, and the keys are named at the bottom left (resume, and photo mode where it opens), for the gamepad or the keyboard,
// whichever pressed our keys last. The game thread says what to show (Publish, UsePadPrompts); nothing here touches a game
// object. The text is set in Exo 2 (SIL OFL 1.1, fonts/OFL.txt), compiled into the ASI (Resources.rc), in the language the
// game's launcher passes. Without the hook or after a fault the Canvas banner of Pause.cpp is drawn instead (Drawing).
namespace MelePause::Overlay
{
    namespace
    {
        using t_Present = HRESULT STDMETHODCALLTYPE(IDXGISwapChain* chain, UINT sync, UINT flags);
        using t_Present1 = HRESULT STDMETHODCALLTYPE(IDXGISwapChain1* chain, UINT sync, UINT flags, DXGI_PRESENT_PARAMETERS const* parameters);
        using t_ResizeBuffers = HRESULT STDMETHODCALLTYPE(IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags);

        /// The loader makes a hook live before InstallHook hands back the original: a call arriving first waits for it.
        std::atomic<t_Present*> Present_orig{ nullptr };
        std::atomic<t_Present1*> Present1_orig{ nullptr };
        std::atomic<t_ResizeBuffers*> ResizeBuffers_orig{ nullptr };

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
        /// The overlay started on the game's swap chain and hasn't failed since.
        std::atomic<bool> Live{ false };

        // ! Text
        // ========================================

        struct Texts
        {
            char const* Title;
            char const* Resume;
            char const* Photo;
            /// The space bar's keycap.
            char const* Space;
            char const* Language;
        };

        constexpr Texts English{ "PAUSE", "Resume", "Photo mode", "Space", "English" };
        constexpr Texts French{ "PAUSE", "Reprendre", "Mode photo", "Espace", "French" };
        constexpr Texts German{ "PAUSE", "Fortsetzen", "Fotomodus", "Leertaste", "German" };
        constexpr Texts Italian{ "PAUSA", "Riprendi", "Modalità foto", "Spazio", "Italian" };
        constexpr Texts Spanish{ "PAUSA", "Reanudar", "Modo foto", "Espacio", "Spanish" };
        constexpr Texts Polish{ "PAUZA", "Wznów", "Tryb fotograficzny", "Spacja", "Polish" };
        constexpr Texts Russian{ "ПАУЗА", "Продолжить", "Фоторежим", "Пробел", "Russian" };

        /// The text language the LE launcher gives the game (-OVERRIDELANGUAGE=FR; FE is French text over English voices, and
        /// so are GE, IE, RA and PL to DE, IT, RU and PLPC). English without it (a start past the launcher) and for Japanese:
        /// Exo 2 has no kana.
        Texts const& LanguageTexts()
        {
            constexpr std::wstring_view Switch = L"-OVERRIDELANGUAGE=";
            std::wstring_view const line = GetCommandLineW();
            auto const at = std::search(line.begin(), line.end(), Switch.begin(), Switch.end(),
                                        [](wchar_t a, wchar_t b) { return std::towupper(a) == std::towupper(b); });
            if (at == line.end()) return English;
            std::wstring code;
            for (auto c = at + Switch.size(); c != line.end() && *c != L' ' && *c != L'\t' && *c != L'"'; ++c)
                code += static_cast<wchar_t>(std::towupper(*c));
            if (code == L"FR" || code == L"FE") return French;
            if (code == L"DE" || code == L"GE") return German;
            if (code == L"IT" || code == L"IE") return Italian;
            if (code == L"ES") return Spanish;
            if (code == L"PL" || code == L"PLPC") return Polish;
            if (code == L"RU" || code == L"RA") return Russian;
            return English;
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
        /// MovieIdle starts only this soon after the game's last present: its swap chain is the one the movie paused on.
        constexpr ULONGLONG IdleAdoptMs = 1000;
        /// ... and only once the game has presented nothing for this long.
        constexpr LONGLONG IdleQuietMs = 100;
        /// A Present from another swap chain is the game's new one (a display mode change) once the one we draw on has been
        /// silent this long; before, it is someone else's next to it, left alone.
        constexpr ULONGLONG AdoptAfterMs = 500;

        struct Fonts
        {
            ImFont* Light = nullptr;
            ImFont* Regular = nullptr;
            ImFont* SemiBold = nullptr;
            bool Embedded = false;
        };

        struct RenderState
        {
            bool Ready = false;
            bool Failed = false;
            ID3D11Device* Device = nullptr;
            ID3D11DeviceContext* Context = nullptr;
            /// Identity only: no reference is held through it.
            IDXGISwapChain* GameChain = nullptr;
            ULONGLONG GameChainSeen = 0;
            ID3D11Texture2D* Back = nullptr;
            ID3D11RenderTargetView* Target = nullptr;
            LARGE_INTEGER LastFrame{};
            float Fade = 0;
            /// What the screen shows, kept while it fades out.
            bool Movie = false;
            bool Photo = false;
            bool WasShown = false;
            bool BlurLogged = false;
            unsigned DrawFailures = 0;
            /// While a fullscreen movie plays: the game's last frame, copied before anything is drawn on it. A paused movie
            /// presents nothing, so MovieIdle presents this frame with the pause screen over it.
            ID3D11Texture2D* Clean = nullptr;
            /// The swap chain MovieIdle presents to, referenced only while it does (a movie paused by us): the game may replace
            /// its swap chain at any other time.
            IDXGISwapChain* MovieChain = nullptr;
            /// The game's last own present (not MovieIdle's), and the swap chain of the last present of all.
            LARGE_INTEGER LastGamePresent{};
            IDXGISwapChain* LastChain = nullptr;
            bool IdleLogged = false;
            bool CleanLogged = false;
            Texts const* Text = &English;
            Fonts Font;
        };
        /// Leaked on purpose: no D3D Release from DllMain at process exit, after d3d11.dll may have cleaned up.
        RenderState& R = *new RenderState{};
        thread_local bool InPresent = false;
        /// MovieIdle's own present: the back buffer holds Clean already.
        thread_local bool SelfPresent = false;
        /// The thread that presents the game's frames: the only one that may use the device's immediate context with us.
        std::atomic<DWORD> PresentThread{ 0 };

        // ! Fonts
        // ========================================

        /// One of the fonts compiled into the ASI (Resources.rc), or null.
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
            R.Font.Light = AddEmbedded(MELEPAUSE_FONT_LIGHT);
            R.Font.Regular = AddEmbedded(MELEPAUSE_FONT_REGULAR);
            R.Font.SemiBold = AddEmbedded(MELEPAUSE_FONT_SEMIBOLD);
            R.Font.Embedded = R.Font.Light != nullptr && R.Font.Regular != nullptr && R.Font.SemiBold != nullptr;
            if (R.Font.Embedded) return;
            LEASI_WARN("overlay: the embedded Exo 2 fonts did not load: ImGui's default font instead");
            auto const fallback = ImGui::GetIO().Fonts->AddFontDefault();
            R.Font = { fallback, fallback, fallback, false };
        }

        // ! Drawing
        // ========================================

        ImU32 Colour(int r, int g, int b, float alpha)
        {
            return IM_COL32(r, g, b, static_cast<int>(std::clamp(alpha, 0.f, 1.f) * 255.f + 0.5f));
        }

        /// Exo 2's capitals stand 690 units above the baseline, and ImGui's font size spans its ascent to its descent (999 +
        /// 201 units): the capitals are this share of the size.
        constexpr float CapHeight = 690.f / 1200.f;

        /// Where a line's top goes for its capitals to centre on y.
        float CapsTop(ImFont* font, float size, float y)
        {
            return y - font->GetFontBaked(size)->Ascent + 0.5f * CapHeight * size;
        }

        /// A line with extra space between its letters (titles are set wide in the game's menus).
        float SpacedWidth(ImFont* font, float size, std::string_view text, float spacing)
        {
            auto const baked = font->GetFontBaked(size);
            float width = 0;
            int letters = 0;
            for (auto p = text.data(), end = text.data() + text.size(); p < end;)
            {
                unsigned c = 0;
                p += ImTextCharFromUtf8(&c, p, end);
                width += baked->GetCharAdvance(static_cast<ImWchar>(c));
                letters++;
            }
            return width + spacing * static_cast<float>(std::max(letters - 1, 0));
        }

        void DrawSpaced(ImDrawList* list, ImFont* font, float size, ImVec2 at, ImU32 colour, std::string_view text, float spacing)
        {
            auto const baked = font->GetFontBaked(size);
            for (auto p = text.data(), end = text.data() + text.size(); p < end;)
            {
                unsigned c = 0;
                auto const next = p + ImTextCharFromUtf8(&c, p, end);
                list->AddText(font, size, at, colour, p, next);
                at.x += baked->GetCharAdvance(static_cast<ImWchar>(c)) + spacing;
                p = next;
            }
        }

        /// Text with a soft drop shadow, its top left at <paramref name="at"/>.
        void DrawShadowed(ImDrawList* list, ImFont* font, float size, ImVec2 at, ImU32 colour, float alpha, float k, char const* text)
        {
            list->AddText(font, size, { at.x + 2 * k, at.y + 2 * k }, Colour(0, 0, 0, 0.55f * alpha), text);
            list->AddText(font, size, at, colour, text);
        }

        /// The centre: the title set wide in a thin frame, its corners bracketed, a tick on each side.
        void DrawTitle(ImDrawList* list, float width, float height, float alpha, float k)
        {
            auto const font = R.Font.Light;
            auto const size = 50 * k;
            auto const spacing = 14 * k;
            auto const text = R.Text->Title;
            auto const textWidth = SpacedWidth(font, size, text, spacing);
            ImVec2 const centre{ width * 0.5f, height * 0.47f };

            auto const halfWidth = textWidth * 0.5f + 64 * k;
            auto const halfHeight = 0.5f * CapHeight * size + 30 * k;
            ImVec2 const min{ centre.x - halfWidth, centre.y - halfHeight };
            ImVec2 const max{ centre.x + halfWidth, centre.y + halfHeight };
            auto const line = std::max(1.f, k);
            auto const thick = std::max(1.5f, 2 * k);
            auto const frame = Colour(205, 225, 245, 0.35f * alpha);
            auto const bracket = Colour(205, 225, 245, 0.95f * alpha);

            list->AddRectFilled(min, max, Colour(0, 0, 0, 0.18f * alpha));
            list->AddRect(min, max, frame, 0, line);
            auto const arm = 18 * k;
            for (int corner = 0; corner < 4; corner++)
            {
                auto const right = (corner & 1) != 0;
                auto const bottom = (corner & 2) != 0;
                ImVec2 const at{ right ? max.x : min.x, bottom ? max.y : min.y };
                auto const dx = right ? -arm : arm;
                auto const dy = bottom ? -arm : arm;
                list->AddLine(at, { at.x + dx, at.y }, bracket, thick);
                list->AddLine(at, { at.x, at.y + dy }, bracket, thick);
            }
            list->AddLine({ min.x - 14 * k, centre.y }, { min.x - 4 * k, centre.y }, bracket, line);
            list->AddLine({ max.x + 4 * k, centre.y }, { max.x + 14 * k, centre.y }, bracket, line);

            ImVec2 const at{ centre.x - textWidth * 0.5f, CapsTop(font, size, centre.y) };
            DrawSpaced(list, font, size, { at.x + 2 * k, at.y + 2 * k }, Colour(0, 0, 0, 0.5f * alpha), text, spacing);
            DrawSpaced(list, font, size, at, Colour(238, 241, 245, alpha), text, spacing);
        }

        enum class Icon { PadStart, PadA, PadY, KeyEsc, KeySpace, KeyF7 };

        bool IsPad(Icon icon) { return icon == Icon::PadStart || icon == Icon::PadA || icon == Icon::PadY; }

        char const* KeyLabel(Icon icon)
        {
            if (icon == Icon::KeyEsc) return "Esc";
            if (icon == Icon::KeySpace) return R.Text->Space;
            return "F7";
        }

        float IconWidth(Icon icon, float radius, float k)
        {
            if (IsPad(icon)) return 2 * radius;
            auto const textWidth = R.Font.SemiBold->CalcTextSizeA(0.85f * radius, FLT_MAX, 0, KeyLabel(icon)).x;
            // The space bar reads as one: wider than a key, whatever its label.
            return std::max(icon == Icon::KeySpace ? 5 * radius : 2 * radius, textWidth + 22 * k);
        }

        /// A gamepad button (a ring: Start's three lines, or A's or Y's letter in its colour) or a keycap, centred on c.
        void DrawIcon(ImDrawList* list, Icon icon, ImVec2 c, float radius, float alpha, float k)
        {
            auto const ring = Colour(226, 229, 233, 0.92f * alpha);
            auto const thick = std::max(1.5f, 2 * k);
            if (IsPad(icon))
            {
                list->AddCircleFilled(c, radius, Colour(0, 0, 0, 0.25f * alpha));
                list->AddCircle(c, radius, ring, 0, thick);
                if (icon == Icon::PadStart)
                {
                    for (int row = -1; row <= 1; row++)
                    {
                        auto const y = c.y + static_cast<float>(row) * 0.3f * radius;
                        list->AddLine({ c.x - 0.42f * radius, y }, { c.x + 0.42f * radius, y }, Colour(240, 242, 245, alpha), std::max(1.5f, 2.2f * k));
                    }
                    return;
                }
                auto const letter = icon == Icon::PadA ? "A" : "Y";
                auto const colour = icon == Icon::PadA ? Colour(108, 190, 69, alpha) : Colour(241, 196, 15, alpha);
                auto const size = 1.1f * radius;
                auto const width = R.Font.SemiBold->CalcTextSizeA(size, FLT_MAX, 0, letter).x;
                list->AddText(R.Font.SemiBold, size, { c.x - width * 0.5f, CapsTop(R.Font.SemiBold, size, c.y) }, colour, letter);
                return;
            }
            auto const label = KeyLabel(icon);
            auto const half = IconWidth(icon, radius, k) * 0.5f;
            ImVec2 const min{ c.x - half, c.y - radius };
            ImVec2 const max{ c.x + half, c.y + radius };
            list->AddRectFilled(min, max, Colour(0, 0, 0, 0.25f * alpha), 6 * k);
            list->AddRect(min, max, ring, 6 * k, thick);
            auto const size = 0.85f * radius;
            auto const width = R.Font.SemiBold->CalcTextSizeA(size, FLT_MAX, 0, label).x;
            list->AddText(R.Font.SemiBold, size, { c.x - width * 0.5f, CapsTop(R.Font.SemiBold, size, c.y) }, Colour(240, 242, 245, alpha), label);
        }

        struct Prompt
        {
            Icon What;
            char const* Label;
        };

        /// The bottom left: each key's icon over its label, side by side.
        void DrawPrompts(ImDrawList* list, float height, float alpha, float k, bool pad)
        {
            // Resume names A and the space bar; Start and Esc, which paused, resume too.
            Prompt const prompts[] = { { pad ? Icon::PadA : Icon::KeySpace, R.Text->Resume }, { pad ? Icon::PadY : Icon::KeyF7, R.Text->Photo } };
            auto const count = R.Photo ? 2 : 1;
            auto const radius = 22 * k;
            auto const labelSize = 25 * k;
            auto const labelTop = height - 84 * k - labelSize;
            auto const iconY = labelTop - 12 * k - radius;
            auto x = 88 * k;
            for (int i = 0; i < count; i++)
            {
                auto const& p = prompts[i];
                auto const labelWidth = R.Font.Regular->CalcTextSizeA(labelSize, FLT_MAX, 0, p.Label).x;
                auto const column = std::max(IconWidth(p.What, radius, k), labelWidth);
                auto const middle = x + column * 0.5f;
                DrawIcon(list, p.What, { middle, iconY }, radius, alpha, k);
                DrawShadowed(list, R.Font.Regular, labelSize, { middle - labelWidth * 0.5f, labelTop }, Colour(232, 234, 238, alpha), alpha, k, p.Label);
                x += column + 40 * k;
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
                if (kept.Width != desc.Width || kept.Height != desc.Height || kept.Format != desc.Format || kept.SampleDesc.Count != desc.SampleDesc.Count ||
                    kept.SampleDesc.Quality != desc.SampleDesc.Quality)
                {
                    R.Clean->Release();
                    R.Clean = nullptr;
                }
            }
            if (R.Clean == nullptr)
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

        bool Start(IDXGISwapChain* chain)
        {
            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(chain->GetDesc(&desc))) return false;
            if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&R.Device))) || R.Device == nullptr) return false;
            R.Device->GetImmediateContext(&R.Context);

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            // imgui.ini and its log would land next to the game's executable, inside the install. No input: nothing to click.
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.MouseDrawCursor = false;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
            io.BackendPlatformName = "MelePause";
            LoadFonts();
            // ImGui's backend compiles its shaders with D3DCompile, a delay-loaded import (CMakeLists.txt): Windows' copy is
            // loaded first, so the import binds to it, and a machine without it gets the banner, not an ASI that won't load.
            if (LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) == nullptr)
            {
                LEASI_ERROR("overlay: no d3dcompiler_47.dll in System32 (error {})", GetLastError());
                return false;
            }
            if (!ImGui_ImplDX11_Init(R.Device, R.Context)) return false;
            // The shaders compile here, at the game's first frame, not at the first pause.
            auto const blur = Blur::Init(R.Device);

            R.GameChain = chain;
            R.Text = &LanguageTexts();
            QueryPerformanceCounter(&R.LastFrame);
            LEASI_INFO("overlay ready: back buffer {}x{} format {}, present thread {}, text in {}, {}, {}", desc.BufferDesc.Width, desc.BufferDesc.Height,
                       static_cast<int>(desc.BufferDesc.Format), GetCurrentThreadId(), R.Text->Language,
                       R.Font.Embedded ? "Exo 2 embedded" : "default font", blur ? "blur ready" : "NO blur (plain darkening)");
            LEASI_FLUSH();
            return true;
        }

        /// The game's new swap chain (a display mode change): drawn on from now, on its device.
        void Adopt(IDXGISwapChain* chain)
        {
            ID3D11Device* device = nullptr;
            // Not a D3D11 swap chain: not the game's.
            if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || device == nullptr) return;
            ReleaseTarget();
            Blur::Release();
            ReleaseMovie();
            if (device != R.Device)
            {
                ImGui_ImplDX11_Shutdown();
                R.Context->Release();
                R.Device->Release();
                R.Device = device;
                R.Device->GetImmediateContext(&R.Context);
                if (!ImGui_ImplDX11_Init(R.Device, R.Context))
                {
                    LEASI_ERROR("overlay: ImGui did not start on the new swap chain's device: overlay off");
                    R.Failed = true;
                    Live.store(false, std::memory_order_release);
                    return;
                }
            }
            else device->Release();
            R.GameChain = chain;
            LEASI_INFO("overlay: drawing on swap chain {} now (the one before went silent)", static_cast<void const*>(chain));
        }

        void Frame(IDXGISwapChain* chain)
        {
            if (R.Failed)
            {
                if (!SelfPresent && (R.Clean != nullptr || R.MovieChain != nullptr)) ReleaseMovie();
                return;
            }
            if (!SelfPresent) R.LastChain = chain;
            if (!R.Ready)
            {
                R.Ready = Start(chain);
                if (!R.Ready)
                {
                    R.Failed = true;
                    LEASI_ERROR("overlay: could not start on this swap chain: the Canvas banner instead");
                    return;
                }
                Live.store(true, std::memory_order_release);
            }
            auto const now = GetTickCount64();
            if (chain != R.GameChain)
            {
                if (now - R.GameChainSeen < AdoptAfterMs) return;
                Adopt(chain);
                if (R.Failed || chain != R.GameChain) return;
            }
            R.GameChainSeen = now;
            PresentThread.store(GetCurrentThreadId(), std::memory_order_relaxed);

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
            // A movie from a file with the HUD silent may be a fullscreen one: its frame is kept for MovieIdle, before we draw
            // on it. From half the silence Bink.cpp waits for before it takes a pause, so a frame is kept when one comes.
            if (!SelfPresent)
            {
                R.LastGamePresent = time;
                auto const quiet = hud == 0 || (now > hud ? now - hud : 0) > HudSilentMs / 2;
                if (Bink::FileMovies() > 0 && quiet)
                {
                    KeepClean(chain);
                    // The game presents again: no movie paused by us.
                    if (R.MovieChain != nullptr)
                    {
                        R.MovieChain->Release();
                        R.MovieChain = nullptr;
                    }
                }
                else if (R.Clean != nullptr || R.MovieChain != nullptr)
                    ReleaseMovie();
            }
            auto const shown = world || movie;
            if (shown)
            {
                R.Movie = movie;
                R.Photo = world && (flags & OfferPhoto) != 0;
            }
            R.Fade = (flags & HideNow) != 0 ? 0.f : Blur::Step(R.Fade, shown, seconds, shown ? FadeInSeconds : FadeOutSeconds);
            if (shown != R.WasShown)
            {
                R.WasShown = shown;
                if (shown)
                    LEASI_INFO("overlay shown: {}, {} prompts{}", movie ? "movie pause" : "world pause", Pad.load(std::memory_order_relaxed) ? "gamepad" : "keyboard",
                               R.Photo ? ", photo mode offered" : "");
                else
                    LEASI_INFO("overlay hidden{}", (flags & HideNow) != 0 ? " at once (photo mode or the console)" : "");
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
                    R.Failed = true;
                    Live.store(false, std::memory_order_release);
                }
                return;
            }
            R.DrawFailures = 0;
            auto const width = static_cast<float>(desc.BufferDesc.Width);
            auto const height = static_cast<float>(desc.BufferDesc.Height);
            auto const alpha = Blur::Ease(R.Fade);

            auto const blurred = Blur::Draw(R.Context, R.Back, R.Target, alpha);
            if (!R.BlurLogged && !blurred)
            {
                R.BlurLogged = true;
                LEASI_WARN("overlay: no blur on this back buffer (see above): plain darkening");
            }

            auto& io = ImGui::GetIO();
            io.DisplaySize = { width, height };
            io.DeltaTime = std::max(seconds, 1e-4f);
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            auto const list = ImGui::GetForegroundDrawList();
            if (!blurred) list->AddRectFilled({ 0, 0 }, { width, height }, Colour(0, 0, 0, 0.6f * alpha));
            // Laid out for 1080 lines, like the game's HUD.
            auto const k = height / 1080.f;
            DrawTitle(list, width, height, alpha, k);
            DrawPrompts(list, height, alpha, k, Pad.load(std::memory_order_relaxed));
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
                R.Failed = true;
                Live.store(false, std::memory_order_release);
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
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                R.Failed = true;
                Live.store(false, std::memory_order_release);
            }
        }

        // The guard spans the original call too: an overlay or runtime between us and DXGI may present one way through the
        // other (Present1 into Present, or back), and the frame must be drawn once.
        HRESULT STDMETHODCALLTYPE Present_hook(IDXGISwapChain* chain, UINT sync, UINT flags)
        {
            auto const orig = Wait(Present_orig);
            if (InPresent) return orig(chain, sync, flags);
            InPresent = true;
            // DXGI_PRESENT_TEST only asks whether presenting would work: nothing is shown.
            if (!(flags & DXGI_PRESENT_TEST)) Guarded(chain);
            auto const hr = orig(chain, sync, flags);
            InPresent = false;
            return hr;
        }

        HRESULT STDMETHODCALLTYPE Present1_hook(IDXGISwapChain1* chain, UINT sync, UINT flags, DXGI_PRESENT_PARAMETERS const* parameters)
        {
            auto const orig = Wait(Present1_orig);
            if (InPresent) return orig(chain, sync, flags, parameters);
            InPresent = true;
            if (!(flags & DXGI_PRESENT_TEST)) Guarded(chain);
            auto const hr = orig(chain, sync, flags, parameters);
            InPresent = false;
            return hr;
        }

        /// A paused movie's present, before it is made: its last frame back into the back buffer. False when there is none to
        /// make: not yet (60 a second at most, counting the game's; the game silent for a moment first, so none slips between
        /// its own frames), or no frame of this back buffer's size kept.
        bool PrepareIdle()
        {
            if (R.Failed || R.Clean == nullptr || R.GameChain == nullptr) return false;
            LARGE_INTEGER time, frequency;
            QueryPerformanceCounter(&time);
            QueryPerformanceFrequency(&frequency);
            if (time.QuadPart - R.LastFrame.QuadPart < frequency.QuadPart / IdleRate) return false;
            if (time.QuadPart - R.LastGamePresent.QuadPart < frequency.QuadPart * IdleQuietMs / 1000) return false;
            if (R.MovieChain == nullptr)
            {
                // The movie presented to the game's swap chain a moment ago, as it paused, and nothing presented since: it is
                // alive. Referenced from now until the game presents again (Frame), so it outlives our presents.
                if (R.LastChain != R.GameChain || GetTickCount64() - R.GameChainSeen > IdleAdoptMs) return false;
                R.GameChain->AddRef();
                R.MovieChain = R.GameChain;
            }
            if (R.MovieChain != R.GameChain) return false;
            ID3D11Texture2D* back = nullptr;
            if (FAILED(R.MovieChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || back == nullptr) return false;
            D3D11_TEXTURE2D_DESC desc{}, kept{};
            back->GetDesc(&desc);
            R.Clean->GetDesc(&kept);
            // Resized while paused: the copy would be dropped, and an undefined buffer blurred.
            auto const same = desc.Width == kept.Width && desc.Height == kept.Height && desc.Format == kept.Format &&
                              desc.SampleDesc.Count == kept.SampleDesc.Count && desc.SampleDesc.Quality == kept.SampleDesc.Quality;
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

        /// Our own work only: a fault below our Present hook (DXGI, the driver, other overlays) is theirs, not ours to swallow.
        bool GuardedPrepareIdle()
        {
            __try
            {
                return PrepareIdle();
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                R.Failed = true;
                Live.store(false, std::memory_order_release);
                return false;
            }
        }

        HRESULT STDMETHODCALLTYPE ResizeBuffers_hook(IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
        {
            auto const orig = Wait(ResizeBuffers_orig);
            // Our view and the blur's copies hold the old back buffer, which must be released before it can be resized. The
            // movie's kept frame no longer fits it.
            if (chain == R.GameChain)
            {
                ReleaseTarget();
                Blur::Release();
                if (R.Clean != nullptr) R.Clean->Release();
                R.Clean = nullptr;
            }
            return orig(chain, count, width, height, format, flags);
        }
    }

    void Install(::LESDK::Initializer& Init)
    {
        // The swap chain methods' addresses, from a throwaway device on a hidden window: every swap chain shares them.
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof windowClass;
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"MelePauseProbe";
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
        IDXGISwapChain* chain = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        auto hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
        if (FAILED(hr)) hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
        if (SUCCEEDED(hr) && chain != nullptr)
        {
            // IDXGISwapChain: Present is 8, ResizeBuffers 13; IDXGISwapChain1 adds Present1 at 22.
            auto const table = *reinterpret_cast<void***>(chain);
            present = table[8];
            resize = table[13];
            IDXGISwapChain1* chain1 = nullptr;
            if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&chain1))) && chain1 != nullptr)
            {
                present1 = (*reinterpret_cast<void***>(chain1))[22];
                chain1->Release();
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
        Present_orig.store(static_cast<t_Present*>(Init.InstallHook("IDXGISwapChain::Present", present, Present_hook)), std::memory_order_release);
        if (present1 != nullptr && present1 != present)
            Present1_orig.store(static_cast<t_Present1*>(Init.InstallHook("IDXGISwapChain1::Present1", present1, Present1_hook)), std::memory_order_release);
        LEASI_INFO("overlay: swap chain hooks Present {}, Present1 {}, ResizeBuffers true{}", Present_orig.load() != nullptr, Present1_orig.load() != nullptr,
                   Present_orig.load() == nullptr ? ": no Present, the Canvas banner instead" : "");
    }

    void Publish(bool worldPause, bool offerPhoto, bool hideNow)
    {
        Published.store((worldPause ? ShowWorldPause : 0u) | (offerPhoto ? OfferPhoto : 0u) | (hideNow ? HideNow : 0u), std::memory_order_release);
    }

    void UsePadPrompts(bool pad) { Pad.store(pad, std::memory_order_relaxed); }

    void MovieIdle()
    {
        if (!Live.load(std::memory_order_acquire) || InPresent) return;
        // The movie's thread is the one that presents in LE1; on any other, the device's context isn't ours to use.
        auto const presenter = PresentThread.load(std::memory_order_relaxed);
        if (GetCurrentThreadId() != presenter)
        {
            static std::atomic<bool> logged{ false };
            if (!logged.exchange(true))
                LEASI_WARN("overlay: the paused movie plays on thread {}, frames are presented on {}: its pause screen shows only as it pauses",
                           GetCurrentThreadId(), presenter);
            return;
        }
        if (!GuardedPrepareIdle()) return;
        // Through our Present hook, which draws the pause screen; the game holds the swap chain too, our reference only for the call.
        auto const chain = R.MovieChain;
        chain->AddRef();
        SelfPresent = true;
        chain->Present(1, 0);
        SelfPresent = false;
        chain->Release();
    }

    bool Drawing() { return Live.load(std::memory_order_acquire); }
}
