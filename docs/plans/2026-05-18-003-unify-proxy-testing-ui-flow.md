---
title: "plan(UI): unify proxy testing UI flow for context-menu and toolbar paths"
type: plan
status: draft
date: 2026-05-18
origin: "eager-moon.md (original .kilo plan)"
---

# 1779072849830-eager-moon

## Plan Goal
Unify and complete the UI flow for proxy testing across both context-menu and toolbar paths, ensuring: 
- per-proxy intermediate progress rows appear in the TestPanel
- the delay column in the ProxyListPanel refreshes on test completion
- completion events propagate to MainFrame to trigger refresh flow, without breaking existing behavior
- thread-safe UI updates from background test workers

## Context
- The codebase implements a two-pane UI: SubscriptionPanel + ProxyListPanel on the left and TestPanel on the bottom, with a central ProxyListPanel showing proxies and a per-proxy test flow.
- Current issues observed: (a) constructing TestPanel before ProxyListPanel fixes null pointer scenarios; (b) completion events posted to TestPanel do not reliably trigger MainFrame refresh, causing delay values not to update and test rows not being populated as expected.

## Assumptions
- wxWidgets version is used (3.x) with standard event loops; UI updates must occur on the main thread.
- doTestSubscription() runs in a worker thread and posts events back to the main thread via wxQueueEvent.
- The existing events include: StatusUpdateEvent, SubscriptionTestEvent, and ProxyTestProgressEvent (with an isCompleted flag).
- The plan aims to keep existing toolbar path behavior intact while ensuring context-menu path updates mirror the toolbar path.

## Proposed Approach
1) Introduce robust event routing for proxy test completion:
   - When a test is started from a context menu (ProxyListPanel -> testPanel_), completion should publish a completion ProxyTestProgressEvent to both TestPanel and the MainFrame top window in a thread-safe manner.
   - Ensure there is a clearly defined isCompleted flag on ProxyTestProgressEvent that TestPanel can use to finalize per-proxy rows and trigger any required finalization steps, and that MainFrame can ultra-safely refresh the ProxyListPanel delay column.
2) Expand TestPanel behavior:
   - On receiving intermediate events (isCompleted=false), populate a new row with columns: IndexId, Type/Address, Delay, Status/Message as available.
   - On receiving completion events (isCompleted=true), ensure TestPanel finalizes its own row state (e.g., disable Cancel button, mark test as complete) and trigger a separate path to refresh MainFrame UI if needed.
3) Expand ProxyListPanel behavior:
   - Listen for completion events and call refreshResults() to pull the latest delays from the database and update the Delay column for all visible proxies.
4) Maintain backward compatibility:
   - Toolbar path must remain unaffected; verify that MainFrame path still pushes events to the event loop as before.
5) Validation plan
   - Manual end-to-end test: trigger test via context menu and via toolbar; verify TestPanel rows populate with per-proxy data; verify ProxyListPanel Delay column updates after completion; verify no regressions in other UI parts.
   - Automated tests: add integration tests to simulate event sequence and verify end-state in both TestPanel and ProxyListPanel.

## Milestones & Tasks
- [ ] Refactor AppController.doTestSubscription to broadcast completion to both TestPanel and MainFrame in the context-menu path (safely, on the main thread).
- [ ] Extend ProxyListPanel to refresh Delay column on completion events.
- [ ] Extend TestPanel to create per-proxy rows on intermediate events and finalize state on completion.
- [ ] Ensure thread-safety: all UI updates occur on main thread; test worker threads post events via wxQueueEvent.
- [ ] Validate with manual end-to-end tests for both paths; ensure no regressions on toolbar path.
- [ ] Update docs with a brief description of the new event routing and the UI behavior.

## Acceptance Criteria
- Context-menu proxy tests produce per-proxy rows in TestPanel with correct proxy IDs, remarks, and delays populated after completion.
- ProxyListPanel delay column refreshes on test completion, reflecting latest measured delays.
- Toolbar tests continue to work as before.
- No crashes or data races when tests run concurrently or sequentially.

## Risks & Mitigations
- Risk: cross-thread UI updates cause race conditions.
  Mitigation: use wxQueueEvent (main thread) for all UI updates; ensure listener registration guards are in place.
- Risk: duplication of completion events across two receivers.
  Mitigation: ensure idempotent completion logic; guard by isCompleted flag and idempotent refresh semantics.

## Success Metrics
- All test paths (context-menu and toolbar) result in visible test rows with meaningful data and refreshed delays.
- No regressions observed in UI performance or stability.

## Open Questions
- Do we want a dedicated event type for test completion to avoid overloading ProxyTestProgressEvent with isCompleted flag?
- Should we introduce a lightweight feature flag for enabling/rolling back this UI flow if issues arise in CI?

Plan ready for implementation after user confirmation.
