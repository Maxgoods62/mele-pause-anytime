#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

// A game's own GUI fonts for ImGui. GFX movies hold them as vector outlines (game/LE<n>.hpp's PauseFonts); this turns one of them
// into a TrueType file in memory, which ImGui's stb_truetype can read. Plain C++ with no game or ImGui types, so it runs anywhere.
// Nothing here keeps a state, writes a file or touches a global.
//
// The movie is untrusted input: a mod can replace it. Every read is checked and every size capped, and anything unexpected
// gives an empty result, never a crash. The span itself must be valid memory: that is the caller's part.
namespace PauseAnytime::GameFont
{
    /// The kerning of two neighbouring characters, in EM: add it to the pen after <see cref="Left"/>.
    struct KerningPair
    {
        char32_t Left = 0;
        char32_t Right = 0;
        float Em = 0;
    };

    /// What the TrueType file cannot give ImGui.
    struct Layout
    {
        /// Ascent plus descent in EM, 0 if anything is wrong. ImGui's size is not an EM size: it scales by ascent plus descent,
        /// so text of s px per EM takes the ImGui size s * SizePerEm.
        float SizePerEm = 0;
        /// The top of the font's own H in EM, read from the whole font whatever characters were asked for; 0 when it has no H
        /// that reads, or one with no point above the baseline. A font mod may redraw the capitals: this lets the screen
        /// centre them as the font now stands.
        float CapsEm = 0;
        /// The pairs of which both characters were asked for. ImGui has no kerning: a title that should match the game draws
        /// its letters one by one with these. Empty when the font has none, or when its kerning table is damaged.
        std::vector<KerningPair> Kerning;
    };

    /// A TrueType file in memory of that font of a GFX movie, holding the glyphs of the characters asked for, or empty if
    /// anything is wrong. A character the font does not have is left out, not an error: ImGui's IsGlyphInFont tells afterwards.
    /// No character found at all is an error. Glyph 0 is an empty .notdef, 2048 units per EM.
    /// ImGui reads the file for as long as its atlas lives: keep the vector, or hand ImGui a copy it owns.
    std::vector<unsigned char> Build(std::span<unsigned char const> movie, std::string_view fontName,
                                     std::span<char32_t const> characters);

    /// The layout of that font, for those characters.
    Layout ReadLayout(std::span<unsigned char const> movie, std::string_view fontName, std::span<char32_t const> characters);
}
