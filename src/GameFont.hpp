#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

// The game's own GUI fonts for ImGui. GUI_SF_Fonts.Fonts is a GFX movie that holds them as vector outlines; this turns one of
// them into a TrueType file in memory, which ImGui's stb_truetype can read. Plain C++ with no game or ImGui types, so it runs
// anywhere. Nothing here keeps a state, writes a file or touches a global.
//
// The movie is untrusted input: a mod can replace it. Every read is checked and every size capped, and anything unexpected
// gives an empty result, never a crash. The span itself must be valid memory: that is the caller's part.
namespace MelePause::GameFont
{
    /// The three fonts, by their names in the movie. The movie has no ExportAssets tag: "$BioMass", "$AeroLight" and "$Myriad"
    /// are aliases in the game's exe, and the movie does not know them. Ids and order differ between languages (JA swaps two):
    /// only the names can be trusted.
    inline constexpr std::string_view BioMass = "Biomass_jpn";
    inline constexpr std::string_view AeroLight = "Tigensu_jpn";
    inline constexpr std::string_view Myriad = "Myriad Pro+DFGGothicP-W5";

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
