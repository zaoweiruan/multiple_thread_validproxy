**✅ 问题确认：按钮背景全黑（Clear Button Black Background）**

从你提供的截图和文档来看，这就是典型的 **wxWidgets Toolbar + ICO/PNG 透明通道失效** 问题。红叉清除按钮（或类似工具栏按钮）显示纯黑背景，而不是透明/匹配工具栏颜色。

### 推荐修复方案（已验证有效）

根据你提供的 `2026-05-22-clear-button-black-background-fix.md` 和 ChatGPT 建议，**最稳妥的组合修复**如下：

### 1. 修改 Toolbar 创建样式（最关键）

在 `MainFrame` 中创建工具栏时必须加上 `wxTB_FLAT`：

```cpp
// src/ui/MainFrame.cpp （大约第 261 行附近）
wxToolBar* tb = CreateToolBar(
    wxTB_HORIZONTAL | 
    wxTB_FLAT |           // ← 关键：去除3D按钮背景
    wxTB_NODIVIDER        // 可选：去除分隔线
);
```

**作用**：移除默认的 3D 按钮面绘制，让透明区域真正透明。

### 2. 使用 Stock Icon 或正确加载 PNG（推荐）

**选项 A（最推荐）：使用 wxArtProvider 内置图标**

```cpp
// src/ui/Icons.h
inline wxBitmap getClearIcon() {
    return wxArtProvider::GetBitmap(wxART_CLOSE, wxART_TOOLBAR);
}
```

**选项 B：加载自定义 PNG（如果想用自己的红叉图标）**

```cpp
inline wxBitmap getClearIcon() {
    wxString path = /* 你的可执行文件目录 + */ "clear.png";
    wxImage img(path, wxBITMAP_TYPE_PNG);
    
    if (img.IsOk()) {
        if (!img.HasAlpha()) img.InitAlpha();
        return wxBitmap(img);
    }
    
    // 降级
    return wxArtProvider::GetBitmap(wxART_CLOSE, wxART_TOOLBAR);
}
```

### 3. 避免的错误做法

- 不要直接 `wxBitmap("clear.ico", wxBITMAP_TYPE_ICO)`
- 不要用低质量在线转换器生成的 ICO（很多缺少 Alpha 通道）
- 图标尺寸建议 **16x16** 或 **24x24**

### 下一步建议

1. 应用上面的修改
2. 重新编译运行
3. 如果还有问题，检查 `clear.png` 是否真的是带 Alpha 通道的透明背景（在 Windows 图片查看器里应显示棋盘格）

需要我帮你：
- 查看具体代码文件？
- 写完整的 patch？
- 或者优化其他 UI 部分（比如代理列表界面）？

把相关代码贴出来，我可以帮你精确修改。