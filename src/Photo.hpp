#pragma once

#include <LESDK/Init.hpp>
#include "MelePause/game/Game.hpp"

// The game's photo mode over our world pause, as Pause.cpp asks for it. Game thread only. Photo.cpp, with the facts of
// game/LE<n>.hpp.
namespace MelePause::PhotoMode
{
    /// The natives that let photo mode's input, panel and sounds past the world pause. Each one missing is logged.
    void InstallHooks(::LESDK::Initializer& Init);

    /// Photo mode would answer under the pause (its input and GUI tick hooks are in): the pause screen offers its key.
    bool Answers();

    /// Our photo session runs.
    bool On();

    /// That object is our photo session's controller (false when no session runs).
    bool IsController(UObject* object);

    /// Y or F7 under our pause of a conversation or a cutscene. Returns what it did, or null when refused: Drop then gives
    /// back what it got to.
    char const* Enter(ABioPlayerController* pc, unsigned char mode, bool gamepad);

    /// Ends the session: the game's exit and ours (worldGone: field writes only, on what is still alive).
    void Drop(char const* why, bool worldGone);

    /// Every frame and every key of ours: the session ends with the world, the controller, our world pause or the mode it
    /// stands on.
    void Check(bool worldPaused, AWorldInfo* pausedWorld);

    /// After PlayerController.PlayerTick on our controller: the camera follows it in the same frame.
    void Tick(ABioPlayerController* pc, float dt);

    /// Each frame, after Check: what the game may have set again, and the camera when no PlayerTick drove it.
    void Frame();

    /// One of the controller's GUI manager's sounds (a GuiSounds 2DA row), played at once also under the world pause: the
    /// GUI sound hooks make its component bNonRealtime. False, nothing played, without those hooks or a GUI manager.
    bool PlayGuiSound(ABioPlayerController* pc, wchar_t const* row);
}
