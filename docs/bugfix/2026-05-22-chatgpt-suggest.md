这是典型的 Win32 / wxWidgets 工具栏 ICO 透明通道失效问题。

你这个截图里：

* 左边蓝色图标正常透明
* 红叉按钮背景纯黑

说明：

* ICO 被成功加载了
* 但透明 Mask / Alpha 没生效
* Toolbar 又在用默认按钮背景绘制

你上传的文档里已经基本分析对了。核心修复是：

---

# 一、必须使用 FLAT Toolbar

这是最关键的一步。

你现在大概率：

```cpp
CreateToolBar();
```

应改成：

```cpp
wxToolBar* tb = CreateToolBar(
    wxTB_HORIZONTAL |
    wxTB_FLAT |
    wxTB_NODIVIDER
);
```

原因：

默认 toolbar 会给 tool 绘制 3D 按钮底色。

黑色背景其实不是 ICO 本身，而是：
透明区域没有正确 Mask 后，
toolbar 用黑色填充了 bitmap 背景。

---

# 二、ICO 不要直接 wxBitmap(path)

这是很多人踩坑点。

错误写法：

```cpp
wxBitmap bmp("clear.ico", wxBITMAP_TYPE_ICO);
```

因为：

* Windows ICO 有多种编码
* wxBitmap 对 alpha 支持并不稳定
* 尤其 MinGW/wxMSW

应改为：

```cpp
wxImage img(path, wxBITMAP_TYPE_ICO);

if (img.IsOk())
{
    img.InitAlpha(); // 很重要

    wxBitmap bmp(img);
    return bmp;
}
```

---

# 三、如果 ICO 没 Alpha，需要手动 Mask

很多 ICO 实际没有 alpha。

尤其：

* 在线 ICO 转换器生成的
* 老版 16x16 ico
* BMP-based ico

这种必须：

```cpp
wxMask* mask = new wxMask(bmp, wxColour(0,0,0));
bmp.SetMask(mask);
```

但：

黑色会被透明掉。

所以图标内部不能有黑色内容。

更推荐：

* 用洋红 `(255,0,255)` 做透明色

---

# 四、最推荐方案：别用 ICO，当 Toolbar Icon 用 PNG

这是现代 GUI 最稳方案。

因为：

wxWidgets 对 PNG alpha 支持远好于 ICO。

直接：

```cpp
wxImage img("clear.png", wxBITMAP_TYPE_PNG);
wxBitmap bmp(img);
```

然后：

```cpp
tb->AddTool(ID_CLEAR, "", bmp);
```

基本不会翻车。

---

# 五、Windows 下 ICO 真正稳定要求

如果你坚持 ICO：

必须：

* 32-bit RGBA
* 含 alpha channel
* 非 indexed color
* 非 BMP mask only

推荐：

用 Photoshop / GIMP / Axialis 创建。

不要用垃圾在线转换器。

---

# 六、你当前问题 90% 是这个

你现在大概率：

```cpp
wxBitmap("clear.ico", wxBITMAP_TYPE_ICO)
```

* 默认 toolbar style。

所以：

透明区域被渲染成黑色。

---

# 七、最终建议（最稳）

直接改：

## MainFrame.cpp

```cpp
wxToolBar* tb = CreateToolBar(
    wxTB_HORIZONTAL |
    wxTB_FLAT |
    wxTB_NODIVIDER
);
```

---

## Icons.h

改成 PNG：

```cpp
inline wxBitmap getClearIcon()
{
    wxString path = exeDir + "/docs/design/ui/clear.png";

    wxImage img(path, wxBITMAP_TYPE_PNG);

    if (img.IsOk())
        return wxBitmap(img);

    return wxArtProvider::GetBitmap(wxART_CLOSE, wxART_TOOLBAR);
}
```

---

# 八、如果仍然黑底

说明你的 PNG 本身没有 alpha。

验证：

Windows 图片查看器里：

* 是否能看到棋盘透明背景
* 还是纯黑背景

如果已经是黑背景：

那图标资源本身就坏了。

---

# 九、额外建议（非常重要）

toolbar icon 最好：

* 16x16
* 24x24
* PNG RGBA
* 不要 ICO

wxWidgets 在 Windows 上：
PNG toolbar 是最省事的。
