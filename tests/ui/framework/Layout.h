// tests/ui/framework/Layout.h - screen-geometry sensing, layout validation and
// window resizing.
//
// Three capabilities live here, all built on UiElement::boundingRect():
//
//   1. RectReport / collectRects - snapshot every visible control's screen
//      rectangle so a test can assert on the page layout numerically instead
//      of eyeballing a PNG.
//   2. LayoutValidator - the actual rules a good layout must satisfy
//      (nothing out of bounds, nothing unexpectedly overlapping, expected
//      controls actually present and on screen).
//   3. resizeWindow / restoreWindow - drive the page layout automatically by
//      growing or shrinking the frame and re-measuring, which is how you test
//      responsive behaviour and how you recover a window that opens too small
//      to expose the control under test.
//
// All rectangles are SCREEN pixels, matching UIA_BoundingRectanglePropertyId.
// When comparing against a parent rect, convert with clientToScreen first - see
// RECT helpers below.

#pragma once
#include <windows.h>
#include <sstream>
#include <string>
#include <vector>

#include "framework/UIElement.h"
#include "framework/UIAutomation.h"
#include "framework/WindowInfo.h"

namespace uitest {

// ------------------------------------------------------------- RECT helpers

inline int rectWidth(const RECT& r) { return r.right - r.left; }
inline int rectHeight(const RECT& r) { return r.bottom - r.top; }
inline long rectArea(const RECT& r) {
    const long w = rectWidth(r);
    const long h = rectHeight(r);
    return w > 0 && h > 0 ? w * h : 0;
}
inline POINT rectCenter(const RECT& r) {
    POINT p;
    p.x = r.left + rectWidth(r) / 2;
    p.y = r.top + rectHeight(r) / 2;
    return p;
}
inline bool rectIsEmpty(const RECT& r) { return rectWidth(r) <= 0 || rectHeight(r) <= 0; }

// Intersection area; 0 when the rects do not overlap. Used by the overlap rule
// so that "touching" edges do not count as an overlap.
inline long rectOverlapArea(const RECT& a, const RECT& b) {
    const int l = (a.left > b.left) ? a.left : b.left;
    const int t = (a.top > b.top) ? a.top : b.top;
    const int r = (a.right < b.right) ? a.right : b.right;
    const int bt = (a.bottom < b.bottom) ? a.bottom : b.bottom;
    if (r <= l || bt <= t) return 0;
    return static_cast<long>(r - l) * static_cast<long>(bt - t);
}

// Does `inner` sit completely inside `outer`? Tolerance absorbs the 1-2px
// rounding UIA applies to fractional DPI layouts.
inline bool rectInside(const RECT& inner, const RECT& outer, int tolerance = 2) {
    return inner.left >= outer.left - tolerance &&
           inner.top >= outer.top - tolerance &&
           inner.right <= outer.right + tolerance &&
           inner.bottom <= outer.bottom + tolerance;
}

// How far `inner` pokes outside `outer`, per edge. Zero means contained.
struct RectOverflow {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool any() const { return left || top || right || bottom; }
    std::wstring describe() const {
        std::wostringstream o;
        o << L"left=" << left << L" top=" << top
          << L" right=" << right << L" bottom=" << bottom;
        return o.str();
    }
};

inline RectOverflow rectOverflow(const RECT& inner, const RECT& outer, int tolerance = 2) {
    RectOverflow o;
    if (inner.left < outer.left - tolerance) o.left = outer.left - inner.left;
    if (inner.top < outer.top - tolerance) o.top = outer.top - inner.top;
    if (inner.right > outer.right + tolerance) o.right = inner.right - outer.right;
    if (inner.bottom > outer.bottom + tolerance) o.bottom = inner.bottom - outer.bottom;
    return o;
}

// --------------------------------------------------------- Rect collection

// One measured control.
struct RectEntry {
    std::wstring name;
    std::wstring className;
    int controlType = 0;
    RECT rect = {0, 0, 0, 0};
    bool enabled = true;
    bool offscreen = false;
    bool measured = false;   // false when boundingRect() failed (no geometry)
    std::wstring label() const {
        if (!measured) return name + L"<no-rect>";
        std::wostringstream o;
        o << name << L"[" << className << L"]@(" << rect.left << L"," << rect.top
          << L"," << rect.right << L"," << rect.bottom << L")";
        return o.str();
    }
};

// Snapshot of a window's layout.
struct RectReport {
    HWND hwnd = nullptr;
    RECT windowRect = {0, 0, 0, 0};   // GetWindowRect
    RECT clientRect = {0, 0, 0, 0};   // GetClientRect, still client coordinates
    std::vector<RectEntry> entries;
    // Traversal bookkeeping. A wxDataView exposes one UIA node per visible cell,
    // so an unbounded walk costs thousands of cross-process COM calls and looks
    // like a hang. `truncated` tells the caller the report is a shallow sample,
    // not a complete picture, so it must not assert "nothing else overlaps".
    bool truncated = false;
    int visited = 0;         // elements actually walked
    int maxDepthUsed = 0;    // deepest level reached
    int gridsPruned = 0;     // grid containers skipped wholesale
    int windowWidth() const { return rectWidth(windowRect); }
    int windowHeight() const { return rectHeight(windowRect); }
};

// wxDataView and its siblings expose one UIA node per rendered cell. Walking
// into them costs thousands of cross-process COM calls, and every one of those
// calls has to be serviced by the GUI's own main thread - which during startup
// is busy loading subscriptions. The result is minutes of blocking that read as
// a hung test rather than a slow one.
//
// So prune at the container: measure the grid itself (it still has geometry,
// which is what the layout rules care about) and never enumerate its cells.
inline bool isHeavyGridClass(const std::wstring& className) {
    if (className.empty()) return false;
    return className.find(L"wxDataView") != std::wstring::npos ||
           className.find(L"DataGrid") != std::wstring::npos ||
           className.find(L"wxGrid") != std::wstring::npos ||
           className.find(L"wxListCtrl") != std::wstring::npos ||
           className.find(L"wxListView") != std::wstring::npos ||
           className.find(L"SysListView32") != std::wstring::npos ||
           className.find(L"SysHeader32") != std::wstring::npos;
}

// Breadth-first walk of the control-view subtree, measuring every element that
// exposes geometry.
//
// Two deliberate choices, both learned the hard way:
//
//   1. BFS, not DFS. The main frame holds two wxDataViews whose cell subtrees are
//      thousands of nodes deep; a depth-first walk descends into the first cell
//      and never comes back out. BFS visits the shallow chrome - menu bar,
//      splitters, log buttons - before it ever touches the grid data, which is
//      exactly the set layout rules are written against.
//   2. A hard node budget. Every node costs ~5 cross-process COM property
//      calls, so an unbounded walk on a populated grid takes minutes and reads as
//      a hung test rather than a slow one. When the budget runs out the walk
//      stops and `rep.truncated` is set.
//
// Uses an explicit queue: a deep provider must not be able to blow the test
// process stack.
inline RectReport collectRects(const UiElement& root, int maxDepth = 6,
                               int maxNodes = 200) {
    RectReport rep;
    if (!root.valid()) return rep;

    HWND frame = nullptr;
    // MinGW declares UIA_HWND as void*, so the out-param is void**, not HWND**.
    UIA_HWND rawHwnd = nullptr;
    if (!root.raw() ||
        FAILED(root.raw()->get_CurrentNativeWindowHandle(&rawHwnd)) ||
        !rawHwnd) {
        return rep;
    }
    frame = static_cast<HWND>(rawHwnd);
    rep.hwnd = frame;
    ::GetWindowRect(frame, &rep.windowRect);
    ::GetClientRect(frame, &rep.clientRect);

    IUIAutomation* ua = Uia::instance().com();
    IUIAutomationTreeWalker* walker = nullptr;
    if (!ua || FAILED(ua->get_ControlViewWalker(&walker)) || !walker) return rep;

    // FIFO queue of (element, depth). The root's raw pointer is owned by the
    // caller's ComPtr, so it is never released; children carry their own ref.
    std::vector<IUIAutomationElement*> queue;
    std::vector<int> queueDepths;
    // Every AddRef'd child pulled from the walker, released only once the walk
    // is finished. Releasing mid-walk looks tidier but is a use-after-free
    // waiting to happen: the UIA client cache recycles element proxies, so a
    // pointer already sitting in `queue` can be handed back by the very next
    // sibling/child call, and the first Release then frees it twice.
    std::vector<IUIAutomationElement*> owned;
    size_t head = 0;
    queue.push_back(root.raw());   // root's ref belongs to the caller's ComPtr
    queueDepths.push_back(0);

    while (head < queue.size()) {
        if (rep.visited >= maxNodes) {
            rep.truncated = true;
            break;
        }
        IUIAutomationElement* el = queue[head];
        const int depth = queueDepths[head];
        ++head;
        if (!el) continue;              // defensive: never dereference a hole
        ++rep.visited;
        if (depth > rep.maxDepthUsed) rep.maxDepthUsed = depth;

        RectEntry e;
        CONTROLTYPEID ct = UIA_ControlTypePropertyId;
        BOOL off = TRUE;
        BOOL en = TRUE;
        // MinGW hands back BSTR (wchar_t*) by out-param, never std::wstring*.
        BSTR rawName = nullptr;
        BSTR rawClass = nullptr;
        el->get_CurrentName(&rawName);
        el->get_CurrentClassName(&rawClass);
        el->get_CurrentControlType(&ct);
        el->get_CurrentIsOffscreen(&off);
        el->get_CurrentIsEnabled(&en);
        if (rawName) e.name = rawName;
        if (rawClass) e.className = rawClass;
        e.controlType = static_cast<int>(ct);
        e.offscreen = (off == TRUE);
        e.enabled = (en == TRUE);
        e.measured = boundingRectOf(el, &e.rect);

        // Only keep named, sized controls: anonymous layout spacers would
        // drown the report and every pair would "overlap".
        if (e.measured && !e.name.empty() && !rectIsEmpty(e.rect)) {
            rep.entries.push_back(e);
        }

        ::SysFreeString(rawName);
        ::SysFreeString(rawClass);

        // Grid containers are measured but never descended into - see
        // isHeavyGridClass(). Without this the walk spends its entire budget
        // inside the first data cell and the real chrome is never reached.
        const bool pruned = isHeavyGridClass(e.className);
        if (pruned) ++rep.gridsPruned;

        if (depth < maxDepth && !pruned) {
            IUIAutomationElement* child = nullptr;
            if (SUCCEEDED(walker->GetFirstChildElement(el, &child)) && child) {
                while (child) {
                    // Respect the budget before allocating more queue slots,
                    // otherwise one wide grid row can still queue thousands.
                    if (rep.visited + static_cast<int>(queue.size() - head) >= maxNodes) {
                        rep.truncated = true;
                        child->Release();
                        child = nullptr;
                        break;
                    }
                    // Record the reference in `owned` and never release it
                    // here - see the note on `owned` above.
                    queue.push_back(child);
                    queueDepths.push_back(depth + 1);
                    owned.push_back(child);
                    IUIAutomationElement* next = nullptr;
                    if (FAILED(walker->GetNextSiblingElement(child, &next)) || !next) {
                        child = nullptr;   // last sibling already recorded
                        break;
                    }
                    child = next;
                }
            }
        }
    }

    // Release only now that no pointer in `queue` is still in use.
    for (size_t i = 0; i < owned.size(); ++i) owned[i]->Release();
    walker->Release();
    return rep;
}

// Human readable dump for INFO logs and failure artifacts. Also answers the
// "analyse the page layout from a screenshot" need numerically, so a failing
// test prints geometry instead of requiring a human to open the PNG.
inline std::wstring rectReportText(const RectReport& rep, int maxRows = 80) {
    std::wostringstream o;
    o << L"window=" << (rep.hwnd ? L"valid" : L"none")
      << L" size=" << rep.windowWidth() << L"x" << rep.windowHeight()
      << L" measured=" << rep.entries.size()
      << L" visited=" << rep.visited
      << L" depth=" << rep.maxDepthUsed
      << L" gridsPruned=" << rep.gridsPruned;
    if (rep.truncated) {
        o << L" [TRUNCATED - shallow sample, not a complete picture]";
    }
    o << L"\n";
    int rows = 0;
    for (size_t i = 0; i < rep.entries.size() && rows < maxRows; ++i) {
        o << L"  " << rep.entries[i].label()
          << (rep.entries[i].enabled ? L"" : L" [disabled]")
          << (rep.entries[i].offscreen ? L" [offscreen]" : L"") << L"\n";
        ++rows;
    }
    if (static_cast<size_t>(rows) < rep.entries.size()) {
        o << L"  ... " << (rep.entries.size() - static_cast<size_t>(rows))
          << L" more\n";
    }
    return o.str();
}

// Find the measured entry whose name matches, case-insensitively.
inline const RectEntry* findRect(const RectReport& rep, const std::wstring& name) {
    for (size_t i = 0; i < rep.entries.size(); ++i) {
        if (rep.entries[i].name == name) return &rep.entries[i];
    }
    return nullptr;
}

// --------------------------------------------------------- Layout validation

// One failed layout rule. Tests turn these into CHECK messages, so the text
// must name the offending control and the numbers.
struct LayoutViolation {
    std::wstring rule;     // "out-of-bounds" / "overlap" / "missing" / "offscreen"
    std::wstring subject;  // control name
    std::wstring detail;   // numbers
    std::wstring line() const {
        std::wostringstream o;
        o << L"[" << rule << L"] " << subject << L": " << detail;
        return o.str();
    }
};

// Tunables. Defaults are deliberately loose: UIA reports fractional-DPI rects
// that round oddly, and a test that fails on a 1px wobble is a test nobody runs.
struct LayoutRules {
    int tolerance = 2;              // px slack for containment
    bool checkOverlap = true;
    bool checkOutOfBounds = true;
    long minOverlapArea = 40;       // px^2 before an overlap counts as a defect
    bool requireOnScreen = true;    // expected controls must not be offscreen
};

// The controls a test expects to be present and visible.
struct LayoutExpectation {
    std::wstring name;
    bool required = true;   // false = "if present, must be laid out sanely"
};

class LayoutValidator {
public:
    explicit LayoutValidator(const RectReport& rep, const LayoutRules& rules = LayoutRules())
        : rep_(rep), rules_(rules) {}

