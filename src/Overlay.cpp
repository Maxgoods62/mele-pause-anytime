#include "MelePause/Api.hpp"
#include "MelePause/Blur.hpp"
#include "MelePause/GameFont.hpp"
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
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The pause screen, drawn into the game's back buffer from a hooked IDXGISwapChain::Present on the render thread: over our
// world pause and our movie pause the frame is blurred and darkened (Blur.cpp), "PAUSE" stands in a bracketed frame at the
// centre, and the keys are named at the bottom left (resume, and photo mode where it opens), for the gamepad or the keyboard,
// whichever pressed our keys last. In LE1 and LE2 it wears the game's menu colours instead (OverlayLook, NativeLook): the title
// on a band, each key left of its label. The game thread says what to show (Publish, UsePadPrompts, UseGameTexts, UseGameIcons,
// UseGameFonts); nothing here touches a game object, only the D3D11 views of two of its textures and, on the game thread, the
// bytes of its fonts' movies. The text is set in Exo 2 (SIL OFL 1.1, fonts/OFL.txt), compiled into the ASI (MelePause.rc), in the
// language the game's launcher passes, or in the game's own words where it gives them, and in LE1 and LE2 in the game's own
// fonts, turned into TrueType in memory (GameFont.cpp) and never written anywhere. Without the hook or after a fault the Canvas
// banner of Pause.cpp is drawn instead (Drawing).
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
        /// Install's, set before the hooks go live.
        std::atomic<OverlayLook> Look{ OverlayLook::Own };

        /// UseGameTexts', UTF-8. Never freed: the render thread draws from it.
        struct GameTexts
        {
            std::string Title;
            std::string Resume;
            std::string Photo;
        };
        std::atomic<GameTexts const*> GameText{ nullptr };

        /// UseGameIcons', not checked yet. Never freed, like GameTexts.
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

        /// The text language the LE launcher gives the game: -OVERRIDELANGUAGE=<code> to LE1 and LE2, -language=<code> to LE3.
        /// LE1's codes: FR, DE, IT, RA and PLPC with their voices, FE, GE, IE, RU and PL over English voices, ES and JA. LE2's
        /// and LE3's: FRA, DEU, ITA and POL with their voices, FRE, DEE, ITE and POE (LE2) over English voices, ESN, RUS and
        /// JPN. English for INT, without a switch (a start past the launcher), and for Japanese: Exo 2 has no kana.
        Texts const& LanguageTexts()
        {
            std::wstring_view const line = GetCommandLineW();
            std::wstring code;
            for (std::wstring_view const option : { std::wstring_view{ L"-OVERRIDELANGUAGE=" }, std::wstring_view{ L"-LANGUAGE=" } })
            {
                auto const at = std::search(line.begin(), line.end(), option.begin(), option.end(),
                                            [](wchar_t a, wchar_t b) { return std::towupper(a) == std::towupper(b); });
                if (at == line.end()) continue;
                for (auto c = at + option.size(); c != line.end() && *c != L' ' && *c != L'\t' && *c != L'"'; ++c)
                    code += static_cast<wchar_t>(std::towupper(*c));
                break;
            }
            if (code == L"FR" || code == L"FE" || code == L"FRA" || code == L"FRE") return French;
            if (code == L"DE" || code == L"GE" || code == L"DEU" || code == L"DEE") return German;
            if (code == L"IT" || code == L"IE" || code == L"ITA" || code == L"ITE") return Italian;
            if (code == L"ES" || code == L"ESN") return Spanish;
            if (code == L"PL" || code == L"PLPC" || code == L"POL" || code == L"POE") return Polish;
            if (code == L"RU" || code == L"RA" || code == L"RUS") return Russian;
            return English;
        }

        /// Every letter the pause screen may set, ours and the game's, and ImGui's two fallbacks: the game's fonts are cut to these.
        std::vector<char32_t> ScreenLetters()
        {
            std::vector<char32_t> letters{ U' ', U'?' };
            auto const add = [&letters](std::string_view text) {
                for (auto p = text.data(), end = text.data() + text.size(); p < end;)
                {
                    unsigned c = 0;
                    p += ImTextCharFromUtf8(&c, p, end);
                    letters.push_back(static_cast<char32_t>(c));
                }
            };
            auto const& ours = LanguageTexts();
            for (auto const text : { ours.Title, ours.Resume, ours.Photo, ours.Space, "Esc", "F7" }) add(text);
            if (auto const game = GameText.load(std::memory_order_acquire); game != nullptr)
            {
                add(game->Title);
                add(game->Resume);
                add(game->Photo);
            }
            return letters;
        }

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
            /// The band's rules and short ticks.
            Rgb Line;
            Rgb PanelTop;
            Rgb PanelBottom;
            /// A cast over the blurred frame, toward the game's menus.
            Blur::Settings Blur;
            GameLine TitleLine;
            GameLine LabelLine;
            GameLine KeyLine;
            /// The height of the fonts' H, in EM.
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

        /// Null for our own look.
        NativeLook const* NativeLookOf(OverlayLook look)
        {
            if (look == OverlayLook::LE1) return &Le1Look;
            if (look == OverlayLook::LE2) return &Le2Look;
            return nullptr;
        }

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
            /// Null for our own look.
            NativeLook const* Native = nullptr;
            Texts const* Text = &English;
            /// The language's texts with the game's words in place of ours (Text points here once they came).
            Texts Mixed = English;
            bool GameTextsSeen = false;
            /// The game's gamepad buttons, referenced from when they passed TakeView: both or neither.
            ID3D11ShaderResourceView* IconA = nullptr;
            ID3D11ShaderResourceView* IconY = nullptr;
            bool GameIconsSeen = false;
            bool GameFontsSeen = false;
            Fonts Font;
            NativeStyles Style;
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

        /// One of the fonts compiled into the ASI (MelePause.rc), or null.
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
            if (!R.Font.Embedded)
            {
                LEASI_WARN("overlay: the embedded Exo 2 fonts did not load: ImGui's default font instead");
                auto const fallback = ImGui::GetIO().Fonts->AddFontDefault();
                R.Font = { fallback, fallback, fallback, false };
            }
            R.Style = { { R.Font.Regular, 58, 18 }, { R.Font.Regular, 28, 0 }, { R.Font.SemiBold, 19.55f, 0 } };
        }

        ImFont* TitleFont() { return R.Native != nullptr ? R.Style.Title.Font : R.Font.Light; }

        /// The font has every letter of a text that has some.
        bool Drawable(ImFont* font, std::string const& text)
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

        /// Once UseGameTexts gave them: the game's words in place of ours, each one its font can draw.
        void TakeGameTexts()
        {
            if (R.GameTextsSeen) return;
            auto const game = GameText.load(std::memory_order_acquire);
            if (game == nullptr) return;
            R.GameTextsSeen = true;
            R.Mixed = *R.Text;
            auto const take = [](ImFont* font, std::string const& text, char const*& slot) {
                if (!Drawable(font, text)) return "kept ours";
                slot = text.c_str();
                return "used";
            };
            auto const title = take(TitleFont(), game->Title, R.Mixed.Title);
            auto const resume = take(R.Style.Label.Font, game->Resume, R.Mixed.Resume);
            auto const photo = take(R.Style.Label.Font, game->Photo, R.Mixed.Photo);
            R.Text = &R.Mixed;
            LEASI_INFO("overlay: the game's texts: title \"{}\" {}, resume \"{}\" {}, photo mode \"{}\" {}", game->Title, title, game->Resume, resume,
                       game->Photo, photo);
        }

        /// Null if ImGui can't read it.
        ImFont* AddGameFont(GameFontFile const& file)
        {
            ImFontConfig config;
            // Kept for good, as GameFonts says: the atlas must not free it.
            config.FontDataOwnedByAtlas = false;
            return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(file.Ttf.data()), static_cast<int>(file.Ttf.size()), 32.f, &config);
        }

        LineStyle GameStyle(ImFont* font, GameFontFile const& file, GameLine const& line, float caps)
        {
            auto const perEm = file.Layout.SizePerEm;
            return { font, line.Em * perEm, line.Spacing, caps / perEm, file.Layout.Kerning, perEm };
        }

        /// Once UseGameFonts gave them: the game's fonts in place of Exo 2, both or neither. They must draw our words too, since
        /// those stand in for any word of the game's they can't. Between ImGui's frames, and ahead of TakeGameTexts.
        void TakeGameFonts()
        {
            if (R.GameFontsSeen) return;
            auto const game = GameFontFiles.load(std::memory_order_acquire);
            if (game == nullptr || R.Native == nullptr) return;
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
                return line.Bold ? GameStyle(bold, game->Bold, line, look.BoldCaps) : GameStyle(light, game->Light, line, look.LightCaps);
            };
            NativeStyles const styles{ style(look.TitleLine), style(look.LabelLine), style(look.KeyLine) };
            auto const& ours = LanguageTexts();
            if (!Drawable(styles.Title.Font, ours.Title) || !Drawable(styles.Label.Font, ours.Resume) || !Drawable(styles.Label.Font, ours.Photo) ||
                !Drawable(styles.Key.Font, ours.Space) || !Drawable(styles.Key.Font, "F7"))
            {
                LEASI_WARN("overlay: the game's fonts lack letters of the pause screen: Exo 2 stays");
                return;
            }
            R.Style = styles;
            // The game's words are judged again, by these fonts.
            R.Text = &ours;
            R.GameTextsSeen = false;
            LEASI_INFO("overlay: the game's fonts: {} and {} bytes of TrueType, {} and {} kerning pairs: used", game->Light.Ttf.size(), game->Bold.Ttf.size(),
                       game->Light.Layout.Kerning.size(), game->Bold.Layout.Kerning.size());
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

        /// The method table of this device's views, read off a view of our own on a one-pixel texture. Null without one. Any
        /// overlay or driver in between shows here as it shows on the game's.
        void const* ViewTable()
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
            if (FAILED(R.Device->CreateTexture2D(&desc, nullptr, &texture)) || texture == nullptr) return nullptr;
            ID3D11ShaderResourceView* view = nullptr;
            auto const hr = R.Device->CreateShaderResourceView(texture, nullptr, &view);
            texture->Release();
            if (FAILED(hr) || view == nullptr) return nullptr;
            auto const table = TableOf(view);
            view->Release();
            return table;
        }

        /// The game's pointer, referenced, if it is a view as ours are, of a 2D texture, on the device we draw with. Nothing of
        /// it is called before its method table matched.
        ID3D11ShaderResourceView* TakeView(void* candidate, void const* table)
        {
            if (candidate == nullptr || table == nullptr || TableOf(candidate) != table) return nullptr;
            auto const view = static_cast<ID3D11ShaderResourceView*>(candidate);
            ID3D11Device* device = nullptr;
            view->GetDevice(&device);
            if (device != nullptr) device->Release();
            D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
            view->GetDesc(&desc);
            if (device != R.Device || desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) return nullptr;
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
            auto const table = ViewTable();
            R.IconA = TakeView(game->PadA, table);
            R.IconY = TakeView(game->PadY, table);
            auto const a = R.IconA != nullptr;
            auto const y = R.IconY != nullptr;
            if (!a || !y) ReleaseGameIcons();
            LEASI_INFO("overlay: the game's gamepad icons: A {}, Y {}: {}", a ? "fine" : "refused", y ? "fine" : "refused", a && y ? "used" : "ours drawn instead");
        }

        /// A fault in there costs the icons, not the overlay. No C++ object may live in this frame.
        bool GuardedGameIcons()
        {
            __try
            {
                TakeGameIcons();
                return true;
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
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

        /// A line with its style's space between its letters (titles are set wide in our own look) and its kerning. ImGui bakes
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

        /// Text with a soft drop shadow, its top left at <paramref name="at"/>.
        void DrawShadowed(ImDrawList* list, ImFont* font, float size, ImVec2 at, ImU32 colour, float alpha, float k, char const* text)
        {
            list->AddText(font, size, { at.x + 2 * k, at.y + 2 * k }, Colour(0, 0, 0, 0.55f * alpha), text);
            list->AddText(font, size, at, colour, text);
        }

        /// The centre: the title set wide in a thin frame, its corners bracketed, a tick on each side.
        void DrawTitle(ImDrawList* list, float width, float height, float alpha, float k)
        {
            LineStyle const style{ R.Font.Light, 50, 14 };
            auto const text = R.Text->Title;
            auto const textWidth = SpacedWidth(style, k, text);
            ImVec2 const centre{ width * 0.5f, height * 0.47f };

            auto const halfWidth = textWidth * 0.5f + 64 * k;
            auto const halfHeight = 0.5f * CapHeight * style.Size * k + 30 * k;
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

            ImVec2 const at{ centre.x - textWidth * 0.5f, CapsTop(style, k, centre.y) };
            DrawSpaced(list, style, k, { at.x + 2 * k, at.y + 2 * k }, Colour(0, 0, 0, 0.5f * alpha), text);
            DrawSpaced(list, style, k, at, Colour(238, 241, 245, alpha), text);
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
            auto const textWidth = R.Native != nullptr ? SpacedWidth(R.Style.Key, k, KeyLabel(icon))
                                                       : R.Font.SemiBold->CalcTextSizeA(0.85f * radius, FLT_MAX, 0, KeyLabel(icon)).x;
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

        // ! A game's look
        // ========================================

        // The gamepad's buttons are the game's own textures, drawn from its own views of them (UseGameIcons), or without those
        // drawn to look like them (a disc in the button's colour, a dark rim, a dark letter). The keys are drawn as
        // GUI_SF_PhotoMode's translucent grey keycaps, the same in LE1 and LE2.

        constexpr Rgb DiscA{ 0x7B, 0x9E, 0x3F };
        constexpr Rgb DiscY{ 0xFC, 0xD3, 0x05 };
        constexpr Rgb DiscStart{ 0xCB, 0xFF, 0xFF };
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

        /// The centre: the title on a band like the game's menu bars, a rule along each edge, a short rule beyond each end.
        void DrawBandTitle(ImDrawList* list, float width, float height, float alpha, float k)
        {
            auto const& look = *R.Native;
            auto const& style = R.Style.Title;
            auto const text = R.Text->Title;
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
            for (auto const side : { -1.f, 1.f })
            {
                auto const from = centre.x + side * (halfWidth + 14 * k);
                list->AddLine({ from, centre.y }, { from + side * 26 * k, centre.y }, Colour(look.Line, 0.9f * alpha), rule);
            }

            ImVec2 const at{ centre.x - textWidth * 0.5f, CapsTop(style, k, centre.y) };
            DrawSpaced(list, style, k, { at.x + 3 * k, at.y + 4 * k }, Colour(0, 0, 0, 0.25f * alpha), text);
            DrawSpaced(list, style, k, { at.x + 2 * k, at.y + 3 * k }, Colour(0, 0, 0, 0.4f * alpha), text);
            DrawSpaced(list, style, k, at, Colour(look.Title, alpha), text);
        }

        /// A key in the game's style, centred on c: a gamepad button (the game's texture of it where we have one, else a disc
        /// in its colour, darker toward a black rim, its letter dark; Start pale with three dark lines), or a keycap (translucent
        /// grey as the art is stored, lighter toward the bottom).
        void DrawGameIcon(ImDrawList* list, Icon icon, ImVec2 c, float radius, float alpha, float k)
        {
            if (IsPad(icon))
            {
                auto const texture = icon == Icon::PadA ? R.IconA : icon == Icon::PadY ? R.IconY : nullptr;
                if (texture != nullptr)
                {
                    auto const& art = R.Native->Button;
                    auto const texel = 2 * radius / art.Disc;
                    ImVec2 const min{ c.x - art.CentreX * texel, c.y - art.CentreY * texel };
                    list->AddImage(reinterpret_cast<ImTextureID>(texture), min, { min.x + art.Size * texel, min.y + art.Size * texel }, { 0, 0 }, { 1, 1 },
                                   Colour(255, 255, 255, alpha));
                    return;
                }
                auto const fill = icon == Icon::PadA ? DiscA : icon == Icon::PadY ? DiscY : DiscStart;
                list->AddCircleFilled(c, radius, Colour(0, 0, 0, 0.5f * alpha));
                list->AddCircleFilled(c, 0.95f * radius, Colour(fill, alpha, 0.3f));
                list->AddCircleFilled(c, 0.9f * radius, Colour(fill, alpha, 0.55f));
                list->AddCircleFilled(c, 0.86f * radius, Colour(fill, alpha, 0.75f));
                list->AddCircleFilled(c, 0.82f * radius, Colour(fill, alpha));
                auto const ink = Colour(0, 0, 0, 0.75f * alpha);
                if (icon == Icon::PadStart)
                {
                    for (int row = -1; row <= 1; row++)
                    {
                        auto const y = c.y + static_cast<float>(row) * 0.28f * radius;
                        list->AddLine({ c.x - 0.4f * radius, y }, { c.x + 0.4f * radius, y }, ink, std::max(1.5f, 2.6f * k));
                    }
                    return;
                }
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
            Prompt const prompts[] = { { pad ? Icon::PadA : Icon::KeySpace, R.Text->Resume }, { pad ? Icon::PadY : Icon::KeyF7, R.Text->Photo } };
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
            R.Native = NativeLookOf(Look.load(std::memory_order_relaxed));
            R.Text = &LanguageTexts();
            QueryPerformanceCounter(&R.LastFrame);
            LEASI_INFO("overlay ready: back buffer {}x{} format {}, present thread {}, {} look, text in {}, {}, {}", desc.BufferDesc.Width,
                       desc.BufferDesc.Height, static_cast<int>(desc.BufferDesc.Format), GetCurrentThreadId(), R.Native != nullptr ? R.Native->Name : "our own",
                       R.Text->Language, R.Font.Embedded ? "Exo 2 embedded" : "default font", blur ? "blur ready" : "NO blur (plain darkening)");
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
                if (R.IconA != nullptr) LEASI_INFO("overlay: the game's gamepad icons were on the old device: ours drawn from now on");
                ReleaseGameIcons();
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

            auto const blurred = Blur::Draw(R.Context, R.Back, R.Target, alpha, R.Native != nullptr ? R.Native->Blur : Blur::Settings{});
            if (!R.BlurLogged && !blurred)
            {
                R.BlurLogged = true;
                LEASI_WARN("overlay: no blur on this back buffer (see above): plain darkening");
            }

            TakeGameFonts();
            auto& io = ImGui::GetIO();
            io.DisplaySize = { width, height };
            io.DeltaTime = std::max(seconds, 1e-4f);
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            auto const list = ImGui::GetForegroundDrawList();
            if (!blurred) list->AddRectFilled({ 0, 0 }, { width, height }, Colour(0, 0, 0, 0.6f * alpha));
            TakeGameTexts();
            // Laid out for 1080 lines, like the game's HUD.
            auto const k = height / 1080.f;
            auto const pad = Pad.load(std::memory_order_relaxed);
            if (R.Native != nullptr)
            {
                DrawBandTitle(list, width, height, alpha, k);
                DrawGamePrompts(list, height, alpha, k, pad);
            }
            else
            {
                DrawTitle(list, width, height, alpha, k);
                DrawPrompts(list, height, alpha, k, pad);
            }
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

    void Install(::LESDK::Initializer& Init, OverlayLook look)
    {
        Look.store(look, std::memory_order_relaxed);
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

    void UseGameTexts(std::string title, std::string resume, std::string photo)
    {
        auto const texts = new GameTexts{ std::move(title), std::move(resume), std::move(photo) };
        GameTexts const* none = nullptr;
        if (!GameText.compare_exchange_strong(none, texts, std::memory_order_acq_rel)) delete texts;
    }

    void UseGameIcons(void* padA, void* padY)
    {
        auto const icons = new GameIcons{ padA, padY };
        GameIcons const* none = nullptr;
        if (!GameIcon.compare_exchange_strong(none, icons, std::memory_order_acq_rel)) delete icons;
    }

    void UseGameFonts(GameFontSource light, GameFontSource bold)
    {
        auto const letters = ScreenLetters();
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
