wxWidgets 本身就有通用的滑块组件：**`wxSlider`**。

最基本的用法：

```cpp
auto* slider = new wxSlider(
    parent,
    wxID_ANY,
    50,     // 当前值
    0,      // 最小值
    100,    // 最大值
    wxDefaultPosition,
    wxSize(200, -1),
    wxSL_HORIZONTAL
);
```

事件：

```cpp
slider->Bind(wxEVT_SLIDER, [=](wxCommandEvent& event) {
    int value = slider->GetValue();
    // value: 0 ~ 100
});
```

### 但有个重要区别

如果你问的是你截图里这种：

> **Windows 10 风格的横向 TrackBar / Slider**

那么 `wxSlider` 是**跨平台通用接口**，但它的**实际外观由平台原生控件决定**。

Windows 下通常会使用 Windows 原生 Trackbar，因此：

```cpp
wxSlider
```

在 Win10 上基本就是你截图底部那个类型。

---

### 如果你想要“统一配色的滑块”

例如你前面希望统一成：

```text
轨道：#E5E5E5
进度：#0078D4
滑块：#0078D4
Hover：#106EBE
```

那么我反而**不建议直接用 wxSlider**。

因为 wxSlider 的颜色、滑块形状、轨道绘制在不同 Windows 主题/系统版本下会受到原生控件影响，想做到完全一致比较麻烦。

这种情况下可以自己做一个：

```cpp
class CustomSlider : public wxPanel
```

然后自己处理：

```text
OnPaint
OnMouseDown
OnMouseMove
OnMouseUp
```

自己画：

```text
┌──────────────────────────────────┐
│──────────────●───────────────────│
└──────────────────────────────────┘
       ↑
   #0078D4
```

这样 Windows 10 / Windows 11、不同主题下都能保持完全一致。

### 如果你的目标是你截图这种软件

我会推荐：

**普通场景：**

```cpp
wxSlider
```

**想统一整个程序 UI：**

```cpp
CustomSlider : public wxPanel
```

尤其你这个 `validproxy - Proxy Manager` 界面，后面如果准备把 **ListCtrl / DataViewCtrl + Toolbar + Slider + StatusBar** 全部统一成前面说的 `#0078D4` 配色，建议直接做一套轻量的 **Custom Controls**，而不是过度依赖 Windows 原生控件。
