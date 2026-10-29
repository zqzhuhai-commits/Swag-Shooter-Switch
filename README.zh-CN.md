# 绝地精英 — Switch 移植

名称：绝地精英；作者：Xiao Fan。当前测试版本 **0.14.34**，运行于Atmosphère。

用户已确认能进入游戏、中文显示、按键/触屏响应及自动识别驾驶状态。最新驾驶布局为左摇杆仅前后、右摇杆仅左右，十字键四向及ZR/ZL仍保留。新原生NSP尚未实机验证安装启动，不能将包校验通过视作完整可玩。

## 单文件NSP

本地生成的NSP包含原生NSO、完整游戏RomFS、名称/作者和图标，无需外置NRO。应用ID为`01AF534741470000`。存档及元数据缓存仍使用SD `/switch/swag-userdata/`。构建及验证见[打包说明](packaging/README.md)，校验记录见[audit/nsp-verification-0.14.34.json](audit/nsp-verification-0.14.34.json)。

仓库仅含兼容层源码、构建脚本、图标和校验记录，不上传商业游戏二进制、游戏素材、用户密钥或测试日志。请自行准备`romfs/game/`，包括库、原始Data、font-assets及许可文件。当前包由1863个本地文件构成。

## 构建

安装devkitPro的devkitA64、libnx、SDL2、Mesa EGL/GLES及依赖，以及Python。运行`./build.ps1`生成NRO与ELF；`python tools/verify_build.py`检查NRO和嵌入文件。进一步生成NSP需要Python cryptography、Pillow与自己的本地密钥文件，见打包说明。

## 按键

步战：左摇杆移动，L3切换跑步；右摇杆视角，R3短按趴/长按0.5秒上车交互。B跳，A短蹲/长趴，Y短按拾取第一项/长按换弹，X切枪。ZL按住开镜/松开关镜，ZR射击。L医疗，R手雷，+地图，-背包。十字左右选主武器/手枪、上跳伞、下切物品。

驾驶自动识别：左摇杆上/下前进倒车，右摇杆左/右转向；十字键四向也可用，ZR油门、ZL刹车倒车，A下车。状态读取失败时保留L+R备用切换。当前未实现侧身/屏息/地图标记等未确认入口。

## 日志与测试

默认不创建或更新诊断日志。SD `/switch/swag-debug.flag`存在时，下次启动启用`swag-switch.log`和`swag-switch-crash.log`；旧日志不会自动删除。配置可选文件为`/switch/swag-controls-v3.cfg`，旧`swag-controls.cfg`忽略。

详细历史和验证界限见[修改记录](CHANGELOG.zh-CN.md)，功能核实见[audit/游戏功能核实.md](audit/游戏功能核实.md)。GC说明见[GC_BRIDGE_NOTES.txt](GC_BRIDGE_NOTES.txt)。
