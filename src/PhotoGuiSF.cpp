#include "MelePause/PhotoGui.hpp"

// Photo mode's GUI in LE1 and LE2: the ScaleForm GUI manager (MassEffectGuiManager, a BioSFManager), whose ShowPhotoMode and
// HidePhotoMode open and close the photo panel, and whose conversation panel (game/LE<n>.hpp ConversationPanelOf) is a
// BioSFPanel with its own input and visibility flags.
#if !MELEPAUSE_PHOTO_GFX

#include "MelePause/Engine.hpp"

#include <cstring>
#include <string>

namespace MelePause::PhotoGui
{
    namespace
    {
        struct GuiState
        {
            Kept<UBioSFPanel> Panel;
            bool OldPanelInputDisabled = false;
            bool OldPanelHandleInput = false;
            bool OldPanelVisible = false;
            Kept<UBioSubtitles> Subtitles;
            /// The subtitle line on screen: LE1's photo mode Activated clears it.
            std::wstring Subtitle;
            float SubtitleTime = 0.0f;
        };

        GuiState State;

        struct GuiFunctions
        {
            CachedFunction GetScaleFormManager;
            CachedFunction ShowPhotoMode;
            CachedFunction HidePhotoMode;
            CachedFunction DisplaySubtitle;
        };
        GuiFunctions GFn;
    }

    UObject* Find(ABioPlayerController* pc)
    {
        APlayerController_execGetScaleFormManager_Parms p;
        if (!Call(pc, L"GetScaleFormManager", GFn.GetScaleFormManager, p)) return nullptr;
        auto const gui = p.ReturnValue;
        return gui != nullptr && gui->IsA(UMassEffectGuiManager::StaticClass()) ? gui : nullptr;
    }

    void Clear() { State = {}; }

    std::size_t KeepSubtitle(AWorldInfo* world)
    {
        if (world == nullptr || !world->IsA(ABioWorldInfo::StaticClass())) return 0;
        auto const subtitles = static_cast<ABioWorldInfo*>(world)->m_Subtitles;
        if (subtitles == nullptr) return 0;
        State.Subtitles.Set(subtitles);
        State.Subtitle.assign(subtitles->m_sSubtitle.Chars(), subtitles->m_sSubtitle.Length());
        State.SubtitleTime = subtitles->m_fTimeRemaining;
        return State.Subtitle.size();
    }

    bool SetAside(ABioPlayerController* pc, UObject* gui)
    {
        LEASI_UNUSED(pc);
        auto const panel = Game::ConversationPanelOf(static_cast<UMassEffectGuiManager*>(gui));
        if (panel == nullptr) return false;
        State.Panel.Set(panel);
        State.OldPanelInputDisabled = panel->bInputDisabled != 0;
        State.OldPanelHandleInput = panel->bHandleInputThisFrame != 0;
        State.OldPanelVisible = panel->IsVisible != 0;
        panel->bInputDisabled = 1;
        panel->bHandleInputThisFrame = 1;
        panel->IsVisible = 0;
        return true;
    }

    bool Show(ABioPlayerController* pc, UObject* gui, UObject*& handler)
    {
        LEASI_UNUSED(pc);
        auto const show = FunctionOf(gui, L"ShowPhotoMode", GFn.ShowPhotoMode);
        if (show == nullptr) return false;
        UMassEffectGuiManager_execShowPhotoMode_Parms shown;
        CallGame(gui, show, &shown);
        handler = shown.ReturnValue;
        return true;
    }

    void AfterShow() { RestoreSubtitle(); }

    bool Hold() { return false; }

    void Hide(ABioPlayerController* pc, UObject* gui)
    {
        LEASI_UNUSED(pc);
        UMassEffectGuiManager_execHidePhotoMode_Parms p;
        Call(gui, L"HidePhotoMode", GFn.HidePhotoMode, p);
    }

    void GiveBack(bool calls)
    {
        LEASI_UNUSED(calls);
        if (auto const panel = State.Panel.Live())
        {
            panel->bInputDisabled = State.OldPanelInputDisabled ? 1 : 0;
            panel->bHandleInputThisFrame = State.OldPanelHandleInput ? 1 : 0;
            panel->IsVisible = State.OldPanelVisible ? 1 : 0;
        }
    }

    void RestoreSubtitle()
    {
        auto const subtitles = State.Subtitles.Live();
        if (subtitles == nullptr || State.Subtitle.empty() || subtitles->m_sSubtitle.Length() != 0) return;
        FString const text{ State.Subtitle.c_str() };
        UBioSubtitles_execDisplaySubtitle_Parms p;
        std::memcpy(&p.sSubtitle, &text, sizeof(FString));
        p.fDuration = State.SubtitleTime;
        Call(subtitles, L"DisplaySubtitle", GFn.DisplaySubtitle, p);
    }
}

#endif
