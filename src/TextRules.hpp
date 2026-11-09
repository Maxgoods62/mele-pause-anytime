#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

// The pause screen's words, as plain functions of what the game's string table answered: the string ids a translation mod
// defines, the order a word is looked up in, Pause Anytime's own text in each language, the banner's pattern and the
// letters the game's fonts are cut to. Pause.cpp asks the game (GetSimpleString) and logs, Overlay.cpp draws, and
// tests/TextRulesTest.cpp checks these on every build (../build.sh). No game, SDK, ImGui or Windows type here. All text is
// UTF-8.
namespace PauseAnytime::TextRules
{
    // ! String ids
    // ========================================

    /// Pause Anytime's block of string ids, the same in LE1, LE2 and LE3: Base to Base + 99. It is mount 7430 times 10000, a
    /// mount no DLC of ours takes. Base itself is kept for the mod's name, which nothing shows yet. A translation mod defines
    /// Base + 1 to Base + 7 in its own talk files (docs/translating.md).
    inline constexpr int Base = 74300000;
    inline constexpr int BlockSize = 100;

    /// The words, in the order of their ids. Title, Resume, Photo and Space are on the pause screen; Esc, Start and Prompt
    /// only on the Canvas banner, the fallback when the pause screen can't be drawn.
    enum class Word : unsigned char { Title, Resume, Photo, Space, Esc, Start, Prompt };
    inline constexpr std::size_t WordCount = 7;
    inline constexpr std::array<Word, WordCount> Words{ Word::Title, Word::Resume, Word::Photo, Word::Space, Word::Esc, Word::Start, Word::Prompt };
    /// The words the pause screen draws (ImGui): the game's fonts are cut to their letters.
    inline constexpr std::array<Word, 4> ScreenWords{ Word::Title, Word::Resume, Word::Photo, Word::Space };

    constexpr std::size_t IndexOf(Word word) { return static_cast<std::size_t>(word); }

    /// Its string id: Base + 1 for the title, up to Base + 7 for the banner's pattern.
    constexpr int IdOf(Word word) { return Base + 1 + static_cast<int>(word); }

    /// Its name in the log.
    constexpr char const* NameOf(Word word)
    {
        constexpr std::array<char const*, WordCount> names{ "title", "resume", "photo mode", "space", "esc", "start", "prompt" };
        return names[IndexOf(word)];
    }

    // ! Languages
    // ========================================

    enum class Language : unsigned char { English, French, German, Italian, Spanish, Polish, Russian, Japanese };
    inline constexpr std::size_t LanguageCount = 8;

    constexpr std::size_t IndexOf(Language language) { return static_cast<std::size_t>(language); }

    constexpr char const* NameOf(Language language)
    {
        constexpr std::array<char const*, LanguageCount> names{ "English", "French", "German", "Italian", "Spanish", "Polish", "Russian", "Japanese" };
        return names[IndexOf(language)];
    }

    /// The text language of the code the LE launcher gives the game, upper or lower case: -OVERRIDELANGUAGE=<code> to LE1
    /// and LE2, -language=<code> to LE3. LE1's codes: FR, DE, IT, RA and PLPC with their voices, FE, GE, IE, RU and PL over
    /// English voices, ES and JA. LE2's and LE3's: FRA, DEU, ITA and POL with their voices, FRE, DEE, ITE and POE (LE2) over
    /// English voices, ESN, RUS and JPN. English for INT, an unknown code, and no code (a start past the launcher).
    inline Language LanguageOfCode(std::wstring_view code)
    {
        std::string upper;
        for (auto const c : code)
        {
            if (c > 0x7F) return Language::English;
            upper += static_cast<char>(c >= L'a' && c <= L'z' ? c - L'a' + L'A' : c);
        }
        auto const in = [&upper](std::initializer_list<std::string_view> codes) { return std::find(codes.begin(), codes.end(), upper) != codes.end(); };
        if (in({ "FR", "FE", "FRA", "FRE" })) return Language::French;
        if (in({ "DE", "GE", "DEU", "DEE" })) return Language::German;
        if (in({ "IT", "IE", "ITA", "ITE" })) return Language::Italian;
        if (in({ "ES", "ESN" })) return Language::Spanish;
        if (in({ "PL", "PLPC", "POL", "POE" })) return Language::Polish;
        if (in({ "RU", "RA", "RUS" })) return Language::Russian;
        if (in({ "JA", "JPN" })) return Language::Japanese;
        return Language::English;
    }

