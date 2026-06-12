#ifndef TEST_RESULT_H
#define TEST_RESULT_H

#include <string>

struct TestResult {
    bool success = false;
    long latencyMs = -1;
    std::string errorMsg;
    std::string indexId;
    std::string address;
    int port = 0;
    int delay = -1;
};

#endif // TEST_RESULT_H
