# Code Review Report: TestPanel Log Display Plan

**Run ID**: `20260519-testpanel-log`  
**Date**: 2026-05-19  
**Review scope**: `docs/plans/2026-05-19-testpanel-log-display.md`  
**Intent**: Migrate test-related log output from Logger callbacks into TestPanel for unified UI experience.

---

## Summary

**Overall Assessment**: ⚠️ **Needs Revision**

The plan has a sound conceptual foundation but contains critical technical issues that must be addressed before implementation. The core Logger callback mechanism lacks callback stacking/restoration, and the TestPanel log display component has threading and line-limit logic bugs.

---

## P1 - Must Fix

| # | File/Section | Issue | Recommendation |
|---|--------------|-------|----------------|
| 1 | Logger.cpp (line 106-109) | `setLogCallback()` overwrites without preserving previous callback; ScopedLogCallback cannot "restore" previous state | Add `Logger::pushCallback()`/`popCallback()` stack or have TestPanel save/restore callback reference |
| 2 | Plan line 119-130 | Line limit `Clear()` removes ALL text, not just old lines; `SetSelection` before `Clear()` has no effect | Use `logCtrl_->Remove(start, logCtrl_->GetLastPosition())` or extract-retval pattern to keep last N lines |
| 3 | Plan line 108 | `wxMutexGuiLocker` is insufficient for cross-thread UI updates; direct callback from worker thread unsafe | Use `wxQueueEvent` pattern (like LogPanel does) or `CallAfter()` for thread marshaling |

---

## P2 - Should Fix

| # | File/Section | Issue | Recommendation |
|---|--------------|-------|----------------|
| 4 | Plan line 82-83 | LogPanel callback ownership conflict not specified; if TestPanel takes over, LogPanel misses logs during testing | Clarify: either keep both via chained callbacks, or document that LogPanel is "testing-session inactive" |
| 5 | Plan line 75 | ScopedLogCallback nested class design incomplete; no specification for how it handles Logger callback ownership | Define `struct ScopedLogCallback { ScopedLogCallback(TestPanel*); ~ScopedLogCallback(); };` with clear save/restore semantics |
| 6 | MainFrame.cpp (line 225-245) | `initPanels()` already does NOT create `logPanel_`; plan says to "remove event binding" but nothing exists to remove | Clarify: plan should state "do not instantiate LogPanel" rather than "remove from existing layout" |
| 7 | AppController.cpp | `doTestSubscription` and `doTestSingleProxy` need callback setup BEFORE thread spawn, not inside | Pass TestPanel reference or use event-based callback switching at thread start |

---

## P3 - Consider Addressing

| # | File/Section | Issue |
|---|--------------|-------|
| 8 | Plan line 102 | Color specification contradicts line 56-57 teletype font config; should be consistent |
| 9 | Plan line 32 | Success criteria: "500+ lines" test case not specified how to verify scroll performance |
| 10 | TestPanel.h | Missing `#include <wx/textctrl.h>` needed for `wxTextCtrl*` member |

---

## Code Quality Observations

### Threading Safety
- LogPanel correctly uses `wxQueueEvent` for cross-thread callback (LogPanel.cpp:76-81)
- TestPanel plan incorrectly suggests `wxMutexGuiLocker` which does NOT marshal to GUI thread

### Memory/Performance
- Line limit logic bug would cause memory growth unbounded
- Consider using `wxTextCtrl::EmulateEmulationMode(wxTextCtrlEmu_EndOfLine)` for large text performance

### API Design
- Logger callback system needs enhancement for multiple subscribers or save/restore pattern
- Current design supports only single callback consumer

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Callback race condition | High | Crash/incorrect behavior | Use event queue, not mutex |
| TestPanel log memory leak | Medium | Performance degradation | Fix line limit logic |
| LogPanel loses logs during testing | Medium | User confusion | Document expected behavior or use chained callbacks |

---

## Recommendations

### Immediate Actions
1. Revise Logger to support callback save/restore OR chained callbacks
2. Fix line limit logic in TestPanel::appendLogLine()
3. Use `wxQueueEvent` pattern, not mutex, for cross-thread UI updates

### Future Enhancements
1. Consider adding `LogLevel::TEST` for filtering test logs specifically
2. Add virtual mode option for wxTextCtrl with >1000 lines

---

## Verdict

**Mergability**: ❌ **Not Ready**

- P1 issues (#1, #2, #3) are blocking for safe implementation
- P2 issues (#4, #5, #6) require clarification before proceeding
- Recommend revising plan to address callback ownership and threading model

---

## Suggested Revised Plan Outline

```
## Revised Implementation Strategy

### Logger Enhancement (Pre-S1)
- Add `static void pushCallback(LogCallback)` and `static void popCallback()`
- Or: Have TestPanel store previous callback in member, restore on destruction

### S1-S2: TestPanel
- Add logCtrl_ with wxTE_MULTILINE | wxTE_READONLY
- Use custom event (TestLogEvent) + wxQueueEvent for thread safety
- Fix line limit: use Remove() for old lines, not Clear()

### S3: AppController
- Pass TestPanel* to test methods OR use event-based callback switch
- Set callback BEFORE worker thread starts

### S4-S5: LogPanel & MainFrame
- LogPanel: add flag to disable callback during testing OR use chained callbacks
- MainFrame: No change needed if LogPanel not in layout
```