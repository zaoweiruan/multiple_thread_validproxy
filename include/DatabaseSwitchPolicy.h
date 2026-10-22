#ifndef APP_DATABASE_SWITCH_POLICY_H
#define APP_DATABASE_SWITCH_POLICY_H

// ---------------------------------------------------------------
// Database switch policy (pure decision logic)
//
// Switching the live database replaces the sqlite3* handle that both
// the periodic silent probe and the user operations use. The probe
// runs on its own thread every proxy_process_monitor.check_interval_ms
// (production: 5000 ms) and holds the handle for the whole round, so
// with a probe interval equal to the probe duration the "probe is
// running" flag is almost permanently true.
//
// The old guard refused the switch whenever the probe was in flight,
// which made switching the database practically impossible. The probe
// is a BACKGROUND task that can be stopped, so the correct reaction is
// to stop it and then switch - not to refuse.
//
// This header holds only the decision, with no I/O and no dependency
// on AppController (which is not part of any unit-test target).
// ---------------------------------------------------------------

namespace db_switch_policy {

// Decision taken when the user asks to switch the live database.
enum class Decision {
    // Nothing is running: switch immediately.
    Allow = 0,
    // Only the periodic silent probe is running: stop it first, then switch.
    QuiesceProbe,
    // A user-initiated operation is running: refuse the switch.
    RefuseUserOperation
};

// Decides what to do with a database switch request.
// A user-initiated operation always wins: it may be mutating the old
// handle, and it cannot be cancelled safely from the UI thread.
inline Decision decide(bool userOperationRunning, bool onlineProbeRunning) {
    if (userOperationRunning) {
        return Decision::RefuseUserOperation;
    }
    if (onlineProbeRunning) {
        return Decision::QuiesceProbe;
    }
    return Decision::Allow;
}

// Short, stable label used in log messages.
inline const char* decisionLabel(Decision decision) {
    switch (decision) {
        case Decision::Allow:
            return "allow";
        case Decision::QuiesceProbe:
            return "quiesce-probe";
        case Decision::RefuseUserOperation:
            return "refuse-user-operation";
    }
    return "unknown";
}

} // namespace db_switch_policy

#endif // APP_DATABASE_SWITCH_POLICY_H
