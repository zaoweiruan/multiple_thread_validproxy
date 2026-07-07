#ifndef UI_SCOPE_GUARD_H
#define UI_SCOPE_GUARD_H

namespace ui {

/**
 * @brief Generic RAII guard that resets a value to false on scope exit.
 *
 * Replaces the recurring pattern:
 *   struct ResetGuard { std::atomic<bool>& flag; ~ResetGuard() { flag = false; } };
 *   ResetGuard _rg{someAtomic_};
 *
 * Usage:
 *   ScopeGuard<std::atomic<bool>> _sg{flag};
 */
template <typename T>
class ScopeGuard {
public:
    explicit ScopeGuard(T& ref) : ref_(ref) {}

    ~ScopeGuard() { ref_ = false; }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

private:
    T& ref_;
};

} // namespace ui

#endif // UI_SCOPE_GUARD_H
