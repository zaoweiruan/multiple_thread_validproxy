根因不是 `wxSlider` 创建了两条滑道，而是 **Windows 主题绘制的 Trackbar 沟槽由上下两部分组成**：上方是已选中/填充区域，下方是未选中区域；当启用 `wxSL_AUTOTICKS` 或主题视觉样式时，系统会绘制额外的凹槽、边框和高亮，视觉上就像两条平行滑道。`wxSlider` 在 Windows 下确实封装的是原生 `msctls_trackbar32`，wxWidgets 没有 `wxSL_FLAT` 这种可移植样式标志。

不过，从截图看，问题还可能叠加了 **控件高度过大或主题 Trackbar 的上下边框被拉开**：

```cpp
wxDefaultSize
```

让 Windows 根据主题决定默认高度，而系统 Trackbar 的主题绘制通常包含上下两条边缘线。建议先从控件尺寸和样式入手。

## 1. 先去掉 `wxSL_AUTOTICKS`

你的滑块没有显示刻度线，因此不需要：

```cpp
wxSL_AUTOTICKS
```

改成：

```cpp
slider_ = new wxSlider(
    this,
    wxID_ANY,
    FloatingWidgetPolicy::hideDelayToSlider(hideDelayMs_),
    0,
    1000,
    wxDefaultPosition,
    wxSize(-1, 24),
    wxSL_HORIZONTAL
);
```

`wxSL_AUTOTICKS` 主要用于自动显示刻度，可能导致 Trackbar 使用不同的主题绘制路径。虽然它通常不会单独产生两条 groove，但去掉后可以排除一个变量。

## 2. 限制滑块高度

截图中的滑块区域高度比较大，建议不要使用 `wxDefaultSize`，而是明确指定一个较小高度：

```cpp
slider_ = new wxSlider(
    this,
    wxID_ANY,
    FloatingWidgetPolicy::hideDelayToSlider(hideDelayMs_),
    0,
    1000,
    wxDefaultPosition,
    wxSize(-1, 22),
    wxSL_HORIZONTAL
);
```

可以尝试：

```cpp
wxSize(-1, 18)
wxSize(-1, 20)
wxSize(-1, 24)
```

但不要把高度压得过小，否则滑块 thumb 可能被裁剪。

如果是通过 sizer 放置，还要检查 sizer 是否把它纵向拉伸了。例如：

```cpp
sizer->Add(slider_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
```

这里的 proportion 应该是 `0`。不要这样写：

```cpp
sizer->Add(slider_, 1, wxEXPAND);
```

`proportion = 1` 会把 Trackbar 拉高，从而把主题绘制的上下 groove 明显分离。

## 3. 检查是否使用了 `wxEXPAND`

推荐：

```cpp
auto* slider = new wxSlider(
    this,
    wxID_ANY,
    value,
    0,
    1000,
    wxDefaultPosition,
    wxSize(-1, 22),
    wxSL_HORIZONTAL
);

mainSizer->Add(slider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
```

不推荐：

```cpp
mainSizer->Add(slider, 1, wxEXPAND);
```

如果父窗口或 sizer 强制给了较大的高度，可以单独设置最小尺寸：

```cpp
slider_->SetMinSize(wxSize(-1, 22));
slider_->SetMaxSize(wxSize(-1, 22));
```

或者在布局完成后检查：

```cpp
wxLogDebug(
    "slider size = %d x %d",
    slider_->GetSize().GetWidth(),
    slider_->GetSize().GetHeight()
);
```

如果高度达到 30、40 甚至更大，基本可以确认是布局拉伸导致视觉问题加重。

## 4. 不要使用 `wxSL_AUTOTICKS`

如果需要刻度，但不需要系统自动绘制，可以改为：

```cpp
slider_ = new wxSlider(
    this,
    wxID_ANY,
    value,
    0,
    1000,
    wxDefaultPosition,
    wxSize(-1, 22),
    wxSL_HORIZONTAL
);
```

如果确实需要刻度，再考虑：

```cpp
slider_->SetTickFreq(100);
```

不过 Windows 原生 Trackbar 的刻度绘制仍由系统主题控制，无法像自绘控件那样精确控制 groove 外观。

## 5. 使用原生 Trackbar 的视觉样式控制

可以通过 Windows API 修改 Trackbar 的主题：

```cpp
#include <windows.h>
#include <uxtheme.h>

#pragma comment(lib, "uxtheme.lib")

HWND hwnd = reinterpret_cast<HWND>(slider_->GetHandle());

SetWindowTheme(hwnd, L"", L"");
```

这会禁用该控件的主题绘制，让它回到较朴素的经典风格。

如果希望完全不使用主题：

```cpp
SetWindowTheme(hwnd, L" ", L" ");
```

通常建议先试：

```cpp
SetWindowTheme(hwnd, L"", L"");
```

不同 Windows 版本和主题下，两种写法的行为可能略有差异。调用后可以：

```cpp
slider_->Refresh();
slider_->Update();
```

例如：

```cpp
slider_ = new wxSlider(
    this,
    wxID_ANY,
    value,
    0,
    1000,
    wxDefaultPosition,
    wxSize(-1, 22),
    wxSL_HORIZONTAL
);

HWND hwnd = reinterpret_cast<HWND>(slider_->GetHandle());
SetWindowTheme(hwnd, L"", L"");
```

