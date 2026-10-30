# 绝地精英 / SWAG Shooter Switch

Android ARM64 Unity compatibility port for Atmosphère. Game name: 绝地精英. Author: Xiao Fan.

Current release: **0.14.58**, approved for publication by the user on 2026-10-30. [Downloads](https://github.com/zqzhuhai-commits/Swag-Shooter-Switch/releases/tag/v0.14.58) · [Release notes](RELEASE-0.14.58.md) · [Detailed game guide (Chinese)](GAME_GUIDE.zh-CN.md).

Standalone native NSP containing game RomFS and the AI-enhanced icon; no external NRO. Classic squads target 52 actual players in 13 independent teams of four. This release fixes deferred population settings, phantom survivor counts and teammate HUD ownership/life icons, and includes the airplane-stage collection-access fix.

Includes four AI difficulty levels, circle-aware navigation and outside-zone damage fallback, a custom approximately 15-minute six-phase circle schedule, a soft low-health vignette pulsing at 1 Hz, Chinese corrections, persistent settings/items, offline shop/rewards, touch/controller input and automatic driving detection. Initial currencies are 10,000,000 each; existing initialized saves retain later changes. Online/private-server modes remain locked; LAN play is not implemented.

Saves/cache: `/switch/swag-userdata/`; AI configuration: `/switch/swag-ai.cfg`. Logging is off unless `/switch/swag-debug.flag` exists. Back up these data before upgrading. See the guide for exact controls, circle timing and validation scope.

See [中文说明](README.zh-CN.md), [packaging](packaging/README.md) and [changes](CHANGELOG.zh-CN.md). Source history contains code/tools/icon/validation only; game assets, local keys and raw device logs are excluded. Packaged deliverables belong in Release attachments.
