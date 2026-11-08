#pragma once

#include <cstdio>
#include <source_location>
#include <string_view>

// What the checks of PauseAnytimeTests share: one count of checks and failures for the whole program, which Main.cpp
// prints, and each file's entry point. One failed check fails the build (../../build.sh).
namespace PauseAnytime::Tests
{
    inline int Checks = 0;
    inline int Failures = 0;

    inline void Check(bool ok, char const* what, std::source_location where = std::source_location::current())
    {
        Checks++;
        if (ok) return;
        Failures++;
        std::string_view file{ where.file_name() };
        if (auto const slash = file.find_last_of("\\/"); slash != std::string_view::npos) file.remove_prefix(slash + 1);
        std::printf("FAILED %.*s line %u: %s\n", static_cast<int>(file.size()), file.data(), static_cast<unsigned>(where.line()), what);
    }

    /// KeyRulesTest.cpp, MovieRulesTest.cpp and OverlayRulesTest.cpp.
    void RunKeyRules();
    void RunMovieRules();
    void RunOverlayRules();
}
