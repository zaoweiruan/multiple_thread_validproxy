#pragma once

#include <string>
#include <vector>
#include <boost/json.hpp>

namespace config {

// Same layout as Conflict, but in a different header
struct TestConflict2 {
    int severity;
    int type;
    std::string component1;
    std::string component2;
    std::string message;
    int index1;
    int index2;
    std::string sourceBundle1;
    std::string sourceBundle2;
    bool isCrossBundle;
    
    TestConflict2() = default;
    TestConflict2(const TestConflict2& other) = default;
    TestConflict2& operator=(const TestConflict2& other) = default;
    TestConflict2(TestConflict2&& other) = default;
    TestConflict2& operator=(TestConflict2&& other) = default;
};

} // namespace config
