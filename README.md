# 绝地精英 / SWAG Shooter Switch

Android ARM64 Unity compatibility port for Atmosphère. Game name: 绝地精英. Author: Xiao Fan.

Current release: **0.14.34-fix1** (in-game/package version remains **0.14.34**). Fixes standalone NSP startup failing with `0x00010801` while creating a local HIPC session. The user confirmed entering the game after applying the functional fix. The release NSP embeds that same fix; see [release notes](RELEASE-0.14.34-fix1.md) for the exact artifact and validation scope.

The NSP contains the native program, full game RomFS and icon; no external NRO is required. Saves/cache remain at `/switch/swag-userdata/`. Driving uses the left stick vertically and the right stick horizontally, plus four-way D-pad and triggers. Logging is off unless `/switch/swag-debug.flag` exists.

See [中文说明](README.zh-CN.md), [packaging](packaging/README.md), and [changes](CHANGELOG.zh-CN.md). Repository history contains source/tools/icon/verification only; provide game assets and your own local key file when building. Never commit keys, game binaries, raw device logs or packaged NSP/NRO files. Packaged deliverables, when published, are Release attachments rather than Git source files.
