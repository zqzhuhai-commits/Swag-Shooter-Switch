# Swag Shooter Switch

Experimental Android ARM64 Unity compatibility port for Atmosphère.

Current test source: **0.14.10, not yet hardware validated; not playable yet.** Unity constructors, JNI_OnLoad, initJni and the first nativeRender entry have run on hardware. Version 0.14.9 passes the mixed-permission and Unity virtual-memory sequence checks, but eager heap backing for large virtual reservations exhausts memory and crashes the engine. Version 0.14.10 introduces lazy backing for anonymous PROT_NONE reservations and a six-reservation stress test; these changes await hardware validation.

See [中文说明](README.zh-CN.md) and [changes and validation history](CHANGELOG.zh-CN.md).

Game binaries and commercial assets are not included. Build with devkitPro; provide game assets locally in romfs/game. The eventual target is a single NSP; current test artifacts are NROs.