    // ! Pause Anytime's own text
    // ========================================

    /// Each language's text of each word, in the order of Word: shown when neither a translation mod nor the game gives
    /// one. The prompt is the banner's pattern: %1 the key, %2 what it does.
    using Row = std::array<std::string_view, WordCount>;
    inline constexpr std::array<Row, LanguageCount> BuiltInTexts{ {
        { "PAUSE", "Resume", "Photo mode", "Space", "Esc", "Start", "%1: %2" },
        { "PAUSE", "Reprendre", "Mode photo", "Espace", "Échap", "Start", "%1 : %2" },
        { "PAUSE", "Fortsetzen", "Fotomodus", "Leertaste", "Esc", "Start", "%1: %2" },
        { "PAUSA", "Riprendi", "Modalità foto", "Spazio", "Esc", "Start", "%1: %2" },
        { "PAUSA", "Reanudar", "Modo foto", "Espacio", "Esc", "Start", "%1: %2" },
        { "PAUZA", "Wznów", "Tryb fotograficzny", "Spacja", "Esc", "Start", "%1: %2" },
        { "ПАУЗА", "Продолжить", "Фоторежим", "Пробел", "Esc", "Start", "%1: %2" },
        { "ポーズ", "再開", "フォトモード", "スペース", "Esc", "Start", "%1：%2" },
    } };

    constexpr std::string_view BuiltInText(Language language, Word word) { return BuiltInTexts[IndexOf(language)][IndexOf(word)]; }

    // ! Picking a word
    // ========================================

    /// Where a word's text came from: a translation mod (our id), the game's own string, or Pause Anytime's built-in text.
    enum class Source : unsigned char { Ours, Game, BuiltIn };

    struct Pick
    {
        std::string Text;
        Source From = Source::BuiltIn;
        /// The string id it came from (Ours, Game), else 0.
        int Id = 0;
        /// BuiltIn: whose text it is.
        Language Lang = Language::English;
    };
    using Picks = std::array<Pick, WordCount>;

    /// "ours 74300001", "game 157151" or "built-in French", for the log.
    inline std::string Describe(Pick const& pick)
    {
        switch (pick.From)
        {
            case Source::Ours: return std::format("ours {}", pick.Id);
            case Source::Game: return std::format("game {}", pick.Id);
            default: return std::format("built-in {}", NameOf(pick.Lang));
        }
    }

    inline Pick BuiltInPick(Language language, Word word) { return { std::string{ BuiltInText(language, word) }, Source::BuiltIn, 0, language }; }

    inline Picks BuiltInPicks(Language language)
    {
        Picks picks;
        for (auto const word : Words) picks[IndexOf(word)] = BuiltInPick(language, word);
        return picks;
    }

    /// The token at text[at]: 1 for %1, 2 for %2, else 0. A digit right after it makes another number (%10, %21), so no
    /// token.
    constexpr int TokenAt(std::string_view text, std::size_t at)
    {
        if (at + 1 >= text.size() || text[at] != '%' || (text[at + 1] != '1' && text[at + 1] != '2')) return 0;
        if (at + 2 < text.size() && text[at + 2] >= '0' && text[at + 2] <= '9') return 0;
        return text[at + 1] - '0';
    }

    /// A pattern holds %1 and %2 once each (TokenAt).
    inline bool IsPattern(std::string_view text)
    {
        std::size_t keys = 0;
        std::size_t labels = 0;
        for (std::size_t at = 0; at < text.size(); at++)
        {
            auto const token = TokenAt(text, at);
            if (token == 1) keys++;
            if (token == 2) labels++;
        }
        return keys == 1 && labels == 1;
    }

