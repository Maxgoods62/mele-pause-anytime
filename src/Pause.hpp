#pragma once

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>
#include "Common/Base.hpp"
#include "PauseAnytime/Api.hpp"

namespace PauseAnytime
{
    // ! Pause.cpp
    // ========================================

    /// The SDK's globals, and GEngine: its first LocalPlayer is how a key finds the player's controller.
    void InitializeGlobals(::LESDK::Initializer& Init);

    /// ProcessEvent: Esc and a gamepad's Start in Console.InputKey, what the pause screen shows after BioHUD.PostRender, and
    /// the skips Esc is bound to while our key is down. False when the loader refuses the hook (a second copy of the file):
    /// then nothing else may be installed.
    bool InstallHooks(::LESDK::Initializer& Init);
}