但这只是改变系统绘制风格，不能保证外观在所有 Windows 主题下完全一致。

## 6. 去掉 `wxSL_AUTOTICKS` 后仍有双轨怎么办

如果你已经确认：

- `wxSL_AUTOTICKS` 已去掉；
- Trackbar 高度没有被 sizer 拉伸；
- `SetWindowTheme()` 之后仍然存在两条线；

那么基本就是 Windows 原生主题的 groove 绘制结果。此时不要继续尝试 `wxSlider` 样式标志，因为 `wxSlider` 本身无法控制 Trackbar 的内部绘制细节。

有三个选择：

### 方案 A：接受系统样式

优点是：

- 原生行为；
- 键盘、鼠标、辅助功能完整；
- 高 DPI 和主题适配较好；
- 维护成本最低。

适合普通设置界面。

### 方案 B：使用 Windows 原生 Trackbar + 自定义主题

保留 `wxSlider`，通过：

```cpp
SetWindowTheme(hwnd, L"", L"");
```

或自定义窗口过程处理绘制。不过原生 Trackbar 的非客户区和主题绘制比较复杂，收益有限。

### 方案 C：改成自绘滑块

如果浮动窗口需要非常简洁的视觉效果，建议不要使用 `wxSlider`，而是使用一个自定义 `wxPanel` 或 `wxControl`：

```cpp
class FlatSlider : public wxControl
{
public:
    FlatSlider(wxWindow* parent)
        : wxControl(parent, wxID_ANY)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &FlatSlider::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &FlatSlider::OnMouseDown, this);
        Bind(wxEVT_MOTION, &FlatSlider::OnMouseMove, this);
    }

private:
    int value_ = 500;

    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();

        const wxRect r = GetClientRect();

        int y = r.GetHeight() / 2;
        int left = 10;
        int right = r.GetWidth() - 10;

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(190, 190, 190)));
        dc.DrawRoundedRectangle(
            left,
            y - 2,
            right - left,
            4,
            2
        );

        int x = left + (right - left) * value_ / 1000;

        dc.SetBrush(wxBrush(wxColour(30, 130, 220)));
        dc.DrawCircle(x, y, 8);
    }

    void OnMouseDown(wxMouseEvent& event)
    {
        UpdateValue(event.GetX());
        CaptureMouse();
    }

    void OnMouseMove(wxMouseEvent& event)
    {
        if (event.Dragging() && event.LeftIsDown())
            UpdateValue(event.GetX());
    }

    void UpdateValue(int x)
    {
        const int left = 10;
        const int right = GetClientSize().GetWidth() - 10;

        x = std::clamp(x, left, right);
        value_ = (x - left) * 1000 / (right - left);

        Refresh();
        Update();
    }
};
```

这种方式可以明确保证只有一条轨道，并完全控制：

- 轨道颜色；
- 轨道粗细；
- 已选中区域；
- thumb 大小；
- 圆角；
- 鼠标悬停状态；
- 高 DPI 缩放；
- 深色模式。

## 推荐修改

对于你当前的代码，我建议按以下顺序修改：

```cpp
slider_ = new wxSlider(
    this,
    wxID_ANY,
    FloatingWidgetPolicy::hideDelayToSlider(hideDelayMs_),
    0,
    1000,
    wxDefaultPosition,
    wxSize(-1, 22),
    wxSL_HORIZONTAL
);

slider_->SetMinSize(wxSize(-1, 22));
slider_->SetMaxSize(wxSize(-1, 22));

#ifdef __WXMSW__
#include <windows.h>
#include <uxtheme.h>

HWND hwnd = reinterpret_cast<HWND>(slider_->GetHandle());
SetWindowTheme(hwnd, L"", L"");
#endif
```

布局使用：

```cpp
sizer->Add(slider_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
```

不要使用：

```cpp
sizer->Add(slider_, 1, wxEXPAND);
```

## 判断根因的最小测试

可以先创建一个完全独立的测试窗口：

```cpp
auto* slider = new wxSlider(
    this,
    wxID_ANY,
    500,
    0,
    1000,
    wxDefaultPosition,
    wxSize(300, 22),
    wxSL_HORIZONTAL
);

auto* sizer = new wxBoxSizer(wxVERTICAL);
sizer->Add(slider, 0, wxEXPAND | wxALL, 10);
SetSizerAndFit(sizer);
```

如果这个测试窗口只有一条正常轨道，则根因在 `StandaloneFloatingWidget` 的布局拉伸或父窗口背景绘制。

如果测试窗口仍然有两条轨道，则根因是当前 Windows 主题的原生 Trackbar 绘制，应使用 `SetWindowTheme()` 或改为自绘控件。

从你给出的截图和代码看，最可能的组合根因是：

1. `wxDefaultSize` 导致 Trackbar 高度由系统或 sizer 放大；
2. `wxSL_AUTOTICKS` 让 Windows 采用带额外绘制元素的 Trackbar 主题；
3. Windows 视觉样式绘制了上下 groove/边框；
4. 并非 wxWidgets 创建了两个 slider 轨道。