    // A control must exist, be measured, and sit inside the window rect.
    void expectInside(const std::wstring& name) {
        const RectEntry* e = findRect(rep_, name);
        if (!e || !e->measured) {
            add(L"missing", name, L"not present in the measured layout");
            return;
        }
        if (!rules_.checkOutOfBounds) return;
        const RectOverflow ov = rectOverflow(e->rect, rep_.windowRect, rules_.tolerance);
        if (ov.any()) {
            add(L"out-of-bounds", name,
                L"overflow " + ov.describe() + L" vs window " +
                    std::to_wstring(rep_.windowRect.left) + L"," +
                    std::to_wstring(rep_.windowRect.top) + L"," +
                    std::to_wstring(rep_.windowRect.right) + L"," +
                    std::to_wstring(rep_.windowRect.bottom));
        }
    }

    // A control must exist and be on screen (not clipped away).
    void expectVisible(const std::wstring& name) {
        const RectEntry* e = findRect(rep_, name);
        if (!e || !e->measured) {
            add(L"missing", name, L"not present in the measured layout");
            return;
        }
        if (rules_.requireOnScreen && e->offscreen) {
            add(L"offscreen", name, L"reported offscreen by UIA");
        }
        if (e->measured && rectIsEmpty(e->rect)) {
            add(L"offscreen", name, L"zero-size rectangle");
        }
    }

