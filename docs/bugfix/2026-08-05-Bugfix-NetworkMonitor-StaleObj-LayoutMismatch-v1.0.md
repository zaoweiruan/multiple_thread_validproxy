# Bugfix: NetworkMonitor 陈旧 .obj 布局不匹配（ODR 违例）导致启动期崩溃 (2026-08-05)

## 症状

- `bin\validproxy.exe`（GUI）启动期确定性崩溃，Access Violation `0xc0000005`，读地址 `0x0`。
- 两次复现（8/5 08:43 与 10:39）均为**同一指令**：异常地址模块偏移 `+0x30930a`，异常参数相同。
- 崩溃日志最后一行均停在 `[MainFrame] initPanels...`（MainFrame.cpp:215-216 调用、:563-619 行之间），
  其后任何日志点（`AUI panes registered` / `initPanels done` / `Constructor end`）永不出现——
  **崩溃发生在启动阶段 UI 线程初始化期间，监控线程首轮探测即触发**。
- 两次崩溃的异常线程均为非主线程工作线程（TID=5688 / TID=14964），与监控线程特征一致。

## 根因（RCA）

**NetworkMonitor 类布局不匹配（ODR 违例）：同一二进制内两个编译单元对类布局认知不一致。**

1. **时间戳证据（决定性）**：

   | 文件 | mtime | 布局 |
   |------|-------|------|
   | `include/NetworkMonitor.h`（8/4 改动，+25 行新增 `dnsCache_`/`dnsCacheMutex_`） | 2026/8/4 17:09:00 | 新 |
   | `build/.../src/NetworkMonitor.cpp.obj` | 2026/8/4 17:11:28 | 新 |
   | **`build/.../src/ui/AppController.cpp.obj`** | **2026/8/4 9:05:12**（早于头文件改动 8 小时） | **旧** |

2. 8/4 17:09 修改 `NetworkMonitor.h`，在类末尾新增成员
   `mutable std::unordered_map<std::string,int64_t> dnsCache_;`（:75）与
   `mutable std::mutex dnsCacheMutex_;`（:76），**改变类大小与成员偏移**。
3. 17:11 仅 `NetworkMonitor.cpp` 重编（新布局）；**`AppController.cpp` 未重编**——它拥有
   `NetworkMonitor netMon_;` 成员（`src/ui/AppController.h`），且在构造器中
   `netMon_.Start(checkUrls, checkIntervalMs, checkTimeoutMs)`（AppController.cpp:44）启动监控线程。
4. 同一进程内两个 TU 对 `NetworkMonitor` 布局认知不一致：AppController 构造 `netMon_` 时按旧布局
   只初始化前部成员；新成员 `dnsCache_` 落在旧布局眼中的"尾部未初始化字节"上，
   **`unordered_map` 的构造函数从未执行**（`_M_buckets` 等内部指针为垃圾值）。
5. 监控线程 `ThreadLoop`（由新布局的 NetworkMonitor.cpp 编译）首轮循环即调用
   `hasValidDnsCache(host)`（NetworkMonitor.cpp:139）→ `dnsCache_.find(hostname)`（:46）→
   访问垃圾内存中的 `_M_buckets` → 反汇编确认崩溃指令 `0x14030930a` = `mov (%rax),%rax`
   （rax = `_M_buckets + __bkt*8`），有效地址 0x0 → **首次 find() 即确定性崩溃**。

### 与全部证据自洽性

- **崩溃符号精确命中**：`addr2line -f -C -e bin\validproxy.exe 0x14030930a` →
  `std::_Hashtable<std::string, pair<const std::string, long long>>::_M_find_before_node(...)`
  @ `bits/hashtable.h:2067` —— 正是 `unordered_map<string,int64_t>` = `dnsCache_` 的 `.find()`。
- **转储寄存器一致**：Rdx=垃圾 `__bkt`、R9=垃圾 hash、R8=栈地址（`hasValidDnsCache` 的 hostname 参数引用）
  —— 指向**未初始化**的 map。
- **时序一致**：日志停在 `initPanels...`（任何 probe 完成前）→ ThreadLoop 第一次 find() 即崩；
  若 map 正常，空表 find() 不会进入 `_M_find_before_node` 循环体。
- **两次转储确定性复现**：同一指令 +0x30930a、同 fault addr 0 —— 布局 mismatch 是确定性的，
  与 ASLR 无关。
- **NetworkMonitorTest 却通过**：test target 的 `NetworkMonitor.cpp.obj` 也是 17:11 新布局，
  测试 TU 无旧布局消费者，无 mismatch —— 解释了"单测绿但运行时崩"的悖论。

### 陈旧 .obj 为何存活（Ninja 依赖机制澄清）

- 初判"depfile 缺失"系**误判**：Ninja 的 `deps=gcc` 特性在读取 depfile 后即删除 `.d` 文件，
  依赖实际存于 `build/.ninja_deps` 二进制 —— build 目录无 `.d` 属**正常**。
- 旧 build 树的 `.ninja_deps` 未能记录 `AppController.cpp.obj → NetworkMonitor.h` 的依赖
  （缺失/过期/损坏，旧树已删无法考证），导致 Ninja 在头文件变更后未触发该 .obj 重编。
