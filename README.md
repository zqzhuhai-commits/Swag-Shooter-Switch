# Swag Shooter Switch

Experimental Android ARM64 Unity compatibility port for Atmosphère.

Current test source: **0.14.23, input hardware validation pending.** The user confirmed that 0.14.22 displays the opening animation and reaches the menu after the EGL window-size query fix. Version 0.14.23 adds single-pointer touchscreen injection and basic menu navigation (D-pad/left stick, A confirm, B back). Multitouch and full combat controls remain incomplete; this is not yet a fully playable port. Startup uses an offline policy with immediate DNS failure.

See [中文说明](README.zh-CN.md) and [changes and validation history](CHANGELOG.zh-CN.md).

Game binaries and commercial assets are not included. Build with devkitPro; provide game assets locally in romfs/game. The eventual target is a single NSP; current test artifacts are NROs.
