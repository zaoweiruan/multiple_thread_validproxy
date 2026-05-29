# Plan: UI LogWindow Replacement (TestPanel removal) — 2026-05-19

Environment
- Current time: 2026-05-19T17:38:55+08:00
- Working directory: E:\eclipse_workspace\multiple_thread_validproxy
- Workspace root folder: E:\eclipse_workspace\multiple_thread_validproxy

Goal
- Remove TestPanel usage from the UI and replace the in-panel test logs with a centralized LogWindow.
- The LogWindow should display logs in a white background with black text using a monospace font, and be located in the main UI (preferably at the bottom of the MainFrame).
- Preserve the existing Logger mechanism; route logs to the LogWindow without regression to non-test logs.
- Minimize code churn and avoid destabilizing other UI components.

Scope
- In scope:
  - Implement new LogWindow component (LogWindow.h, LogWindow.cpp).
  - Integrate LogWindow into MainFrame layout (MainFrame.h/.cpp).
  - Remove tests UI entrypoint: TestPanel (TestPanel.h/.cpp) and all references in layout and build system.
  - Update CMakeLists.txt to exclude TestPanel and include LogWindow.
  - Maintain a simple, text-only log stream in the LogWindow (no color-coding).
- Out of scope:
  - Any external UI refactor beyond replacing TestPanel with LogWindow.
  - Changes to core test logic or test executors beyond routing logs to LogWindow.

Assumptions & Constraints
- The project already contains a Logger with callback support; we will reuse it for LogWindow output.
- The UI uses wxWidgets; LogWindow will be implemented with a wxTextCtrl in read-only, multiline mode.
- Thread-safety: all UI updates must occur on the main thread;LogWindow will marshal via wxCallAfter or the existing Logger callback path to the main thread.
- If a dedicated test-event formatting is still desired, keep it lightweight and output as plain text lines in the LogWindow.

Architecture Overview
- Components
  - LogWindow (new): A dedicated log display area for all runtime messages (including test logs).
  - MainFrame: Will host the LogWindow at a stable location (bottom dock or bottom panel).
  - Logger: Continues to emit log lines; LogWindow subscribes via a callback to display lines.
  - TestPanel (deprecated): To be removed; any code currently referencing TestPanel will be redirected to use LogWindow.
- Data Flow
  - Logger emits messages → LogWindow receives via registered callback → UI updates on main thread.
- UI/UX
  - Appearance: white background, black monospace text, no color-coding. Simple, readable.
  - Features: append-only log; optional clear functionality exposed via a method or a small toolbar if desired later.

Plan & Execution Waves
- Wave 1: Scaffolding and removal planning
  - Create LogWindow.h/cpp with a minimal API: AppendLogLine(const std::string& line), Clear(), AttachToLogger() (or register via Logger callback externally).
  - Prepare MainFrame to host a LogWindow member; expose a simple AddLogLine route.
  - Remove TestPanel usage from layout and build (but keep files in repo temporarily for rollback).
  - Update CMakeLists to remove TestPanel sources and add LogWindow sources.
- Wave 2: Implement LogWindow and UI wiring
  - Implement LogWindow with a wxTextCtrl (wxTE_MULTILINE | wxTE_READONLY) and monospace font.
  - Wire Logger to log to LogWindow using a callback; marshal updates to the main thread.
  - Integrate LogWindow into MainFrame layout; ensure proper docking and resizing behavior.
- Wave 3: Cleanup and removal of TestPanel
  - Remove TestPanel.h/.cpp from repository; prune any references in UI code.
  - Remove TestPanel from build configuration; ensure no build-time references remain.
- Wave 4: Build, test, and validate
  - Build the project; run the application to verify the LogWindow shows logs in real-time.
  - Validate that non-test logs also appear in LogWindow without regressions.
- Wave 5: Documentation & handover
  - Update documentation: add an entry to docs/plans with the new design and steps; record the changes in docs/build-env-YYYY-MM-DD-*.md after the build.

Task Breakdown (files touched)
- New: src/ui/LogWindow.h
- New: src/ui/LogWindow.cpp
- Modified: src/ui/MainFrame.h
- Modified: src/ui/MainFrame.cpp
- Modified: CMakeLists.txt (UI sources and TestPanel removal; add LogWindow)
- Removed: src/ui/TestPanel.h
- Removed: src/ui/TestPanel.cpp
- Optional: Update references in other modules to stop using TestPanel

Validation & Acceptance Criteria
- Build succeeds with UI changes (LogWindow integrated; TestPanel removed).
- The LogWindow renders logs as white background with black monospace text, without color-coding, in a stable area of the MainFrame.
- All logs flow through the Logger and are visible in the LogWindow; no crashes or regressions in other UI components.
- Documentation updated to reflect the UI change and build steps.

Risks & Mitigations
- Risk: Layout disruption when replacing TestPanel with LogWindow.
  - Mitigation: Implement LogWindow as a drop-in replacement at the same height, using Dock or sizer-based layout, with fallback if space is constrained.
- Risk: Thread-safety issues updating UI from Logger callbacks.
  - Mitigation: Use wxCallAfter or the existing main-thread marshaling strategy in LogWindow implementation.
- Risk: Build breakages due to header changes.
  - Mitigation: Ensure incremental changes; keep a temporary alias or elderly TestPanel code path during transition for rollback.

Deliverables & Timeline
- Deliverables: LogWindow.{h,cpp}, updated MainFrame, updated CMakeLists, removal of TestPanel, documentation update.
- Timeline: 2–3 development iterations, followed by a verification run and documentation update.

Documentation reference
- The change plan should be recorded in docs/plans/2026-05-19-ui-logwindow-design.md (or equivalent) with a summary of decisions, API, and wiring plan.
