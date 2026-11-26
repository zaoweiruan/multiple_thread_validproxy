// pthread_win32_shim.cpp — minimal pthread compat shim for the msvcrt-win32 toolchain.
//
// Prebuilt vcpkg `x64-mingw-static` archives (Boost.JSON, Catch2) were built by
// a posix-threads MinGW and reference pthread_mutex_* / pthread_once. The
// default toolchain uses `--enable-threads=win32`, so libwinpthread is absent.
//
// Layout contract (from this toolchain's <pthread.h>):
//   typedef intptr_t pthread_mutex_t;   // an opaque HANDLE, NOT inline storage
//   typedef long pthread_once_t;         // 0 == PTHREAD_ONCE_INIT
// So pthread_mutex_init must ALLOCATE a Win32 CRITICAL_SECTION and store its
// pointer in the intptr_t slot. Writing a CRITICAL_SECTION inline (40 bytes)
// into that 8-byte slot corrupts adjacent memory (0xC0000374 at Catch2 startup).
//
// Only compiled for the msvcrt-win32 toolchain. Other toolchains link the real
// winpthread and never see these symbols.
#include <windows.h>

extern "C" {

int pthread_mutex_init(void* mutex, const void* attr) {
    (void)attr;
    CRITICAL_SECTION* cs = new CRITICAL_SECTION;
    InitializeCriticalSection(cs);
    *reinterpret_cast<intptr_t*>(mutex) = reinterpret_cast<intptr_t>(cs);
    return 0;
}

int pthread_mutex_destroy(void* mutex) {
    CRITICAL_SECTION* cs = reinterpret_cast<CRITICAL_SECTION*>(
        *reinterpret_cast<intptr_t*>(mutex));
    if (cs != nullptr) {
        DeleteCriticalSection(cs);
        delete cs;
        *reinterpret_cast<intptr_t*>(mutex) = 0;
    }
    return 0;
}

int pthread_mutex_lock(void* mutex) {
    EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(
        *reinterpret_cast<intptr_t*>(mutex)));
    return 0;
}

int pthread_mutex_unlock(void* mutex) {
    LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(
        *reinterpret_cast<intptr_t*>(mutex)));
    return 0;
}

int pthread_once(void* once, void (*routine)()) {
    // pthread_once_t == long, PTHREAD_ONCE_INIT == 0.
    volatile long* flag = reinterpret_cast<volatile long*>(once);
    if (InterlockedCompareExchange(flag, 1L, 0L) == 0L) {
        routine();
    }
    return 0;
}

} // extern "C"
