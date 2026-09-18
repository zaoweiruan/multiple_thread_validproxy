// tests/ui/TestFrameworkUnit.cpp — pure-logic unit tests for the UI test
// framework itself (no GUI required). Run manually:
//   .\tests\UITests.exe "[fw-unit]"
#include <catch2/catch_test_macros.hpp>
#include "framework/Wait.h"

TEST_CASE("waitFor returns true immediately", "[fw-unit]") {
    int calls = 0;
    REQUIRE(uitest::waitFor(1000, 10, [&calls] { ++calls; return true; }));
    REQUIRE(calls == 1);
}

TEST_CASE("waitFor times out with bounded polls", "[fw-unit]") {
    int calls = 0;
    REQUIRE_FALSE(uitest::waitFor(120, 40, [&calls] { ++calls; return false; }));
    REQUIRE(calls >= 3);
    REQUIRE(calls <= 6);
}
