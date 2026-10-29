# SWAG Shooter Switch 兼容层

当前源码版本：**0.14.23 输入测试版**。用户已确认0.14.22能显示首屏动画并进入菜单；0.14.23新增输入注入尚待实机验证，完整战斗操作尚未完成。目标为Atmosphère原生运行，最终制作单文件NSP；当前产物为NRO。

已推进内存预留、元数据读取、GC、JNI、帧回调与图形提交。0.14.22修复窗口尺寸查询返回0×0，随后用户确认约1分钟后出现动画并进入菜单。0.14.23接入单指实体触屏和菜单方向键、A确认、B返回；多点触控、双摇杆及完整战斗映射仍待适配。按单机离线启动处理，域名查询立即返回离线错误。

详细历史、实机证据与限制见[修改与验证记录](CHANGELOG.zh-CN.md)。GC参考说明见[GC_BRIDGE_NOTES.txt](GC_BRIDGE_NOTES.txt)。

## 构建

安装devkitPro的devkitA64、libnx、SDL2、Mesa EGL/GLES及依赖，执行`./build.ps1`。构建产物为`swag-switch.nro`。`python tools/verify_build.py`校验NRO/RomFS结构并逐个核对嵌入文件。

仓库不含游戏资源。需自行准备`romfs/game/`中的libunity.so、libil2cpp.so、libmain.so、bin/Data及font-assets资源；目标Unity版本为2019.4.0f1。所用文件必须与移植针对的二进制相符。另请准备构建脚本引用的romfs测试目录。

## 实机测试

通过完整内存模式hbmenu启动NRO。日志位于`/switch/swag-switch.log`及`/switch/swag-switch-crash.log`；写入目录为`/switch/swag-userdata/`，IL2CPP缓存位于其中的`il2cpp/`。异常文件只有ARMED仅表示未捕获异常，不能证明游戏正常。

当前包含游戏启动探针、Android ABI与线程/TLS测试、虚拟内存、文件缓存、JNI、GC桥，以及独立图形/声音/输入诊断。缺失接口可能中止启动；NRO编译或自检通过均不代表游戏可玩。最新输入日志为GAME_INPUT_READY、GAME_TOUCH、GAME_KEY；native_return仅是注入返回值，是否响应以实机观察为准。构建记录见audit/build-0.14.23.json。
