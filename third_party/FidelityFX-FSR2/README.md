# AMD FidelityFX Super Resolution 2 (FSR 2.2)

- Project: [FidelityFX-FSR2](https://github.com/GPUOpen-Effects/FidelityFX-FSR2).
- Pinned revision: tag `v2.2.1`, commit `1680d1edd5c034f88ebbbb793d8b88f8842cf804`.
- License: [MIT, original text](LICENSE).

This directory keeps the license text in the tree, following the same convention as the other entries in `third_party/`. The SDK itself is **not** vendored (it is large, and Exolune does not currently vendor third-party sources).

**Obtain and build the DX12 API** (once per clone) with:

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_fsr2.ps1
```

The script clones the tagged revision into `third_party/FidelityFX-FSR2/sdk/` (gitignored) and builds the Release static libraries. After that, a Release|x64 build of `MyPlanetEngine` defines `MYPLANETENGINE_HAS_FSR2` and links `ffx_fsr2_api_x64.lib` and `ffx_fsr2_api_dx12_x64.lib`.

Without the SDK, the engine still builds and runs. The default path is unchanged: `MYPLANETENGINE_UPSCALER` defaults to off, and a request for FSR is ignored until the libraries are present and the project is rebuilt.

The wrapper is `MyPlanetEngine/Upscaler.cpp`. It talks to the official FSR 2.2 DX12 API; the engine code around it (render size, jitter, motion vectors) belongs to MyPlanetEngine.