    /// A text the word can take: not empty, and for the prompt a pattern (IsPattern).
    inline bool Usable(Word word, std::string_view text) { return !text.empty() && (word != Word::Prompt || IsPattern(text)); }

    /// A word's text: GetSimpleString of our id when a talk file defines it, else the game's string where the word has one
    /// (gameId, game/LE<n>.hpp's GameWords; 0: none), else the built-in text of the launch language. Empty means missing:
    /// for an id no talk file defines, GetSimpleString gives "" in LE1, LE2 and LE3 (tested at the main menu, 2026-11-09).
    /// The prompt takes only a pattern: another text is skipped as missing (Usable).
    inline Pick Resolve(Word word, std::string_view ours, std::string_view game, int gameId, Language language)
    {
        if (Usable(word, ours)) return { std::string{ ours }, Source::Ours, IdOf(word), Language::English };
        if (gameId != 0 && Usable(word, game)) return { std::string{ game }, Source::Game, gameId, Language::English };
        return BuiltInPick(language, word);
    }

    /// Our id answers now with a text the first read did not take: a talk file loaded after that read.
    inline bool AnswersLate(Word word, Pick const& first, std::string_view now)
    {
        return Usable(word, now) && (first.From != Source::Ours || first.Text != now);
    }

    /// The pattern with %1 replaced by the key and %2 by the label, each once, left to right (TokenAt): a key or label that
    /// holds %1 or %2 stays as it is.
    inline std::string FillPattern(std::string_view pattern, std::string_view key, std::string_view label)
    {
        std::string out;
        bool keyDone = false;
        bool labelDone = false;
        for (std::size_t i = 0; i < pattern.size(); i++)
        {
            auto const token = TokenAt(pattern, i);
            if (token == 1 && !keyDone)
            {
                out += key;
                keyDone = true;
                i++;
                continue;
            }
            if (token == 2 && !labelDone)
            {
                out += label;
                labelDone = true;
                i++;
                continue;
            }
            out += pattern[i];
        }
        return out;
    }

    // ! Letters
    // ========================================

    inline constexpr char32_t Replacement = U'�';

    struct Decoded
    {
        char32_t Code = Replacement;
        std::size_t Length = 1;
    };

