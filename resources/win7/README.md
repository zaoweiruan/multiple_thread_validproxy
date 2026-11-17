# resources/win7 — Windows 7 SP1 运行时补丁

## libwinpthread-1.dll（MSVCRT 版，仅此一个 DLL）

| 属性 | 值 |
| --- | --- |
| 大小 | 65,442 字节 |
| SHA-256 | `B3AD954EC35305895E2370D0A1E3CA6A8203DB1D37C7C49CF5D06E72A5F0DB69` |
| 导入表 | `KERNEL32.dll`、`msvcrt.dll`（**无** `api-ms-win-crt-*` / `ucrtbase`） |
| 导出 | 覆盖 `bin/validproxy.exe` 需要的全部 23 个符号，含 `clock_gettime64`、`nanosleep64`、`pthread_cond_timedwait64` |
| 上游 | mingw-w64 `winpthreads`（ZPL-2.1 / MIT 兼容，可再分发） |

### 为什么需要它

`-static-libgcc -static-libstdc++` 之后仍剩 **libwinpthread-1.dll** 一个动态依赖。
两个工具链自带的该 DLL 都是 **UCRT 构建**：

| 来源 | 大小 | 导入 |
| --- | --- | --- |
| `E:\clion\bin\mingw\bin\libwinpthread-1.dll` | 72,032 | `api-ms-win-crt-convert/heap/private/runtime/stdio/string` |
| `E:\mingw64\bin\libwinpthread-1.dll` | 56,832 | 同上（+ environment/time） |

未安装 Universal CRT（KB2999226）的 Windows 7 SP1 没有这些转发 DLL，启动即报：

```
无法启动此程序，因为计算机中丢失 api-ms-win-crt-convert-l1-1-0.dll
```

`-static` 也不可行：`E:\clion\bin\mingw` 的 `libpthread.a` 同样是 UCRT 构建
（引用只在 `libucrt*.a` 中定义的 `__intrinsic_setjmpex`），静态链接直接失败。

因此发布包改为随附这个 MSVCRT 版 DLL（由 CMake 在
`VALIDPROXY_STATIC_WX=ON` 时 POST_BUILD 复制到 exe 目录），全链路 MSVCRT，
Win7 SP1 裸机可运行。

### 校验

```powershell
powershell -File scripts/check-win7-deps.ps1 -Path bin -IncludeDlls
```

扫描 exe 与随包 DLL 的导入表，出现 `api-ms-win-crt-*` / `ucrtbase` /
非系统 DLL 即返回 1。

### 替换来源（如需）

任何 **MSVCRT 版** mingw-w64 `libwinpthread-1.dll` 均可，前置条件：

1. 导入表只有 `KERNEL32.dll` + `msvcrt.dll`；
2. 导出 `clock_gettime64`、`nanosleep64`、`pthread_cond_timedwait64`
   （mingw-w64 v10+ 的 time64 符号）。

配置时用 `-DVALIDPROXY_MSVCRT_WINPTHREAD_DLL=<绝对路径>` 覆盖即可。

> 另一条不需要本 DLL 的路线：让 Win7 用户安装 KB2999226 / VC++ 2015-2022
> 运行库（机器级一次），此时工具链自带的 UCRT 版 DLL 也能用。
