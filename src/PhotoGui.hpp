#pragma once

#include "MelePause/game/Game.hpp"

#include <cstddef>

// The GUI side of photo mode under the pause, as Photo.cpp asks for it: the panel that shows photo mode, and the
// conversation's panel set aside while it shows. Per game: LE1's and LE2's ScaleForm GUI manager (PhotoGuiSF.cpp), LE3's
// GFx interaction (PhotoGuiGFx.cpp), picked by game/LE<n>.hpp's MELEPAUSE_PHOTO_GFX. One session at a time, game thread
// only; what it set aside it keeps itself, until GiveBack or Clear.
namespace MelePause::PhotoGui
{
    /// The controller's GUI: the object whose native tick runs photo mode's panel, which the GUI tick hook lets past the
    /// world pause. Null when there is none.
    UObject* Find(ABioPlayerController* pc);

    /// Forgets the last session's state.
    void Clear();

    /// The subtitle line on screen, kept for RestoreSubtitle. Returns its length (0: none, or a game whose photo mode
    /// leaves it alone).
    std::size_t KeepSubtitle(AWorldInfo* world);

    /// Before the game's entry: the conversation's panel loses its input (it would eat the sticks, and A or Enter would
    /// pick a reply) and its picture (the wheel, the subtitle). True when a panel was set aside: GiveBack is then owed.
    bool SetAside(ABioPlayerController* pc, UObject* gui);

    /// The game's entry: EnableMode(Photo), then photo mode's panel. handler gets what the game returned, for the log.
    /// False when the game's function is missing.
    bool Show(ABioPlayerController* pc, UObject* gui, UObject*& handler);

    /// After Show, with the panel set aside: what the game's entry undid there (LE1's Activated clears the subtitle line).
    void AfterShow();

    /// Each frame of the session, with the panel set aside: hides it again if the game showed it. True when it had to (LE3's
    /// conversation movie, which advances again under photo mode, can show itself).
    bool Hold();

    /// The game's exit: photo mode's panel removed, and Photo disabled while its panel was there.
    void Hide(ABioPlayerController* pc, UObject* gui);

    /// The conversation's panel as SetAside found it, if still alive: its fields, and with calls what only a call gives back
    /// (LE3's movie keeps its visibility and input natively). Without calls, a world being torn down: nothing is called.
    void GiveBack(bool calls);

    /// The subtitle line kept at the entry, shown again unless another one shows now.
    void RestoreSubtitle();
}
