#include "MelePause/PhotoGui.hpp"

// Photo mode's GUI in LE3: SFXGUIInteraction, the GFx interaction that opens every movie. Its ShowPhotoMode(PC) is
// EnableMode(Photo) and OpenMovie(MovieTag_PhotoMode); HidePhotoMode(PC) is DisableMode(Photo) and RemoveMovie. The
// conversation's panel is a movie too (BioSFHandler_Conversation, from GetConversationHandler): its visibility and input
// live in the native movie, so SetAside and GiveBack go through its natives.
#if MELEPAUSE_PHOTO && MELEPAUSE_PHOTO_GFX

#include "MelePause/Engine.hpp"

namespace MelePause::PhotoGui
{
    namespace
    {
        struct GuiState
        {
            Kept<UBioSFHandler_Conversation> Panel;
            bool OldVisible = false;
            bool OldInputEnabled = false;
            /// The handler's SetVisible(FALSE) clears these: they say whether the wheel (or its investigate submenu) is up.
            bool OldWheel = false;
            bool OldInvestigate = false;
        };

        GuiState State;

        struct GuiFunctions
        {
            CachedFunction GetSFXUIController;
            CachedFunction ShowPhotoMode;
            CachedFunction HidePhotoMode;
            CachedFunction GetPhotoModeHandler;
            CachedFunction GetConversationHandler;
            CachedFunction GetVisible;
            CachedFunction SetVisible;
            CachedFunction GetInputEnabled;
            CachedFunction SetInputEnabled;
        };
        GuiFunctions GFn;

        void SetPanel(UBioSFHandler_Conversation* panel, bool visible, bool inputEnabled)
        {
            UBioSFHandler_Conversation_eventSetVisible_Parms v;
            v.bVal = visible ? 1 : 0;
            Call(panel, L"SetVisible", GFn.SetVisible, v);
            USFXGUIMovie_execSetInputEnabled_Parms i;
            i.bEnabled = inputEnabled ? 1 : 0;
            Call(panel, L"SetInputEnabled", GFn.SetInputEnabled, i);
        }
    }

    UObject* Find(ABioPlayerController* pc)
    {
        ABioPlayerController_execGetSFXUIController_Parms p;
        if (!Call(pc, L"GetSFXUIController", GFn.GetSFXUIController, p)) return nullptr;
        return p.ReturnValue;
    }

    void Clear() { State = {}; }

    std::size_t KeepSubtitle(AWorldInfo* world)
    {
        LEASI_UNUSED(world);
        return 0;
    }

    bool SetAside(ABioPlayerController* pc, UObject* gui)
    {
        USFXGUIInteraction_execGetConversationHandler_Parms p;
        p.oPlayerController = pc;
        if (!Call(gui, L"GetConversationHandler", GFn.GetConversationHandler, p) || p.ReturnValue == nullptr) return false;
        auto const panel = p.ReturnValue;
        USFXGUIMovie_execGetVisible_Parms visible;
        USFXGUIMovie_execGetInputEnabled_Parms input;
        if (!Call(panel, L"GetVisible", GFn.GetVisible, visible) || !Call(panel, L"GetInputEnabled", GFn.GetInputEnabled, input)) return false;
        State.Panel.Set(panel);
        State.OldVisible = visible.ReturnValue != 0;
        State.OldInputEnabled = input.ReturnValue != 0;
        State.OldWheel = panel->m_bDisplayingWheel != 0;
        State.OldInvestigate = panel->m_bDisplayInvestigateSubMenu != 0;
        SetPanel(panel, false, false);
        LEASI_INFO("photo mode: conversation movie {} set aside (it was {}, input {}, wheel {}, investigate {})", PathOf(panel),
                   State.OldVisible ? "visible" : "hidden", State.OldInputEnabled ? "on" : "off", State.OldWheel, State.OldInvestigate);
        return true;
    }

    bool Show(ABioPlayerController* pc, UObject* gui, UObject*& handler)
    {
        auto const show = FunctionOf(gui, L"ShowPhotoMode", GFn.ShowPhotoMode);
        if (show == nullptr) return false;
        USFXGUIInteraction_execShowPhotoMode_Parms p;
        p.oPlayerController = pc;
        CallGame(gui, show, &p);
        USFXGUIInteraction_execGetPhotoModeHandler_Parms shown;
        shown.oPlayerController = pc;
        handler = Call(gui, L"GetPhotoModeHandler", GFn.GetPhotoModeHandler, shown) ? shown.ReturnValue : nullptr;
        return true;
    }

    void AfterShow() {}

    bool Hold()
    {
        auto const panel = State.Panel.Live();
        if (panel == nullptr) return false;
        USFXGUIMovie_execGetVisible_Parms visible;
        if (!Call(panel, L"GetVisible", GFn.GetVisible, visible) || visible.ReturnValue == 0) return false;
        UBioSFHandler_Conversation_eventSetVisible_Parms v;
        v.bVal = 0;
        Call(panel, L"SetVisible", GFn.SetVisible, v);
        return true;
    }

    void Hide(ABioPlayerController* pc, UObject* gui)
    {
        if (pc == nullptr) return;
        USFXGUIInteraction_execHidePhotoMode_Parms p;
        p.oPlayerController = pc;
        Call(gui, L"HidePhotoMode", GFn.HidePhotoMode, p);
    }

    void GiveBack(bool calls)
    {
        auto const panel = State.Panel.Live();
        if (panel == nullptr) return;
        // The flags after the calls: SetVisible(FALSE), for a panel hidden before ours, would clear them again.
        if (calls) SetPanel(panel, State.OldVisible, State.OldInputEnabled);
        panel->m_bDisplayingWheel = State.OldWheel ? 1 : 0;
        panel->m_bDisplayInvestigateSubMenu = State.OldInvestigate ? 1 : 0;
    }

    void RestoreSubtitle() {}
}

#endif
