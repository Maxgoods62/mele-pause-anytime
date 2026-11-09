// PauseAnytimeTests, which ../../build.sh builds and runs after the ASIs: the checks of the rules that the threads race for,
// in KeyRules.hpp, MovieRules.hpp and OverlayRules.hpp, and of the pause screen's words in TextRules.hpp. One failed check
// fails the build.
#include "PauseAnytime/tests/Check.hpp"

#include <cstdio>

int main()
{
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
