[中文](README.md) · [English](README_ENG.md)

> This README was generated automatically by an Agent; please refer to the actual project state as authoritative.

# BH-NX-Ferculum

> **This project is a VIBECODING product.** Development relied heavily on AI assistance, and the code may contain omissions. Please bear with any issues; fixes and improvements from developers are welcome.

A Nintendo Switch compatibility layer and integration reference for the Honkai Impact 3rd 1.9 Android ARMv7 client.

The project builds on open-source porting work such as android32, libnx32, and Mesa. It provides compatibility fixes for the specified client, special patches, input and audio/video bridging, a way to host the Python server, and tools for deploying and packaging resources. Users supply a matching client, game resources, and server, then configure their own runtime combination through the integration interface.

You can study and modify the source directly, hand the repository to an Agent for collaborative development, or use the build scripts to produce a runnable program. NSP packaging and cartridge-entry tools are optional. Development entry points: [developer guide](docs/developer-guide.md) and [development conventions](AGENTS.md).

Linked documents in this repository are still in Chinese.

## What this repository provides

| Item | Purpose |
| --- | --- |
| Platform runtime and compatibility layer | Android AArch32 loading, system interfaces, and platform adaptation |
| Patches for the specified client | Unity / IL2CPP startup, input, audio/video, and resource-read adaptation |
| Server hosting and integration templates | Embedded Python, an in-memory communication channel, and backend interfaces |
| Build and deployment tools | Dependency build scripts, resource install and verification, NSP packaging, and cartridge entry |
| Minimal example | An AArch32 graphics program that does not load the game |

The client, native game libraries, game resources, server business source and data, saves, and keys are supplied separately by the user. The startup text font is generated from a local font. Steps: [build notes](docs/developer-guide.md#启动文字字库).

## Version and target

Based on the **1 November 2026** version. It does not represent the latest development progress, and it does not guarantee later features or fixes. New features or support for other versions can be developed on this base.

Target: Honkai Impact 3rd 1.9 / Unity 5.3.6p5 / IL2CPP / Android `armeabi-v7a`. SHA-256 of the `libil2cpp.so` that the dedicated patches match:

```text
3c47c6774dcceee5d54b4cdcc6c2d4eb3f0da87cbbf8d0b0ee08a5729023b867
```

Patches are bound to a specific binary layout. Other builds with the same version number need their addresses and instruction signatures checked again. The server must implement the hosting interface. Runtime conditions: [features and integration requirements](docs/status.md).

### Tested devices

The following devices were actually tested by the maintainer and are listed for reference; unlisted environments are not guaranteed to behave the same.

| Device | System environment |
| --- | --- |
| Nintendo Switch V1 (HAC001) | System 21.2.0, AMS 1.10.2, E |

## Usage flow

1. Prepare the pinned toolchain, ARM32 dependencies, and working directories as described in the [build notes](docs/build.md).
2. Build the [minimal example](docs/developer-guide.md#最小示例) and confirm the AArch32 and graphics environment on device.
3. Prepare the client, resources, server, and server data outside the repository.
4. Configure the client path and the `nx_backend` entry as described in the [integration notes](docs/integration.md).
5. Generate the resource manifest and in-program resource identity, build the runnable program, and test it on device.
6. Package an NSP or generate a cartridge entry according to how you use it.

## Server integration

The Python runtime is embedded in the main program. It accepts game connections and HTTP requests through an in-memory byte channel, without creating a BSD listening port. The repository provides the hosting mechanism and interface templates. Server business logic is implemented externally.

`nx_backend` initializes runtime data and provides the HTTP handler and the game-session handler. The sample backend only demonstrates the HTTP interface and does not implement the game protocol. Contract: [Python server hosting reference](references/bh3-1.9/python-server-host/README.md).

Servers in other languages need their own runtime, communication, and lifecycle management. Where to attach an external Python backend: [integration notes](docs/integration.md#外部python服务端).

## Layout

```text
poc/m3_boot/             Compatibility and startup source for the specified target
third_party/android32/  Runtime source and build support
references/bh3-1.9/     Server hosting and integration templates
poc/python32_probe/     Python / NumPy cross-compile materials
tools/                  Build, resource verification, and packaging tools
examples/minimal-app/   Graphics example with no game content
manifests/              File origins, dependency versions, and file manifests
docs/                   Build, integration, and feature notes
```

## License and acknowledgements

New code added by this project is under the [MIT License](LICENSE), copyright **ClearwSea**. Third-party code keeps its own licenses and original author notices. Main sources and scope: [third-party sources and licenses](THIRD_PARTY_NOTICES.md). This project's license does not grant any extra rights to the game client, resources, trademarks, or external servers.

The runtime base comes from aks796's android32, libnx32, mesa32, and dcr_sea_nx, Liartes's spacehulk-nx, and TheOfficialFloW, fgsfds, xerpi, switchbrew, devkitPro, Mesa, and related upstream developers. Python, NumPy, FFmpeg, and build/packaging tools are also used.

Thanks to **ChatGPT, DeepSeek, Claude, and Gemini** for help with development, problem analysis, and code iteration.
