# 单文件原生NSP构建

当前版本0.14.38，名称绝地精英，作者Xiao Fan，应用ID `01AF534741470000`。这是含全部资源的原生NSO NSP，不是指向SD卡NRO的快捷方式。需要可安装并启动自制NSP的Atmosphère环境。用户已确认本版本实机测试通过，发布使用相同成品。

## 输入与命令

先准备本地`romfs/game/`和devkitPro依赖，运行根目录`build.ps1`。保持输出`swag-switch.elf`、`swag-switch.nacp`与`swag-switch.nro`来自同一次构建。安装Python的cryptography和Pillow。

```
python tools/verify_build.py
python tools/build_nsp.py --keys <local-prod.keys-path>
python tools/verify_nsp.py "../绝地精英 [01AF534741470000][0.14.38].nsp" --keys <local-prod.keys-path>
```

默认devkitPro为`C:/devkitPro`，可传`--devkitpro`。默认密钥路径为用户主目录`.switch/prod.keys`，脚本只读取header_key用于自制NCA头部；不得提交密钥内容。图标以`artwork/icon-256.jpg.b64`保存于源码仓库，脚本解码后写入16语言控制资源。资源与大型成品不放仓库。

## 启动适配

libnx原生NSO启动不带NRO的ProcessHandle配置项；`native_process.c`使用Atmosphère/Mesosphere `svcGetInfo(...,65001,INVALID_HANDLE,0)`取得真实自身句柄并核对PID。原本地HIPC会话路径曾触发资源限制0x10801，已移除。NRO继续使用加载器句柄。NPDM使用Application类型和39位地址空间，允许所需代码映射SVC。此前参考代码许可保留于third_party。

中文覆盖由`tools/build_chinese_assets.py`从本地原始资源生成，写入font-assets目录，保持原对象ID并重建序列化文件偏移。先运行此脚本，再构建；编译不会自行下载或修改游戏资源。

## 验证与来源

`verify_nsp.py`独立检查NCA头、PFS0分块、六级IVFC、NSO解压段哈希、NPDM权限、1865个游戏文件、16语言名称/作者/图标和CNMT。参考容器实现来自fungalspores/condemned-winenx的nsp/build_nsp.py，已保留GPL-3.0许可。NPDM字段参考hacBrewPack模板并适配当前devkitPro工具字段。

- https://github.com/switchbrew/nx-hbloader
- https://github.com/rlaphoenix/hacBrewPack
- https://github.com/fungalspores/condemned-winenx

校验记录：audit/nsp-0.14.38.json及audit/nsp-verification-0.14.38.json。本版本用户已确认实机测试通过；不宣称所有玩法和系统版本均已穷尽验证。
