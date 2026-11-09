// PauseAnytimeTests, which ../../build.sh builds and runs after the ASIs: the checks of the rules that the threads race for,
// in KeyRules.hpp, MovieRules.hpp and OverlayRules.hpp, and of the pause screen's words in TextRules.hpp. One failed check
// fails the build. With --strings it prints the string ids instead (PrintStrings).
#include "PauseAnytime/tests/Check.hpp"
#include "PauseAnytime/TextRules.hpp"

#include <cstdio>
#include <string_view>

// --strings prints every string id the ASI asks for, with its English text, one "id<TAB>text" line each: the lines of
// docs/strings.tsv, which ../../build.sh compares with this list on every build.
static int PrintStrings()
{
    using namespace PauseAnytime::TextRules;
    for (auto const word : Words)
    {
        auto const text = BuiltInText(Language::English, word);
        std::printf("%d\t%.*s\n", IdOf(word), static_cast<int>(text.size()), text.data());
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view{ argv[1] } == "--strings") return PrintStrings();
    using namespace PauseAnytime::Tests;
    RunKeyRules();
    RunMovieRules();
    RunOverlayRules();
    RunTextRules();
    if (Failures != 0)
    {
        std::printf("PauseAnytimeTests: %d of %d checks FAILED\n", Failures, Checks);
        return 1;
    }
    std::printf("PauseAnytimeTests: %d checks passed\n", Checks);
    return 0;
}
