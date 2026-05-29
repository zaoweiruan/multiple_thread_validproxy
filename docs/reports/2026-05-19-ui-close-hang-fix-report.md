# 2026-05-19 UI Close Hang Fix Report

## Issue
Application failed to close properly - window remained open or hung during shutdown.

## Root Causes

### 1. AppController Destructor Race Condition
**File**: `src/ui/AppController.cpp:27-33`

**Problem**: `XrayManager::release()` was called before worker thread was joined, causing the worker thread to potentially access a deleted singleton.

**Original Code**:
```cpp
AppController::~AppController() {
    cancelRequested_ = true;
    XrayManager::release();  // singleton deleted here
    if (workerThread_.joinable()) {
        workerThread_.detach();  // thread may still access deleted singleton
    }
}
```

**Fix**: Wait for thread to complete before releasing singleton.
```cpp
AppController::~AppController() {
    cancelRequested_ = true;
    if (workerThread_.joinable()) {
        workerThread_.join();  // wait for thread to finish
    }
    XrayManager::release();  // safe to delete now
}
```

### 2. XrayInstance Process Handle Bug
**File**: `src/XrayInstance.cpp:57-61`

**Problem**: `pi.hProcess` was closed via `CloseHandle()` but then assigned to member variable, resulting in invalid handle storage.

**Original Code**:
```cpp
ResumeThread(pi.hThread);
CloseHandle(pi.hProcess);  // closed here
CloseHandle(pi.hThread);
processHandle_ = pi.hProcess;  // storing closed handle
```

**Fix**: Save handle before closing thread handle.
```cpp
ResumeThread(pi.hThread);
processHandle_ = pi.hProcess;  // save first
CloseHandle(pi.hThread);
```

### 3. XrayInstance Stop Missing Wait
**File**: `src/XrayInstance.cpp:68-75`

**Problem**: `TerminateJobObject` was called but no wait for process termination, causing potential hang on shutdown.

**Fix**: Added `WaitForSingleObject` to ensure clean process termination.
```cpp
void XrayInstance::stop() {
    if (jobObject_) {
        TerminateJobObject(jobObject_, 0);
        CloseHandle(jobObject_);
        jobObject_ = nullptr;
    }
    if (processHandle_) {
        WaitForSingleObject(processHandle_, 3000);  // wait for termination
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    }
    running_ = false;
}
```

### 4. MainFrame Close Handler
**File**: `src/ui/MainFrame.cpp:232-243`

**Problem**: Controller was not deleted on close, delaying destruction until frame destructor.

**Fix**: Delete controller during close event.
```cpp
void MainFrame::onClose(wxCloseEvent& event) {
    if (controller_) {
        controller_->cancelTest();
        delete controller_;
        controller_ = nullptr;
    }
    event.Skip();
}
```

## Files Modified
- `src/ui/AppController.cpp` - Destructor fix
- `src/XrayInstance.cpp` - Process handle and stop fix
- `src/ui/MainFrame.cpp` - Close handler fix

## Verification
- Build: SUCCESS (Debug mode)
- Tests: 3/3 PASSED
- Manual test: Application closes cleanly