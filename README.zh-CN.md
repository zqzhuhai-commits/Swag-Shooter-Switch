# SWAG Shooter Switch 兼容层

当前源码版本：**0.14.10 测试版，尚未确认进入游戏画面**。目标为Atmosphère原生运行，最终制作单文件NSP；当前产物为NRO。

最新实机日志确认0.14.9混合权限与Unity内存序列自检通过，但旧mmap为大型虚拟预留分配实际堆内存，累计约2.68 GiB后分配失败并触发Unity崩溃。0.14.10改为延迟分配实际内存，包含6段大型并存预留自检；新实现与新测试尚待实机验证。

详细历史、实机证据与限制见[修改与验证记录](CHANGELOG.zh-CN.md)。GC参考说明见[GC_BRIDGE_NOTES.txt](GC_BRIDGE_NOTES.txt)。

## 构建

安装devkitPro的devkitA64、libnx、SDL2、Mesa EGL/GLES及依赖，执行`./build.ps1`。构建产物为`swag-switch.nro`。`python tools/verify_build.py`校验NRO/RomFS结构并逐个核对嵌入文件。

仓库不含游戏资源。需自行准备`romfs/game/`中的libunity.so、libil2cpp.so、libmain.so、bin/Data及font-assets资源；目标Unity版本为2019.4.0f1。所用文件必须与移植针对的二进制相符。另请准备构建脚本引用的romfs测试目录。

## 实机测试

通过完整内存模式hbmenu启动NRO。日志位于`/switch/swag-switch.log`及`/switch/swag-switch-crash.log`；写入目录为`/switch/swag-userdata/`，IL2CPP缓存位于其中的`il2cpp/`。异常文件只有ARMED仅表示未捕获异常，不能证明游戏正常。

当前包含游戏启动探针、Android ABI与线程/TLS测试、虚拟内存、文件缓存、JNI、GC桥，以及独立图形/声音/输入诊断。缺失接口可能中止启动；NRO编译或自检通过均不代表游戏可玩。按键映射仍处于诊断预览阶段。