    // Two named controls must be laid out side by side, not stacked. `horizontal`
    // means B lies to the right of A with only a small vertical overlap.
    void expectSideBySide(const std::wstring& a, const std::wstring& b,
                          bool horizontal, int maxCrossOverlap = 8) {
        const RectEntry* ea = findRect(rep_, a);
        const RectEntry* eb = findRect(rep_, b);
        if (!ea || !eb) {
            add(L"missing", (!ea ? a : b), L"one of the pair is not measured");
            return;
        }
        if (horizontal) {
            if (eb->rect.left < ea->rect.right - rules_.tolerance) {
                add(L"overlap", b,
                    L"expected to the right of " + a + L" but starts at " +
                        std::to_wstring(eb->rect.left) + L" while " + a +
                        L" ends at " + std::to_wstring(ea->rect.right));
            }
            const long cross = rectOverlapArea(ea->rect, eb->rect);
            if (cross > static_cast<long>(maxCrossOverlap) * 1) {
                // Only report when the overlap is mostly vertical stacking.
                if (static_cast<long>(rectHeight(ea->rect)) * rectWidth(eb->rect) > 0 &&
                    eb->rect.left < ea->rect.right) {
                    add(L"overlap", b, L"unintended overlap area=" + std::to_wstring(cross));
                }
            }
        } else {
            if (eb->rect.top < ea->rect.bottom - rules_.tolerance) {
                add(L"overlap", b,
                    L"expected below " + a + L" but starts at " +
                        std::to_wstring(eb->rect.top) + L" while " + a +
                        L" ends at " + std::to_wstring(ea->rect.bottom));
            }
        }
    }

