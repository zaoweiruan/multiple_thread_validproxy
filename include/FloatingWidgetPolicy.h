#ifndef FLOATING_WIDGET_POLICY_H
#define FLOATING_WIDGET_POLICY_H

#include <string>
#include <cstdint>
#include <cstdio>

namespace FloatingWidgetPolicy {

// Clamp bounds mirror ProxyProcessMonitorConfigParser, which clamps
// check_interval_ms to [5000, 300000]. Keep these in sync.
struct Defaults {
    static const int kMinIntervalMs = 5000;
    static const int kMaxIntervalMs = 300000;
};

// Clamp a refresh interval (ms) into the valid [5000, 300000] range.
inline int clampIntervalMs(int v) {
    if (v < Defaults::kMinIntervalMs) return Defaults::kMinIntervalMs;
    if (v > Defaults::kMaxIntervalMs) return Defaults::kMaxIntervalMs;
    return v;
}

// The collapsed (pill) widget is shown only when both the feature is enabled
// in config and the user has activated monitoring.
inline bool shouldShowCollapsed(bool configEnabled, bool userActive) {
    return configEnabled && userActive;
}

// The floating widget expands its detail list only while the collapsed pill is
// visible and the pointer is hovering over it.
inline bool shouldExpand(bool collapsedVisible, bool hovering) {
    return collapsedVisible && hovering;
}

// Format a duration in milliseconds as "mm:ss" when below one hour, otherwise
// "h:mm:ss".
inline std::string formatDuration(int64_t durationMs) {
    if (durationMs < 0) durationMs = 0;
    if (durationMs < 3600000) {
        const int64_t totalSeconds = durationMs / 1000;
        const int64_t minutes = totalSeconds / 60;
        const int64_t seconds = totalSeconds % 60;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld",
                      static_cast<long long>(minutes),
                      static_cast<long long>(seconds));
        return std::string(buf);
    }
    const int64_t totalSeconds = durationMs / 1000;
    const int64_t hours = totalSeconds / 3600;
    const int64_t rem = totalSeconds % 3600;
    const int64_t minutes = rem / 60;
    const int64_t seconds = rem % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld",
                  static_cast<long long>(hours),
                  static_cast<long long>(minutes),
                  static_cast<long long>(seconds));
    return std::string(buf);
}

// ---------------------------------------------------------------
// Circular floating-widget policy (v1.2) — pure, testable.
// Controls geometry, screen docking, and the hide-delay slider
// mapping.  All functions are side-effect free so they can be
// unit-tested without any wx dependency.
// ---------------------------------------------------------------
struct CircleDefaults {
    static const int kMinRadius = 24;
    static const int kMaxRadius = 80;
    // 默认半径由 36 缩小 30%（直径 72 -> ~50px），仅在悬浮球内居中显示监控进程数量。
    static constexpr int kDefaultRadius = 25;
};

// Clamp an orb radius (px, DIP) into the valid [24, 80] range.
inline int clampRadius(int r) {
    if (r < CircleDefaults::kMinRadius) return CircleDefaults::kMinRadius;
    if (r > CircleDefaults::kMaxRadius) return CircleDefaults::kMaxRadius;
    return r;
}

enum class DockEdge { Left, Right, Top, Bottom };

struct ScreenAnchor {
    int screenW = 0;
    int screenH = 0;
    int margin = 8;
};

// Clamp a window's top-left (x,y) so the whole window stays on-screen.
inline void clampToScreen(int& x, int& y, int w, int h, const ScreenAnchor& a) {
    if (a.screenW <= 0 || a.screenH <= 0) return;
    if (x < a.margin) x = a.margin;
    if (y < a.margin) y = a.margin;
    if (x + w > a.screenW - a.margin) x = a.screenW - a.margin - w;
    if (y + h > a.screenH - a.margin) y = a.screenH - a.margin - h;
    if (x < a.margin) x = a.margin;
    if (y < a.margin) y = a.margin;
}

// Pick the screen edge nearest to a window center point (cx, cy).
inline DockEdge nearestEdge(int cx, int cy, int screenW, int screenH) {
    int minD = cx;
    DockEdge edge = DockEdge::Left;
    if ((screenW - cx) < minD) { minD = screenW - cx; edge = DockEdge::Right; }
    if (cy < minD) { minD = cy; edge = DockEdge::Top; }
    if ((screenH - cy) < minD) { edge = DockEdge::Bottom; }
    return edge;
}

// Compute the docked top-left for a given edge (window centered on that edge).
inline void dockPosition(DockEdge edge, int w, int h, const ScreenAnchor& a,
                         int& x, int& y) {
    switch (edge) {
        case DockEdge::Left:
            x = a.margin;
            y = (a.screenH - h) / 2;
            break;
        case DockEdge::Right:
            x = a.screenW - w - a.margin;
            y = (a.screenH - h) / 2;
            break;
        case DockEdge::Top:
            x = (a.screenW - w) / 2;
            y = a.margin;
            break;
        case DockEdge::Bottom:
            x = (a.screenW - w) / 2;
            y = a.screenH - h - a.margin;
            break;
    }
}

struct HideDelayDefaults {
    static const int kMinMs = 300;
    static const int kMaxMs = 4000;
    static const int kDefaultMs = 1500;
};

inline int clampHideDelayMs(int v) {
    if (v < HideDelayDefaults::kMinMs) return HideDelayDefaults::kMinMs;
    if (v > HideDelayDefaults::kMaxMs) return HideDelayDefaults::kMaxMs;
    return v;
}

// Map a slider position [0..1000] -> hide delay ms (linear).
inline int sliderToHideDelay(int pos) {
    if (pos < 0) pos = 0;
    if (pos > 1000) pos = 1000;
    const int span = HideDelayDefaults::kMaxMs - HideDelayDefaults::kMinMs;
    return HideDelayDefaults::kMinMs + (span * pos) / 1000;
}

// Inverse of sliderToHideDelay.
inline int hideDelayToSlider(int ms) {
    ms = clampHideDelayMs(ms);
    const int span = HideDelayDefaults::kMaxMs - HideDelayDefaults::kMinMs;
    return ((ms - HideDelayDefaults::kMinMs) * 1000) / span;
}

struct HoverExpandDefaults {
    static const int kMinMs = 200;
    static const int kMaxMs = 2000;
    static const int kDefaultMs = 500;
};

// Clamp the hover-dwell delay (ms) before the orb expands into the panel.
inline int clampHoverExpandDelayMs(int v) {
    if (v < HoverExpandDefaults::kMinMs) return HoverExpandDefaults::kMinMs;
    if (v > HoverExpandDefaults::kMaxMs) return HoverExpandDefaults::kMaxMs;
    return v;
}

// Pixels of movement below which a press-release is a click, not a drag.
inline int dragThresholdPx() { return 4; }

} // namespace FloatingWidgetPolicy

#endif // FLOATING_WIDGET_POLICY_H
