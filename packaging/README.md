# 单文件原生NSP构建

当前版本0.14.34，名称绝地精英，作者Xiao Fan，应用ID `01AF534741470000`。这是含全部资源的原生NSO NSP，不是指向SD卡NRO的快捷方式。需要可安装并启动自制NSP的Atmosphère环境。0.14.34-fix1修复原生NSP启动句柄初始化；功能修复已由用户确认可进入游戏。

## 输入与命令

先准备本地`romfs/game/`和devkitPro依赖，运行根目录`build.ps1`。保持输出`swag-switch.elf`、`swag-switch.nacp`与`swag-switch.nro`来自同一次构建。安装Python的cryptography和Pillow。

```
python tools/verify_build.py
python tools/build_nsp.py --keys <local-prod.keys-path>
python tools/verify_nsp.py "../绝地精英 [01AF534741470000][0.14.34].nsp" --keys <local-prod.keys-path>
```

默认devkitPro为`C:/devkitPro`，可传`--devkitpro`。默认密钥路径为用户主目录`.switch/prod.keys`，脚本只读取header_key用于自制NCA头部；不得提交密钥内容。图标以`artwork/icon-256.jpg.b64`保存于源码仓库，脚本解码后写入16语言控制资源。资源与大型成品不放仓库。

## 启动适配

libnx原生NSO启动不带NRO的ProcessHandle配置项；`native_process.c`在需要时调用Atmosphère/Mesosphere的`svcGetInfo(..., 65001, INVALID_HANDLE, 0)`获取真实自身句柄，再以两个句柄查询PID比较，提供给安卓ELF加载和数据映射。此前本地HIPC会话方案在实机`svcCreateSession`返回`0x10801`，因此移除。NRO继续使用原有句柄。旧HIPC参考代码的许可继续保留于third_party。NPDM使用Application类型和39位地址空间，允许游戏所需代码映射SVC；句柄修复路径已通过用户实机启动验证；不支持该Mesosphere接口时明确返回错误。

## 验证与来源

`verify_nsp.py`独立检查NCA头、PFS0分块、六级IVFC、NSO解压段哈希、NPDM权限、1863个游戏文件、16语言名称/作者/图标和CNMT。参考容器实现来自fungalspores/condemned-winenx的nsp/build_nsp.py，已保留GPL-3.0许可。NPDM字段参考hacBrewPack模板并适配当前devkitPro工具字段。

- https://github.com/switchbrew/nx-hbloader
- https://github.com/rlaphoenix/hacBrewPack
- https://github.com/fungalspores/condemned-winenx

校验记录：audit/nsp-0.14.34.json及audit/nsp-verification-0.14.34.json。有效包结构不代表设备安装或游戏启动已经通过。
