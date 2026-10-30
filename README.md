# 绝地精英 / SWAG Shooter Switch

Android ARM64 Unity compatibility port for Atmosphère. Game name: 绝地精英. Author: Xiao Fan.

Current release: **0.14.38**, confirmed working on hardware by the user on 2026-10-30. See [release notes](RELEASE-0.14.38.md) and [downloads](https://github.com/zqzhuhai-commits/Swag-Shooter-Switch/releases/tag/v0.14.38).

Standalone native NSP with full game RomFS and icon; no external NRO. Includes Mesosphere startup fix, offline shop and local item grants, direct rewarded-ad callbacks, product-price label repair, Chinese corrections, touch/controller input, automatic driving detection and 10,000,000 starting cash/gold/diamonds per launch. AI enhancements are not included in this release.

Saves/cache: `/switch/swag-userdata/`. Driving: left stick vertically, right stick horizontally; D-pad and triggers also supported. Logging is off unless `/switch/swag-debug.flag` exists.

See [中文说明](README.zh-CN.md), [packaging](packaging/README.md) and [changes](CHANGELOG.zh-CN.md). Source history contains code/tools/icon/validation only; game assets, local keys and raw device logs are excluded. Packaged deliverables belong in Release attachments.
