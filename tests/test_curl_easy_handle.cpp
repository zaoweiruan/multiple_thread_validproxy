#include "CurlEasyHandle.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "Testing CurlEasyHandle basic functionality..." << std::endl;

    try {
        CurlEasyHandle curl;
        curl.setUrl("https://www.example.com")
            .setNoBody(true)
            .setTimeoutMs(5000)
            .perform();

        long code = curl.getResponseCode();
        std::cout << "  Response code: " << code << std::endl;
        assert(code == 200 || code == 204);

        std::cout << "  Total time: " << curl.getTotalTime() << "s" << std::endl;
        std::cout << "  INFO: CurlEasyHandle test passed" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "  ERROR: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