    // Any two measured controls overlapping by more than minOverlapArea is a
    // layout defect. Run this on a *specific* control to keep the report small;
    // running it globally produces noise from legitimate layered UI.
    void expectNoOverlapWithOthers(const std::wstring& name) {
        if (!rules_.checkOverlap) return;
        const RectEntry* e = findRect(rep_, name);
        if (!e || !e->measured) {
            add(L"missing", name, L"not present in the measured layout");
            return;
        }
        for (size_t i = 0; i < rep_.entries.size(); ++i) {
            const RectEntry& o = rep_.entries[i];
            if (o.name == e->name) continue;
            const long area = rectOverlapArea(e->rect, o.rect);
            if (area > rules_.minOverlapArea) {
                add(L"overlap", name,
                    L"overlaps " + o.label() + L" area=" + std::to_wstring(area));
            }
        }
    }

    const std::vector<LayoutViolation>& violations() const { return v_; }
    bool ok() const { return v_.empty(); }

    // Multi-line summary suitable for a CHECK failure message or an artifact.
    std::wstring summary() const {
        if (v_.empty()) return L"layout ok";
        std::wostringstream o;
        o << L"layout violations: " << v_.size();
        for (size_t i = 0; i < v_.size(); ++i) {
            o << L"\n  " << v_[i].line();
        }
        return o.str();
    }

private:
    void add(const std::wstring& rule, const std::wstring& subject,
             const std::wstring& detail) {
        LayoutViolation v;
        v.rule = rule;
        v.subject = subject;
        v.detail = detail;
        v_.push_back(v);
    }

