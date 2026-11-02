# 0.14.72 资源准备与构建

需要devkitPro/devkitA64、libnx、Switch图形依赖及项目Makefile所列库。原游戏ARM64库和Unity数据放入romfs/game，CJK字体替换资源亦需自行准备；这些输入不随源码仓库提供。打包NSP读取本地密钥，密钥不要提交。

## 本地化图片

本次提交包含artwork/brand-localized的七张中文图片及artwork/map-localized/minimap-zh.png，避免构建脚本依赖只存在于作者电脑上的图片。

图片资源编码工具需Python、UnityPy和Pillow；当前本机使用Python3.13、UnityPy1.25.4、Pillow13。运行以下命令之前先准备原始游戏数据：

```powershell
python tools/build_brand_assets.py
python tools/build_startup_logo.py
python tools/build_map_assets.py
python tools/build_royal_texture.py
./build.ps1
python tools/verify_build.py
python tools/verify_squad_rules.py
python tools/verify_health_warning.py
python tools/build_nsp.py
python tools/verify_nsp.py "../绝地精英 [01AF534741470000][0.14.72].nsp"
```

build.ps1会重建中文界面、AI难度和玩法场景覆盖资源，但新图片覆盖文件需先通过上述UnityPy脚本生成。tools/build_royal_texture.py沿用旧转换实验资源；首屏推广禁用由原生与面板逻辑共同完成，不应仅依赖纹理转换。

游戏原始数据只读，修改后的Unity覆盖文件写入romfs/game/font-assets，并通过bionic_io.c路径映射生效。中文地图修改sharedassets2.assets中的Texture2D 189，其他序列化对象逐字节核对不变；启动标志修改globalgamemanagers.assets中的Texture2D 3。

## 验证边界

规则回归执行编译后的ARM64测试代码；步态测试覆盖过快反馈、异常样本、收敛和速度上限。安装包校验覆盖结构、内容哈希、应用名称与图标；不模拟完整Unity游戏，也不等同于Switch实机测试。

升级保留SD /switch/swag-userdata和/switch/swag-ai.cfg；步频诊断在启用/switch/swag-debug.flag后记录GAIT_CONTROL。
