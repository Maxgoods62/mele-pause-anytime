// The checks of TextRules.hpp, run by PauseAnytimeTests (Main.cpp) after the ASIs: one failed check fails the build. The
// string ids, the launcher's language codes, the built-in text, the order a word is looked up in, the banner's line and
// the letters the game's fonts are cut to. They can't show what GetSimpleString answers, which talk file wins, or what a
// font draws: those stay checks in the game (docs/testing.md).
#include "PauseAnytime/TextRules.hpp"
#include "PauseAnytime/tests/Check.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace PauseAnytime::TextRules;
    using PauseAnytime::Tests::Check;

    void Ids()
    {
        std::set<int> ids;
        for (auto const word : Words)
        {
            auto const id = IdOf(word);
            ids.insert(id);
            Check(id > Base && id < Base + BlockSize, "each word's id is inside the block, past Base (the mod's name)");
        }
        Check(ids.size() == WordCount, "the words' ids differ");
        Check(IdOf(Word::Title) == 74300001 && IdOf(Word::Resume) == 74300002 && IdOf(Word::Photo) == 74300003 && IdOf(Word::Space) == 74300004
                  && IdOf(Word::Esc) == 74300005 && IdOf(Word::Start) == 74300006 && IdOf(Word::Prompt) == 74300007,
              "the ids docs/translating.md lists");
        for (std::size_t i = 0; i < WordCount; i++) Check(IndexOf(Words[i]) == i, "Words is in the order of the ids");
        for (auto const word : ScreenWords) Check(word != Word::Esc && word != Word::Start && word != Word::Prompt, "the screen draws no banner word");
    }

    void Codes()
    {
        struct Case
        {
            wchar_t const* Code;
            Language Is;
        };
        Case const cases[] = {
            { L"", Language::English },     { L"INT", Language::English },  { L"FR", Language::French },     { L"FE", Language::French },
            { L"FRA", Language::French },   { L"FRE", Language::French },   { L"DE", Language::German },     { L"GE", Language::German },
            { L"DEU", Language::German },   { L"DEE", Language::German },   { L"IT", Language::Italian },    { L"IE", Language::Italian },
            { L"ITA", Language::Italian },  { L"ITE", Language::Italian },  { L"ES", Language::Spanish },    { L"ESN", Language::Spanish },
            { L"PL", Language::Polish },    { L"PLPC", Language::Polish },  { L"POL", Language::Polish },    { L"POE", Language::Polish },
            { L"RU", Language::Russian },   { L"RA", Language::Russian },   { L"RUS", Language::Russian },   { L"JA", Language::Japanese },
            { L"JPN", Language::Japanese }, { L"fra", Language::French },   { L"jpn", Language::Japanese },  { L"Ru", Language::Russian },
            { L"XX", Language::English },   { L"FRAN", Language::English }, { L"F", Language::English },     { L"ÉS", Language::English },
        };
        for (auto const& c : cases) Check(LanguageOfCode(c.Code) == c.Is, "a launcher code maps to its text language");
    }

    /// Code points of UTF-8.
    std::vector<char32_t> Decode(std::string_view text)
    {
        std::vector<char32_t> out;
        AddLetters(out, text);
        return out;
    }

    void OwnText()
    {
        for (std::size_t l = 0; l < LanguageCount; l++)
        {
            auto const language = static_cast<Language>(l);
            for (auto const word : Words)
            {
                auto const text = BuiltInText(language, word);
                Check(!text.empty(), "each built-in word has a text");
                auto const letters = Decode(text);
                Check(std::find(letters.begin(), letters.end(), Replacement) == letters.end(), "each built-in word is good UTF-8");
            }
            Check(IsPattern(BuiltInText(language, Word::Prompt)), "each built-in prompt holds %1 and %2 once each");
        }
        auto const english = BuiltInPicks(Language::English);
        for (auto const word : Words)
        {
            Check(Latin1(BuiltInText(Language::English, word)), "the English built-in text is Latin-1: the banner's last resort");
            auto const& pick = english[IndexOf(word)];
            Check(pick.From == Source::BuiltIn && pick.Id == 0 && pick.Lang == Language::English, "a built-in pick says so");
        }
        Check(BuiltInText(Language::Japanese, Word::Title) == "ポーズ" && BuiltInText(Language::Japanese, Word::Space) == "スペース",
              "Japanese has its own title and Space key");
        Check(BuiltInText(Language::French, Word::Esc) == "Échap" && BuiltInText(Language::French, Word::Prompt) == "%1 : %2",
              "French names Esc and spaces its colon");
    }

    void Lookup()
    {
        auto const resume = Resolve(Word::Resume, "Weiter", "Fortsetzen", 157151, Language::German);
        Check(resume.Text == "Weiter" && resume.From == Source::Ours && resume.Id == 74300002, "a translation mod's word comes first");
        auto const game = Resolve(Word::Resume, "", "Continuer", 157151, Language::French);
        Check(game.Text == "Continuer" && game.From == Source::Game && game.Id == 157151, "no translation: the game's word");
        auto const builtIn = Resolve(Word::Resume, "", "", 157151, Language::French);
        Check(builtIn.Text == "Reprendre" && builtIn.From == Source::BuiltIn && builtIn.Lang == Language::French && builtIn.Id == 0,
              "neither: the launch language's built-in text");
        auto const none = Resolve(Word::Title, "", "PAUSED", 0, Language::English);
        Check(none.Text == "PAUSE" && none.From == Source::BuiltIn, "a word with no game id ignores any game text");
        auto const title = Resolve(Word::Title, "ПАУЗА!", "", 0, Language::English);
        Check(title.Text == "ПАУЗА!" && title.From == Source::Ours, "ours beats the built-in text of any language");
        auto const japanese = Resolve(Word::Space, "", "", 0, Language::Japanese);
        Check(japanese.Text == "スペース" && Describe(japanese) == "built-in Japanese", "Japanese built-in text");
        Check(Describe(resume) == "ours 74300002" && Describe(game) == "game 157151" && Describe(builtIn) == "built-in French", "the log's source names");

        Check(Resolve(Word::Prompt, "%2 (%1)", "", 0, Language::English).Text == "%2 (%1)", "a pattern may put the label first");
        for (auto const bad : { "%1", "%2: %2", "%1 %1 %2", "key: label", "%1 %3", "%10 %2", "%1 %21", "%12" })
        {
            auto const pick = Resolve(Word::Prompt, bad, "", 0, Language::Polish);
            Check(pick.From == Source::BuiltIn && pick.Text == "%1: %2", "a prompt without %1 and %2 once each is skipped");
        }
        Check(Resolve(Word::Title, "%1", "", 0, Language::English).From == Source::Ours, "only the prompt must be a pattern");
        Check(Resolve(Word::Prompt, "%1 %2 (%10)", "", 0, Language::English).From == Source::Ours, "%10 is no %1: the pattern holds %1 once");

        auto const read = BuiltInPicks(Language::French);
        Check(AnswersLate(Word::Title, read[IndexOf(Word::Title)], "T-PAUSE"), "our id answers after the read took the built-in text");
        Check(!AnswersLate(Word::Title, read[IndexOf(Word::Title)], ""), "still no answer: nothing late");
        Check(!AnswersLate(Word::Resume, resume, "Weiter") && AnswersLate(Word::Resume, resume, "Weiter!"), "the same answer as the read, or another");
        Check(!AnswersLate(Word::Prompt, read[IndexOf(Word::Prompt)], "%1"), "a late text the prompt can't take is no answer");
    }

    void Patterns()
    {
        Check(FillPattern("%1: %2", "Esc", "Resume") == "Esc: Resume", "the English pattern");
        Check(FillPattern("%1 : %2", "Échap", "Reprendre") == "Échap : Reprendre", "the French pattern");
        Check(FillPattern("%2 (%1)", "F7", "Photo") == "Photo (F7)", "the label first");
        Check(FillPattern("%1：%2", "Esc", "再開") == "Esc：再開", "the Japanese pattern");
        Check(FillPattern("%1: %2", "%2", "%1") == "%2: %1", "a key or label holding %1 or %2 stays as it is");
        Check(FillPattern("100%: %1 %2", "a", "b") == "100%: a b", "a lone % stays");
        Check(FillPattern("%1 %2 %1", "a", "b") == "a b %1", "each token once");
        Check(FillPattern("%10 %1 %2", "K", "L") == "%10 K L", "%10 is no token");
        Check(FillPattern("%2 %1", "K", "L") == "L K", "the label first, at the end");
        Check(TokenAt("%1", 0) == 1 && TokenAt("%2", 0) == 2 && TokenAt("%1x", 0) == 1 && TokenAt("%19", 0) == 0 && TokenAt("%", 0) == 0
                  && TokenAt("%3", 0) == 0 && TokenAt("a%1", 0) == 0 && TokenAt("a%1", 1) == 1,
              "where a token is");
    }

    void Letters()
    {
        Check(Decode("A") == std::vector<char32_t>{ U'A' }, "ASCII");
        Check(Decode("é") == std::vector<char32_t>{ 0xE9 }, "two bytes");
        Check(Decode("ポ") == std::vector<char32_t>{ 0x30DD }, "three bytes");
        Check(Decode("\xF0\x9F\x98\x80") == std::vector<char32_t>{ 0x1F600 }, "four bytes");
        Check(Decode("\xFF" "A") == std::vector<char32_t>{ Replacement, U'A' }, "a stray byte is one U+FFFD");
        Check(Decode("\xC3") == std::vector<char32_t>{ Replacement }, "a cut sequence");
        Check(Decode("\xC0\xAF") == std::vector<char32_t>{ Replacement, Replacement }, "an overlong one");
        Check(Decode("\xED\xA0\x80").front() == Replacement, "a surrogate");
        Check(Decode("\xF4\x90\x80\x80").front() == Replacement, "past U+10FFFF");

        Check(Latin1("Échap") && Latin1("Wznów") && Latin1("Spacja") && Latin1("Modalità foto"), "Latin-1 words");
        Check(!Latin1("Przełącz") && !Latin1("ПАУЗА") && !Latin1("ポーズ") && !Latin1("%1：%2"), "words past Latin-1");
        Check(!Latin1("\xFF"), "a bad sequence is no Latin-1");

        auto picks = BuiltInPicks(Language::Russian);
        picks[IndexOf(Word::Title)] = Resolve(Word::Title, "ポーズ中", "", 0, Language::Russian);
        picks[IndexOf(Word::Esc)] = Resolve(Word::Esc, "Ж", "", 0, Language::Russian);
        auto const letters = ScreenLetters(picks, Language::Russian);
        auto const has = [&letters](char32_t c) { return std::binary_search(letters.begin(), letters.end(), c); };
        Check(std::is_sorted(letters.begin(), letters.end()) && std::adjacent_find(letters.begin(), letters.end()) == letters.end(), "sorted, each once");
        Check(has(U' ') && has(U'?') && has(U'F') && has(U'7'), "a space, ImGui's ?, and F7");
        Check(has(0x4E2D), "the letters of a picked word");
        Check(has(0x041F) && has(U'P') && has(U'S'), "the language's and English built-in letters");
        Check(!has(0x0416), "no letter of a word the screen does not draw");
    }

    void Banner()
    {
        auto picks = BuiltInPicks(Language::French);
        auto words = BannerWords(picks, Language::French);
        Check(BannerKeys(words, false, true) == "Échap : Reprendre     F7 : Mode photo", "French keyboard, with photo mode");
        Check(BannerKeys(words, true, false) == "Start : Reprendre", "French gamepad, no photo mode");
        Check(words[IndexOf(Word::Title)] == "PAUSE", "the title");

        picks = BuiltInPicks(Language::Russian);
        words = BannerWords(picks, Language::Russian);
        Check(words[IndexOf(Word::Title)] == "PAUSE" && words[IndexOf(Word::Resume)] == "Resume", "Russian is past Latin-1: English");
        Check(BannerKeys(words, false, false) == "Esc: Resume", "the Russian banner in English");

        picks = BuiltInPicks(Language::Japanese);
        words = BannerWords(picks, Language::Japanese);
        Check(BannerKeys(words, true, true) == "Start: Resume     Y: Photo mode", "the Japanese pattern falls back too");

        picks = BuiltInPicks(Language::Polish);
        picks[IndexOf(Word::Photo)] = Resolve(Word::Photo, "Przełącz", "", 0, Language::Polish);
        picks[IndexOf(Word::Title)] = Resolve(Word::Title, "PAUZĄ", "", 0, Language::Polish);
        words = BannerWords(picks, Language::Polish);
        Check(BannerKeys(words, false, true) == "Esc: Wznów     F7: Tryb fotograficzny", "a word past Latin-1: the language's built-in text, the others kept");
        Check(words[IndexOf(Word::Title)] == "PAUZA" && BannerStandIn(Language::Polish, Word::Title) == Language::Polish, "the Polish title stands in");

        picks = BuiltInPicks(Language::French);
        picks[IndexOf(Word::Resume)] = Resolve(Word::Resume, "Продолжить", "", 0, Language::French);
        words = BannerWords(picks, Language::French);
        Check(words[IndexOf(Word::Resume)] == "Reprendre", "a Russian translation in the French game: the French built-in text");
        Check(BannerStandIn(Language::Russian, Word::Title) == Language::English && BannerStandIn(Language::Japanese, Word::Prompt) == Language::English,
              "past Latin-1 built-in text: English stands in");
        for (std::size_t l = 0; l < LanguageCount; l++)
            for (auto const word : Words)
                Check(Latin1(BuiltInText(BannerStandIn(static_cast<Language>(l), word), word)), "whatever stands in, the banner draws it");
    }

    void Reading()
    {
        Check(AfterTry(true, 1) == TextRead::Done, "every call ran: done");
        Check(AfterTry(true, MaxTextTries) == TextRead::Done, "done on the last try too");
        Check(AfterTry(false, 1) == TextRead::Waiting && AfterTry(false, MaxTextTries - 1) == TextRead::Waiting, "a call that did not run: tried again");
        Check(AfterTry(false, MaxTextTries) == TextRead::Off, "off after the last try");
        for (unsigned tries = 1; tries <= MaxTextTries; tries++)
            Check(AfterTry(true, tries) != TextRead::Checked && AfterTry(false, tries) != TextRead::Checked, "only the check at the first pause sets Checked");
    }
}

void PauseAnytime::Tests::RunTextRules()
{
    Ids();
    Codes();
    OwnText();
    Lookup();
    Patterns();
    Letters();
    Banner();
    Reading();
}
