# Bugfix: test_proxy_list_model.exe 启动 0xC0000139（comctl32 v6 manifest 缺失）

- 日期: 2026-08-18
- 类型: Bugfix
- 模块: CMakeLists.txt / tests/test_proxy_list_model / wxWidgets 3.3.3 动态链接
- 版本: v1.0
- 状态: ✅ completed

---

## 1. 现象

`ctest -V` 全量回归中 `ProxyListModelTest`（`bin\test_proxy_list_model.exe`）
一启动即崩溃：

```
0xC0000139: STATUS_ENTRYPOINT_NOT_FOUND
```

而主程序 `validproxy.exe` 正常运行（加载同一个
`wxmsw333ud_core_gcc_x64_custom.dll`，346MB debug 动态库）。## 2. 根因

### 2.1 直接原因

`test_proxy_list_model.exe`（以及最小复现 `minwxtest.exe`）**未嵌入
Common Controls v6 manifest**。其导入表中
`wxmsw333ud_core_gcc_x64_custom.dll` 从 `comctl32.dll` 导入 4 个
**v6-only** 导出：

- `GetWindowSubclass`
- `SetWindowSubclass`
- `DefSubclassProc`
- `RemoveWindowSubclass`

无 manifest 时 Windows 加载 `C:\Windows\System32\comctl32.dll`
（**5.82**，导出表仅有 InitCommonControls/Ex，无上述符号）→ 加载器
解析入口点失败 → **0xC0000139**。

### 2.2 为何 validproxy 正常

`validproxy.exe` 通过 `src/ui/icons.rc` 内嵌 `src/ui/app.manifest`
（`1 24 "src/ui/app.manifest"`），声明
`Microsoft.Windows.Common-Controls` 6.0.0.0 依赖 → 加载 WinSxS
`amd64_microsoft.windows.common-controls_...6.0.19041...\comctl32.dll`
（导出表含上述 4 符号）→ 启动成功。### 2.3 调查要点（排除的假说）

排除过程中已系统性验证、**均非根因**：

- **wxbase 导出缺失**：core 从 base 导入 670 符号全部存在于 base 导出表
  （3418 个），REAL MISSING: 0；
- **系统 DLL 符号缺失**：user32/kernel32/ole32 差集为 forwarded 符号
  正则误报，winmm 差集为 dumpbin 块解析 bug（实际导入
  PlaySoundW/joyGetDevCapsW 等 7 符号均存在）；
- **gtest/sqlite3/项目代码**：最小程序 `minwxtest.exe`（仅导入
  base+core）同样 0xC0000139，证明与业务代码无关；
- **wxcore DLL 损坏/路径**：validproxy 运行时确认加载同一 DLL 且正常。

## 3. 修复

`tests/test_proxy_list_model.rc`（新建）复用现有 `src/ui/app.manifest`：

```rc
1 24 "src/ui/app.manifest"
```

`CMakeLists.txt`：`test_proxy_list_model` 源列表追加
`tests/test_proxy_list_model.rc`。

## 4. 验证

- 重新 configure + 仅重建目标：RC 编译 + 链接成功；
- `bin\test_proxy_list_model.exe` 二进制已含 common-controls manifest；
- 直接运行：ProxyListModelTest **4/4 PASSED**（0xC0000139 消失）；
- `ctest --test-dir build -V`：**30/30 全部通过**（此前 29/30）。