- 本次全量重建后，新树 `.ninja_deps`（1.35MB）经 `ninja -t deps` 验证：
  `AppController.cpp.obj: #deps 1063, deps mtime (VALID)`，明确包含
  `include/NetworkMonitor.h` —— 依赖跟踪恢复正常，未来头文件改动自动触发重编。

## 修复

### 1. 全量清理重建 build 树（消除一切潜在陈旧 .obj）

```
rm -rf build
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ^
  -DCMAKE_RC_COMPILER=E:/w64devkit/bin/windres.exe ^
  -DCMAKE_CXX_FLAGS_DEBUG="-g3 -O0" -DCMAKE_RC_FLAGS="-O coff"
cmake --build build --parallel 8
```

构建 399/399 成功。产物 `bin\validproxy.exe`（142,585,842 字节）。

### 2. 顺带解决的 4 个构建环境障碍

| # | 障碍 | 根因 | 解法 |
|---|------|------|------|
| 1 | 链接失败（oldnames.lib/msvcrtd.lib 找不到） | PATH 中 clang++（E:\clang+llvm-22.1.5）被 CMake 探测为编译器 | 显式 `-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`（e:\w64devkit\bin） |
| 2 | g++ 编译报 `unrecognized -Xclang` | CMake 新版在 Windows 对 GNU/Clang 自动探测 CodeView，向 CMAKE_CXX_FLAGS_DEBUG 写入 `-Xclang -gcodeview`（clang 专属） | `-DCMAKE_CXX_FLAGS_DEBUG="-g3 -O0"` 覆盖 |
| 3 | `-loldnames` 找不到 | w64devkit 全树无 liboldnames.a（wxWidgets imported target 链接接口引入） | `ar rcs E:\w64devkit\x86_64-w64-mingw32\lib\liboldnames.a` 创建空库 |
| 4 | `icons.rc.res: file format not recognized` | windres (Binutils 2.42) 默认输出格式非 COFF；CMake 默认探测到 llvm-rc.exe（语法 `-fo`，不支持 `-O coff`） | `-DCMAKE_RC_COMPILER=E:/w64devkit/bin/windres.exe -DCMAKE_RC_FLAGS="-O coff"` |

### 3. 同步运行时副本

```
Copy-Item bin\validproxy.exe bin\worker\validproxy.exe -Force
```

（进程未运行时执行；构建不自动复制到 `bin/worker/`，见 AGENTS.md §4.1。）

## 验证

- `ctest --test-dir build -R NetworkMonitorTest --output-on-failure` → **Passed 8.07s**。
- **GUI 前台启动存活**：Start-Process 启动后 12 秒进程 ALIVE（pid=15844），随后清理；
  `bin\temp` 无新 crash dump（仍只有 4 个历史 dump：0720/0728/0843/1039）。
  （background_process 方式启动退出是后台 session 无桌面交互所致，非崩溃。）
- **日志决定性对比**：新日志 `bin\log\ui_20260805_111937.log` 完整走到
  `[MainFrame] Constructor end`（含 `AUI panes registered` / `initPanels done` /
  `SetMenuBar done` / `Constructor end`）；旧崩溃日志停在 `initPanels...` 永不前进。
  initPanels 耗时 7 秒（含 loadSubscriptions/loadProxies 后台等待），监控线程全程存活未崩。

## 调查方法论沉淀（minidump 定位无符号崩溃）

本次通过 minidump + DWARF 符号完成无 GDB 交互的根因定位，要点：

1. **崩溃转储真实位置**：`bin\temp\crash_YYYYMMDD_HHMMSS.dmp`，配套日志 `bin\log\ui_*.log`。
2. **解析脚本**：`C:\Users\dsm\AppData\Local\Temp\kilo\parse_minidump.py`
   （AMD64 CONTEXT 偏移：ContextFlags@0x30、Rip@0xF8；异常流 ThreadId@rva+12；
   模块名 MINIDUMP_STRING = {Length@0, utf-16-le buffer@+4}）。
3. **addr2line 必须传完整 VMA**（映像基址 0x140000000 + RVA），不是 RVA；
   `.text` VMA=0x140001000 文件偏移 0x600。崩溃 RVA 0x30930a → VMA 0x14030930a。
4. **旧 dump 的 Stack 描述符为垃圾**（无栈内存），无法软解栈 —— 只能靠崩溃指令符号 +
   寄存器 + 日志时序三角定位。
5. **布局 mismatch 型崩溃的指纹**：崩溃符号是成员函数但寄存器全是垃圾值、单测绿但运行时崩、
   时间戳显示某 .obj 早于头文件改动 —— 三者同时出现即可锁定 ODR 违例。

## 遗留与操作

- **无代码改动**（本次为纯构建/环境修复；工作区 32 个未提交源码改动与本次崩溃无关，另行审查）。
- 建议后续在 CI/构建脚本中增加 **build 树完整性检查**（如 `ninja -t deps` 抽查关键 .obj
  对头文件依赖、或定期全量清理重建），防止陈旧 .obj 类问题复发。
- 旧崩溃转储 `bin\temp\crash_20260805_{084357,103953}.dmp` 可归档或删除。