    /// One code point of UTF-8 at text[at], and how many bytes it took. A bad sequence (a stray byte, a cut or overlong
    /// one, a surrogate, past U+10FFFF) gives U+FFFD for one byte.
    inline Decoded DecodeAt(std::string_view text, std::size_t at)
    {
        auto const byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
        auto const lead = byte(at);
        if (lead < 0x80) return { lead, 1 };
        std::size_t length = 0;
        char32_t code = 0;
        char32_t lowest = 0;
        if ((lead & 0xE0) == 0xC0) { length = 2; code = lead & 0x1Fu; lowest = 0x80; }
        else if ((lead & 0xF0) == 0xE0) { length = 3; code = lead & 0x0Fu; lowest = 0x800; }
        else if ((lead & 0xF8) == 0xF0) { length = 4; code = lead & 0x07u; lowest = 0x10000; }
        else return {};
        if (at + length > text.size()) return {};
        for (std::size_t i = 1; i < length; i++)
        {
            auto const next = byte(at + i);
            if ((next & 0xC0) != 0x80) return {};
            code = (code << 6) | (next & 0x3Fu);
        }
        if (code < lowest || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return {};
        return { code, length };
    }

    inline void AddLetters(std::vector<char32_t>& letters, std::string_view text)
    {
        for (std::size_t at = 0; at < text.size();)
        {
            auto const d = DecodeAt(text, at);
            letters.push_back(d.Code);
            at += d.Length;
        }
    }

    /// Every letter is in Latin-1 (U+0000 to U+00FF), with no bad sequence: the banner's Canvas font has those 256
    /// characters and no other, in all three games and every language.
    inline bool Latin1(std::string_view text)
    {
        for (std::size_t at = 0; at < text.size();)
        {
            auto const d = DecodeAt(text, at);
            if (d.Code > 0xFF) return false;
            at += d.Length;
        }
        return true;
    }

    /// The letters the game's fonts are cut to, sorted, each once: the pause screen's words as picked, the same words in
    /// the launch language's and in English built-in text (Overlay.cpp falls back to those), "F7", a space, and "?" (ImGui's
    /// fallback).
    inline std::vector<char32_t> ScreenLetters(Picks const& picks, Language language)
    {
        std::vector<char32_t> letters{ U' ', U'?' };
        for (auto const word : ScreenWords)
        {
            AddLetters(letters, picks[IndexOf(word)].Text);
            AddLetters(letters, BuiltInText(language, word));
            AddLetters(letters, BuiltInText(Language::English, word));
        }
        AddLetters(letters, "F7");
        std::sort(letters.begin(), letters.end());
        letters.erase(std::unique(letters.begin(), letters.end()), letters.end());
        return letters;
    }

    // ! The banner
    // ========================================

    /// Whose built-in text stands in on the banner for a picked word that is not all Latin-1: the launch language's where
    /// it is all Latin-1, else English's. The same order as the pause screen's (Overlay.cpp).
    inline Language BannerStandIn(Language language, Word word) { return Latin1(BuiltInText(language, word)) ? language : Language::English; }

    /// Each word's text on the Canvas banner: the picked one where it is all Latin-1, else the built-in text of
    /// BannerStandIn. Views into the picks, which must outlive the row.
    using BannerRow = std::array<std::string_view, WordCount>;
    BannerRow BannerWords(Picks&& picks, Language language) = delete;
    inline BannerRow BannerWords(Picks const& picks, Language language)
    {
        BannerRow row;
        for (auto const word : Words)
        {
            auto const& text = picks[IndexOf(word)].Text;
            row[IndexOf(word)] = Latin1(text) ? std::string_view{ text } : BuiltInText(BannerStandIn(language, word), word);
        }
        return row;
    }

    /// The banner's line under the title: the resume key with its word, then, where photo mode opens, five spaces and the
    /// photo mode key with its word. The keys: Start and Y for a gamepad, Esc and F7 for the keyboard (Y and F7 are no
    /// words to translate).
    inline std::string BannerKeys(BannerRow const& words, bool pad, bool photo)
    {
        auto const prompt = words[IndexOf(Word::Prompt)];
        auto line = FillPattern(prompt, words[IndexOf(pad ? Word::Start : Word::Esc)], words[IndexOf(Word::Resume)]);
        if (photo) line += "     " + FillPattern(prompt, pad ? "Y" : "F7", words[IndexOf(Word::Photo)]);
        return line;
    }

    // ! Reading the words
    // ========================================

    /// Pause.cpp's read of the words from the game's string table, changed by its SetTextRead only. Waiting: no game
    /// object yet, or GetSimpleString did not run. Done: every call ran, the words are picked. Checked: Done, and our ids
    /// were asked once more at the first pause, to log a talk file that loaded after the read. Off: GetSimpleString did
    /// not run in MaxTextTries tries, or a fault: the built-in text for the session.
    enum class TextRead : unsigned char { Waiting, Done, Checked, Off };

    /// Tries with a game object. The read is at the first HUD with one, the main menu's, and is not repeated: tries only
    /// cover a call that did not run. A live call at the main menu found the DLC talk files loaded in LE1, LE2 and LE3
    /// (2026-11-09); whether they are at its first frame is what the check at the first pause logs.
    inline constexpr unsigned MaxTextTries = 3;

    /// The state after try number <paramref name="tries"/> (1 for the first), whose calls all ran or not.
    constexpr TextRead AfterTry(bool called, unsigned tries)
    {
        if (called) return TextRead::Done;
        return tries >= MaxTextTries ? TextRead::Off : TextRead::Waiting;
    }
}
