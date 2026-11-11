#include "PauseAnytime/Minigame.hpp"

#include <cstring>
#include <format>
#include <string_view>

namespace PauseAnytime::Minigame
{
#if PAUSEANYTIME_MINIGAMES
    namespace
    {
        struct Functions
        {
            CachedFunction GetScaleFormManager;
            CachedFunction GetPanelByTag;
            CachedFunction PanelExists;
            CachedFunction GetVariableInt;
        };
        Functions Fn;

        UBioSFManager* GuiManagerOf(ABioPlayerController* pc)
        {
            if (pc == nullptr) return nullptr;
            APlayerController_execGetScaleFormManager_Parms p;
            if (!Call(pc, L"GetScaleFormManager", Fn.GetScaleFormManager, p)) return nullptr;
            return p.ReturnValue;
        }

        /// The movie's variable at MinigameStatePath, read by the panel's native (Scaleform's GetVariable): 0 for a path the
        /// movie does not have, as for a movie a mod replaced.
        int StateOf(UBioSFPanel* panel)
        {
            FString const path{ Game::MinigameStatePath };
            UBioSFPanel_execGetVariableInt_Parms p;
            std::memcpy(&p.sPath, &path, sizeof(FString));
            if (!Call(panel, L"GetVariableInt", Fn.GetVariableInt, p)) return 0;
            return p.ReturnValue;
        }

        struct Found
        {
            UBioSFPanel* Panel = nullptr;
            wchar_t const* Tag = nullptr;
        };

        /// The first of MinigamePanels' tags with a panel on the GUI manager that is not being removed.
        Found OpenOn(ABioPlayerController* pc)
        {
            auto const gui = GuiManagerOf(pc);
            if (gui == nullptr) return {};
            for (auto const tag : Game::MinigamePanels)
            {
                UBioSFManager_execGetPanelByTag_Parms p;
                p.nmMovieTag = SFXName{ tag, 0 };
                if (!Call(gui, L"GetPanelByTag", Fn.GetPanelByTag, p)) return {};
                auto const panel = p.ReturnValue;
                if (panel == nullptr || panel->bToBeRemoved != 0) continue;
                return { panel, tag };
            }
            return {};
        }
    }

    Board Find(ABioPlayerController* pc)
    {
        auto const found = OpenOn(pc);
        if (found.Panel == nullptr) return {};
        Board board;
        board.Panel = found.Panel;
        board.Tag = ToUtf8(found.Tag);
        board.State = StateOf(found.Panel);
        board.Playing = board.State == Game::MinigamePlaying;
        return board;
    }

    bool Open(ABioPlayerController* pc) { return OpenOn(pc).Panel != nullptr; }

    bool StillOpen(UObject* object)
    {
        if (object == nullptr || !object->IsA(UBioSFPanel::StaticClass())) return false;
        auto const panel = static_cast<UBioSFPanel*>(object);
        auto const gui = panel->oParentManager;
        if (panel->bToBeRemoved != 0 || gui == nullptr) return false;
        UBioSFManager_execPanelExists_Parms p;
        p.oPanel = panel;
        if (!Call(gui, L"PanelExists", Fn.PanelExists, p)) return false;
        return p.ReturnValue != 0;
    }
#else
    Board Find(ABioPlayerController*) { return {}; }

    bool Open(ABioPlayerController*) { return false; }

    bool StillOpen(UObject*) { return false; }
#endif

    std::string Describe(Board const& board)
    {
        if (board.Panel == nullptr) return "none";
        return std::format("{} state {}", board.Tag, board.State);
    }
}
