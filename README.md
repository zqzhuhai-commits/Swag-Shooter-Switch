# 绝地精英 / SWAG Shooter Switch

Android ARM64 Unity compatibility port for Atmosphère. Game name: 绝地精英. Author: Xiao Fan.

Current test version: **0.14.34**. Users have reported gameplay, Chinese text, touch/controller input and automatic vehicle state recognition. The native standalone NSP is structurally verified but still awaits installation/launch testing on hardware.

The NSP contains the native program, full game RomFS and icon; no external NRO is required. Saves/cache remain at `/switch/swag-userdata/`. Driving uses the left stick vertically and the right stick horizontally, plus four-way D-pad and triggers. Logging is off unless `/switch/swag-debug.flag` exists.

See [中文说明](README.zh-CN.md), [packaging](packaging/README.md), and [changes](CHANGELOG.zh-CN.md). Repository contains source/tools/icon/verification only; provide game assets and your own local key file when building. Never commit keys, game binaries, raw device logs or packaged NSP/NRO files.
