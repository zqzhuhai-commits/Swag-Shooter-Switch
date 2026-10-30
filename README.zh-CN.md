# 绝地精英 — Switch 移植

名称：绝地精英；作者：Xiao Fan。当前正式版本 **0.14.38**，运行于Atmosphère。用户于2026年10月30日确认此版本实机测试通过。

使用与实机测试相同的NSP发布。包含原生启动修复、离线商店、本地购买发放、激励奖励直接领取及按钮动态价格文字修复。现金、金币、钻石每次启动进入菜单后各补为10,000,000，之后正常记录消费/奖励。修正85条中文词条及732处场景固定文字。

## 单文件NSP

[下载0.14.38](https://github.com/zqzhuhai-commits/Swag-Shooter-Switch/releases/tag/v0.14.38)，解压后安装，覆盖旧版时保留存档。NSP包含原生NSO、完整资源与图标，无需外置NRO。应用ID为`01AF534741470000`，包版本38。存档/缓存仍使用SD `/switch/swag-userdata/`。

详情见[发布说明](RELEASE-0.14.38.md)、[打包说明](packaging/README.md)和[audit/nsp-verification-0.14.38.json](audit/nsp-verification-0.14.38.json)。实机通过不代表所有玩法或系统版本均已穷尽验证。AI难度增强留待后续测试版本。

## 构建

自行准备`romfs/game/`原始资源、已有CJK字体替换资源及许可。运行`python tools/build_chinese_assets.py`生成中文覆盖，再运行`./build.ps1`及`python tools/verify_build.py`；生成NSP见打包说明。源码不含游戏资源、用户密钥、设备原始日志或大型安装包；成品通过Release附件提供。当前嵌入文件1865个。

## 按键

步战：左摇杆移动，L3切换跑步；右摇杆视角，R3短按趴/长按0.5秒上车交互。B跳，A短蹲/长趴，Y短按拾取第一项/长按换弹，X切枪。ZL按住开镜/松开关镜，ZR射击。L医疗，R手雷，+地图，-背包。十字左右选主武器/手枪、上跳伞、下切物品。

驾驶自动识别：左摇杆上/下前进倒车，右摇杆左/右转向；十字键四向也可用，ZR油门、ZL刹车倒车，A下车。状态读取失败时保留L+R备用切换。当前未实现侧身/屏息/地图标记等未确认入口。

## 日志与测试

默认不创建或更新诊断日志。SD `/switch/swag-debug.flag`存在时，下次启动启用`swag-switch.log`和`swag-switch-crash.log`；旧日志不会自动删除。配置可选文件为`/switch/swag-controls-v3.cfg`，旧`swag-controls.cfg`忽略。

详细历史和验证界限见[修改记录](CHANGELOG.zh-CN.md)，功能核实见[audit/游戏功能核实.md](audit/游戏功能核实.md)。GC说明见[GC_BRIDGE_NOTES.txt](GC_BRIDGE_NOTES.txt)。
