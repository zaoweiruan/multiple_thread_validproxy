# Bugfix: StandaloneMonitorDialog 构造期 wxASSERT 崩溃

| 项目 | 内容 |
| :--- | :--- |
| 日期 | 2026-08-21 |
| 模块 | `src/ui/StandaloneMonitorDialog.cpp` |
| 严重级别 | 高（功能不可用：Ctrl+M 打开独立监控对话框即弹断言框并中止） |
| 状态 | 已修复 |

## 1. 故障现象

GUI 中触发「独立监控」入口（`MainFrame::onMenuStandaloneMonitor`）后，
弹出 wx 断言对话框 *"A debugging check in this application has failed."*，随后中止。

## 2. 根因分析（RCA）

### 2.1 定位过程（静态/动态地址匹配）

断言堆栈仅给出未解析运行时地址。通过 `objdump -d` 全量反汇编 +
基址无关差分匹配求解加载基址 `R = 0x7ff69aea0000`，解码得到：

```text
[13] MainFrame::onMenuStandaloneMonitor        (0x1400aa578)
[12] StandaloneMonitorDialog::StandaloneMonitorDialog 构造函数 (ret=0x1400d2bd2)
[11] 内联副本 wxSizer::Add(wxWindow*,...)       (0x14033b9a3)
[10] 内联副本 wxSizer::Add(wxSizerItem*)        (0x14033b924)
[09] wxSizer::Insert                            (wxcore DLL)
[08] wxBoxSizer::DoInsert  ← wxFAIL_MSG 触发点
```

`ret=0x1400d2bd2` 对应构造函数中调用点 `0x1400d2bcd`：
反汇编确认参数 `id=0x1389(wxID_CLOSE)`、`flags=0x200(wxALIGN_RIGHT)`。

### 2.2 根因

`StandaloneMonitorDialog.cpp:50`：

```cpp
wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
buttonSizer->Add(new wxButton(this, wxID_CLOSE, L"关闭"), 0, wxALIGN_RIGHT);  // ← 非法
```

wxWidgets 3.3 `wxBoxSizer::DoInsert`（sizer.cpp:2389）对水平 sizer 强制校验：
**只允许垂直对齐标志**（TOP / CENTRE_VERTICAL / BOTTOM）。
`wxALIGN_RIGHT` 属于水平对齐标志，在水平 sizer 中无意义且被忽略，
Debug 构建下直接 `wxFAIL_MSG` 中止。

> 注：整行按钮的水平右对齐已由外层
> `topSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | ...)`（垂直 sizer，合法）实现，
> 内层标志本就冗余。

## 3. 修复方案

移除内层非法标志（最小改动）：

```diff
-    buttonSizer->Add(new wxButton(this, wxID_CLOSE, L"关闭"), 0, wxALIGN_RIGHT);
+    buttonSizer->Add(new wxButton(this, wxID_CLOSE, L"关闭"));
```

## 4. 验证

- [x] Debug 全量重编译通过
- [ ] GUI 回归：Ctrl+M 打开独立监控对话框正常显示、关闭按钮可用