    RectReport rep_;
    LayoutRules rules_;
    std::vector<LayoutViolation> v_;
};

// ------------------------------------------------------- Window resizing

// Grow or shrink a top level window, keeping its top-left corner anchored so
// repeated resizes in one test do not walk the window off screen. Returns the
// rect actually achieved (GetWindowRect after the call), which callers should
// use rather than assuming the request was honoured: Windows may clamp the
// size to the monitor work area or a min/max track size.
inline RECT resizeWindow(HWND hwnd, int width, int height, bool repaint = true) {
    RECT result = {0, 0, 0, 0};
    if (!hwnd || !::IsWindow(hwnd)) return result;

    RECT before = {0, 0, 0, 0};
    if (!::GetWindowRect(hwnd, &before)) return result;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    // A maximized window silently ignores SetWindowPos: the request "succeeds"
    // and GetWindowRect afterwards returns the unchanged maximized rect, which
    // reads exactly like "the resize did nothing". Restore first, resize, and
    // let WindowStateGuard put the maximize state back at the end of the test.
    if (::IsZoomed(hwnd)) {
        ::ShowWindow(hwnd, SW_RESTORE);
        ::Sleep(120);
        if (!::GetWindowRect(hwnd, &before)) return result;
    }

    ::SetWindowPos(hwnd, nullptr, before.left, before.top, width, height,
                   SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
    if (repaint) {
        ::RedrawWindow(hwnd, nullptr, nullptr,
                       RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    // Give wxWidgets' layout engine a moment to re-run its sizers and for UIA
    // to publish fresh bounding rectangles.
    ::Sleep(180);
    ::GetWindowRect(hwnd, &result);
    return result;
}

// Move + size in one call, for tests that need a specific viewport.
inline RECT moveResizeWindow(HWND hwnd, int x, int y, int width, int height) {
    RECT result = {0, 0, 0, 0};
    if (!hwnd || !::IsWindow(hwnd)) return result;
    ::SetWindowPos(hwnd, nullptr, x, y, width < 1 ? 1 : width,
                   height < 1 ? 1 : height, SWP_NOZORDER | SWP_NOACTIVATE);
    ::Sleep(180);
    ::GetWindowRect(hwnd, &result);
    return result;
}

// Drive the page layout until the window reaches at least the requested size.
// Windows can clamp a resize (max track size, work area), so we loop a bounded
// number of times and report what we actually got instead of failing silently.
// Returns the final rect.
inline RECT ensureMinSize(HWND hwnd, int minWidth, int minHeight, int attempts = 3) {
    RECT current = {0, 0, 0, 0};
    for (int i = 0; i < attempts; ++i) {
        if (!::GetWindowRect(hwnd, &current)) return current;
        if (rectWidth(current) >= minWidth && rectHeight(current) >= minHeight) {
            return current;
        }
        resizeWindow(hwnd, minWidth, minHeight);
    }
    ::GetWindowRect(hwnd, &current);
    return current;
}

// Remember the original rect so a test can put the window back. Restoring keeps
// a resized window from leaking into the next TEST_CASE in the same run.
struct WindowStateGuard {
    HWND hwnd = nullptr;
    RECT original = {0, 0, 0, 0};
    bool captured = false;
    bool wasZoomed = false;   // a maximized frame ignores SetWindowPos outright

    explicit WindowStateGuard(HWND h) : hwnd(h) {
        if (hwnd && ::IsWindow(hwnd)) {
            captured = (::GetWindowRect(hwnd, &original) != FALSE);
            wasZoomed = (::IsZoomed(hwnd) != FALSE);
        }
    }

    ~WindowStateGuard() {
        if (!captured || !hwnd || !::IsWindow(hwnd)) return;
        if (wasZoomed) {
            ::ShowWindow(hwnd, SW_MAXIMIZE);
            return;
        }
        RECT now = {0, 0, 0, 0};
        if (!::GetWindowRect(hwnd, &now)) return;
        if (now.left == original.left && now.top == original.top &&
            rectWidth(now) == rectWidth(original) &&
            rectHeight(now) == rectHeight(original)) {
            return;  // already back in place, do not poke the window again
        }
        ::SetWindowPos(hwnd, nullptr, original.left, original.top,
                       rectWidth(original), rectHeight(original),
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
};

}  // namespace uitest