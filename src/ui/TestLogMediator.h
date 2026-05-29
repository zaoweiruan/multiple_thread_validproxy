// File: src/ui/TestLogMediator.h
#ifndef TEST_LOG_MEDIATOR_H
#define TEST_LOG_MEDIATOR_H

#include <string>
#include <functional>
#include <mutex>

struct TestEvent {
  int test_id;
  int stage;
  std::string message;
  int delay_ms = 0;
  int severity;

  static constexpr int STAGE_INIT = 0;
  static constexpr int STAGE_PROGRESS = 1;
  static constexpr int STAGE_DONE = 2;
  static constexpr int STAGE_ERROR = 3;

};

// Severity constants (global-ish) to avoid macro collisions in headers
static constexpr int TEST_SEVERITY_INFO = 0;
static constexpr int TEST_SEVERITY_WARN = 1;
static constexpr int TEST_SEVERITY_ERROR = 2;



class TestLogMediator {
public:
    static TestLogMediator& instance();

    void postEvent(const TestEvent& ev);
    void setCallback(std::function<void(const TestEvent&)> cb);

private:
    TestLogMediator() = default;
    TestLogMediator(const TestLogMediator&) = delete;
    TestLogMediator& operator=(const TestLogMediator&) = delete;

    std::function<void(const TestEvent&)> m_callback;
    std::mutex m_mutex;
};

#endif // TEST_LOG_MEDIATOR_H
