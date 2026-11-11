#pragma once

#include "PauseAnytime/Engine.hpp"

#include <string>

// The bypass minigames of LE1 and LE2 (game/LE<n>.hpp's PAUSEANYTIME_MINIGAMES): a panel of the ScaleForm GUI manager, its
// tag in game/LE<n>.hpp's MinigamePanels, shown in the GUI mode. Its movie keeps the game's state in a variable
// (MinigameStatePath), and the clock that can lose the game runs only while that state is MinigamePlaying, on the world's
// time: the world pause holds it, its board stays drawn, and it goes on with the time left (docs/how-it-works.md). Game
// thread only. LE3 has none: Find finds nothing there.
namespace PauseAnytime::Minigame
{
    struct Board
    {
        /// The minigame's panel (a UBioSFPanel), or null when none is open.
        UObject* Panel = nullptr;
        /// Its tag, for the logs.
        std::string Tag;
        /// Its movie's game state (MinigameStatePath); 0 when the read did not run or the movie has no such variable.
        int State = 0;
        /// Its clock runs: State is MinigamePlaying.
        bool Playing = false;
    };

    /// The minigame panel open on that controller's GUI manager (GetPanelByTag, as the game finds it), not being removed,
    /// with its movie's state.
    Board Find(ABioPlayerController* pc);

    /// A minigame's panel is open on that controller's GUI manager, as Find finds it, with no read of its movie's state:
    /// for the skip guard, which asks on a key's events.
    bool Open(ABioPlayerController* pc);

    /// That panel (Board::Panel) is still on its manager's list and not being removed. False for null.
    bool StillOpen(UObject* panel);

    /// For the logs: "none", or "<tag> state <n>".
    std::string Describe(Board const& board);
}
