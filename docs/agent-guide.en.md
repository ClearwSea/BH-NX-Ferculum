# Agent guide: NS building, integration, and gameplay validation

[中文](agent-guide.md) · [English](agent-guide.en.md)

**Document version date: 2026/11/10. This guide may lag behind the actual project.** Before acting, carefully compare it with the current repository, selected commit, release notes, configuration examples, and actual file layout—especially supported platforms, required inputs, tool paths, command arguments, and save compatibility. When they differ, follow the verified project state and promptly tell the user which instructions are outdated and how you will adjust. Explicitly identify anything you cannot confirm. Do not treat this guide as a permanently valid command list. If you cannot check online, explain that your conclusions are limited to the local version.

Assume you already have this repository address and are reading it, regardless of vendor or Agent environment. Do not ask again for a known address. Carry out authorized work according to the user’s goal; explain specific missing materials or access and the next step. Check current source, usage documentation, and the selected release rather than presenting unpublished development records as supported features.

This guide does not pin project release versions. Select from current official releases and compatibility requirements, recording actual versions. Game input versions, dependency versions, and example paths are not project releases to download; verify them separately before acting.

## How the three projects work together

| Repository | Guide |
| --- | --- |
| BH2017-LOCRENASCI | [Android building and playing](https://github.com/ClearwSea/BH2017-LOCRENASCI/blob/main/docs/agent-guide.en.md) |
| BH-LOCRENASCI-ACT-EDIT | [Save creation, editing, and restoration](https://github.com/ClearwSea/BH-LOCRENASCI-ACT-EDIT/blob/main/docs/agent-guide.en.md) |
| BH-NX-Ferculum | [NS building, integration, and validation](agent-guide.en.md) |

Follow the guide for the current goal; installing all three repositories is unnecessary. Use the editor when a save is needed, then return to the target platform’s game guide. Do not request usable materials again. Carry forward the user’s platform, versions, file paths, goal, and authorization rather than restarting the questions. Cross-project guides do not replace checks of actual version and interface compatibility.

## Before starting: communication and network checks

### Explain progress in language the user understands

Assess familiarity from the user's questions and feedback without labeling them as having “low technical ability.” If they are unfamiliar with building, signing, or file paths, **at every step** briefly explain what you are doing, why, what currently blocks progress, what they need to do next, and any precautions relevant to that step. If nothing is blocked or no user action is needed, say so. Do not invent problems or overwhelm them with a complete technical manual.

Ask necessary questions before reaching a step that depends on a user decision or missing material, rather than after it fails. Start with their device and available materials. Before downloading, explain file size and destination. Before installation, establish whether this is a new install or an update and whether the save is backed up. Before generating a save, explain starter and max-level experience templates. Ask only a few questions needed for the current and immediately following step. Reuse answers and authorization already given; inspect what you can yourself first. While waiting, continue work that does not depend on the answer.

For example: “I'll check download speed first because the build tools are large, so we don't discover a connection problem halfway through. You don't need to do anything yet. Next, please tell me how much free disk space you have, or let me check it directly.” If the network is blocked, explain the impact and alternatives rather than just displaying an error or telling the user to solve it themselves.

### Test the actual download environment; do not assume a reliable proxy

Assume the user's Agent **does not have a reliable network proxy**. Before downloading large files, use available tools for a quick check with time and traffic limits. Being able to read the repository does not prove that large release assets download reliably. An Agent's cloud connection also does not represent the user's computer connection.

1. In the environment that will actually download the files, test the repository/release page or GitHub API. Record response status and elapsed time. A connection timeout of about 10 seconds and a total request limit of about 20 seconds are reasonable starting points. Retry only a limited number of times.
2. Obtain a real asset URL from actual release metadata and follow redirects to test downloading. Prefer a small sample of about 1–4 MiB, with an approximately 20-second total timeout and a hard byte limit. Record bytes received, average speed, and interruptions. Stop at the limit even if the server ignores Range; do not turn a speed test into a full download. A HEAD request alone does not measure download speed.
3. Estimate download duration using the actual asset size. Repeated timeouts, interrupted transfers, or sustained speeds below roughly 200 KiB/s for a large package are examples of poor quality, not rigid pass/fail thresholds. Distinguish authentication errors, API rate limits, and expired or invalid links from network problems.
4. If you cannot make network requests, say that you could not test. Give the user short executable instructions or commands and explain what results to return. Do not fabricate a successful check.

If the connection is poor, **explicitly tell the user: “The current environment has a poor connection to GitHub,”** supported by measured timeouts, speed, or estimated waiting time. This can occur on some networks in mainland China, but location alone is not evidence. Also explain which downloads remain necessary and what preparation can continue.

Then proactively devise and try a workable way to obtain the release. Do not merely tell the user to “find a proxy.” Choose based on your tools and their environment: official direct asset links, a downloader with resume support, an existing user-approved proxy or alternate network, downloading through the browser and handing the file to the Agent, or downloading in another authorized environment and transferring it back. When user participation is necessary, explain the approach, why it can help, and the specific step they need to take.

This guide does not prescribe a permanently valid mirror or proxy address. If a third-party relay is necessary, verify availability and disclose the source first. Do not send it GitHub credentials, signing keys, or saves, and do not disable TLS certificate validation. Check downloaded files against official SHA-256 values when provided. If an official checksum is temporarily unavailable, mark verification as pending rather than claiming the file is trusted. Proxy installation, system setting changes, and paid services require the applicable user authorization; download problems do not authorize unrelated environment changes.

## 1. Explain the actual delivery scope

This repository is a Nintendo Switch compatibility layer and integration reference for a specific Android ARMv7 client. It includes runtime support, patches, input/audio/video bridges, Python hosting, and packaging tools. It is **not a one-click installer containing a complete game and backend**. Establish whether the user wants to study source, build the compatibility layer, or ultimately play on NS.

Read the [README](../README_ENG.md), [AGENTS.md](../AGENTS.md), [build instructions](build.md), [developer guide](developer-guide.md), [integration instructions](integration.md), and [dependency lockfile](../manifests/dependencies.lock.json). Inspect actual release assets; do not assume a complete NSP, offline tool suite, or ready-to-run game is supplied.

For a player, explain plainly: “This repository supplies adaptation code for running the game on Switch. Matching game files, resources, and a real server backend are still needed. I'll inspect what you have and identify missing items. Compiling only the compatibility layer or demonstration backend does not make the game playable.” Missing backend integration is development work; do not promise extract-and-play or hide it as ordinary installation.

## 2. Check the environment and request materials

Inspect existing files, then ask progressively about computer OS and permitted build directory, Linux/WSL/container environment and free space; NS model and current system/runtime environment; existing working installation or first deployment; available SD-card access; progress to preserve. The README's tested environment is a reference, not universal compatibility.

Building requires Docker capable of `linux/amd64` and ARM32 cross dependencies. Explicitly map Windows, WSL, and container paths rather than copying another computer's paths. The device must already support execution of the project's output. If not, identify missing conditions rather than treating device-environment modification as ordinary deployment. Deployment and save writes follow user authorization.

**Explicitly request these external materials and provide concrete folders or accessible delivery methods:**

| Material | Required checks |
| --- | --- |
| Matching 1.9 Android ARMv7 APK, native libraries, and resources | Version labels alone are insufficient; check the target `libil2cpp.so` SHA-256 in the README, patch preimages, and client configuration |
| Game resource payload | Must match client read paths and network/resource bridges; renaming an arbitrary APK does not make it compatible |
| Real Python game backend and runtime data | Must implement the `nx_backend.configure` integration; the HTTP example is not a game backend |
| Compatible `account.json` | New users may generate one with the companion editor; existing users supply an actual gameplay save and back it up first |
| Font | A font supporting required startup text, with its license, for generating the glyph header |
| Full NSP packaging materials, if selected | Control files, icon, matching tools, and the user's existing external key file; never commit or display keys |

If the user cannot locate clients/resources, provide the [Android README reference section](https://github.com/ClearwSea/BH2017-LOCRENASCI#客户端参考信息) and proactively show the Bilibili links:

| Reference material | Video |
| --- | --- |
| 1.9 client | [BV1UU4u6yE8R](https://www.bilibili.com/video/BV1UU4u6yE8R/) |
| 2.4 reference client (demo) | [BV1zJ411N7Qz](https://www.bilibili.com/video/BV1zJ411N7Qz/) |
| 7.x PC Windows resources (7.0 reference) | [BV1xCpPezEeM](https://www.bilibili.com/video/BV1xCpPezEeM/) |

Check the current README for updated entries. Explain these are reference videos, not verified direct-download links. Ask the user to obtain clients/resources following their instructions, not download the videos themselves. Library hashes still need verification; do not promise that any file from these references works directly on NS.

**The 1.9 client is this repository's game target; 2.4 and 7.x are not required to compile the minimal example.** Request 2.4, 7.x, and supplements only when choosing the Android project's data/resource generation route and its requirements call for them; follow the [Android build guide](https://github.com/ClearwSea/BH2017-LOCRENASCI/blob/main/docs/agent-guide.en.md). Do not demand all inputs again when matching resources and a backend already exist.

Give beginners concrete destinations and accept existing paths, uploads, accessible shares, or download links. The Agent inspects archives, extracts required files, and locates resources. Explain what can proceed when network access or materials are missing; do not make the user guess formats. Estimate space from actual clients, resources, dependencies, container images, and outputs. This repository gives no universal full-build space guarantee; do not automatically apply Android's recommendation.

## 3. Prepare the locked toolchain and minimal example

Apply network checks to the actual environment accessing GitHub, GHCR, and other dependency sources; success with one does not imply all work. Read versions, commits, and container digest from the [lockfile](../manifests/dependencies.lock.json), with Python/NumPy archives in the [dependency list](../poc/python32_probe/dependencies.json). Do not replace locked dependencies with a remembered “latest” version.

Follow the [developer guide](developer-guide.md) for this layout, replacing placeholders with confirmed paths:

```text
<build-root>/
  BH-NX-Ferculum/
  libnx32/prefix/
  mesa32/prefix/
  ffmpeg32-src/prefix/
  python32-probe/
```

Build libnx32 and Mesa at locked commits with the matching toolchain. Fetch the container image beforehand; the main script uses `--pull=never` and does not fetch missing images. Having the image does not mean ARM32 dependency prefixes have been built.

Once dependencies are ready, run the minimal example in the appropriate cross-compilation environment. First set `BH3_BUILD_ROOT` to the actual build root:

```sh
LIBNX32="$BH3_BUILD_ROOT/libnx32/prefix" \
PORTLIBS="$BH3_BUILD_ROOT/mesa32/prefix" \
RUNTIME_DIR="$BH3_BUILD_ROOT/BH-NX-Ferculum/third_party/android32" \
sh "$BH3_BUILD_ROOT/BH-NX-Ferculum/examples/minimal-app/build.sh"
```

This example loads no game or server; it validates AArch32 and graphics. Deploy according to the actual output and the user's existing execution method, checking the color display and logs. Without device access, mark “Example built; device validation pending.” Do not claim game playability at this stage.

## 4. Prepare runtime dependencies and a real backend

Build ARM32 FFmpeg, CPython, and NumPy following the developer guide. FFmpeg needs the target ARM softfp ABI, four-byte enums, and required decoders; host libraries are not substitutes. Prepare the Python standard library, NumPy Python modules, and business dependencies using the documented scripts. Verify generated files such as `numpy_inittab.h` and `objects.rsp` exist before main-program compilation.

Prepare a font and generate `boot_ui_font.h` from the repository root using the [font instructions](developer-guide.md#启动文字字库). Check Pillow first; explain and obtain applicable authorization for dependency installation. The user supplies the font. Preserve its license and do not commit generated glyph data to public source.

```sh
python3 tools/generate_boot_ui_font.py --font /actual/path/to/font.ttf
```

The font path above is an example; substitute the actual location before execution.

Implement `nx_backend.configure(runtime, data)` according to the [hosting contract](../references/bh3-1.9/python-server-host/README.md), returning the game-session handler, HTTP handler, and count of initialized/verified files. Check `_nxipc` socket methods, data initialization, persistence, and ready-notification ordering.

`nx_backend.example.py` demonstrates HTTP and returns 501; it does not implement the game protocol. Renaming it does not complete integration. You may study the [Android server source](https://github.com/ClearwSea/BH2017-LOCRENASCI), but verify the selected commit/data combination and adapt entry points such as `server19_portable.configure`, `server19.GameHandler`, and `server19_http.make_handler`. Android's TCP/HTTP-listening `start` flow does not replace NS in-memory-channel hosting. If no backend exists, explain the development scope and establish the user's intent to undertake that work.

The default standalone layout is illustrated below; actual paths follow `port_config.h`, path bridges, and backend configuration:

```text
/switch/bh3_2017/
  game.apk
  libmain.so / libulua.so / libunity.so / libil2cpp.so
  libAkSoundEngine.so / libAVProLocal.so
  python/stdlib/
  python/runtime/
  server-stage3/nx_boot_profile.py
  server-stage3/nx_server.py
  server-stage3/nx_backend.py
  server-stage3/data/
```

Check template path remnants carefully: at this guide's date, `PYTHON_ROOT` in `nx_boot_profile.py` points to a probe directory rather than the standalone Python directory above. Align actual paths instead of blindly copying constants. Also check returned client addresses/ports against bridge configuration.

## 5. Resource manifest and main program

Arrange resources, backend code, and necessary initialization data in an external `<romfs>/payload/`. Identify S-class files initialized only when absent. If an initial `account.json` is included, treat it as a seed; do not package the user's active save as an ordinary overwritable resource.

From the repository root, use this command template after substituting all placeholders:

```sh
python3 tools/resources/prepare_manifest.py --romfs <romfs> --header <new-header> --seed server-stage3/data/account.json
```

Omit `--seed` if no initial save is included. `manifest.tsv`, `package.id`, and the new header must not already exist; put the header outside payload. Use the generated header as `poc/m3_boot/source/package_identity.h` before compiling. Regenerate all three after payload changes rather than retaining an old resource identity.

Once dependencies, font header, backend, and resource identity are ready, run from the repository root:

```sh
bash tools/build/release-original.sh
```

`BH3_BUILD_ROOT` must already be set. Check logs and outputs according to the actual script. At this guide's date outputs are under `release-reference/`, and `release.nsp` is an **ExeFS container, not a complete installable game package**. Compilation alone does not validate backend or resource integration.

## 6. Full packaging and deployment

For a complete installer, follow the [developer guide](developer-guide.md#完整安装包与卡带入口) to prepare `digital-local.nsp`, `control.nacp` and icon under `digital-package/control/`, `hacBrewPack`, `hactool`, and external keys. Keys come from the user's existing authorized environment; do not find or publish them on their behalf. Tool sources are locked; required packaging Python version follows current documentation.

The Agent fills this command template after inspecting the script and actual files:

```sh
python3 tools/packaging/build_standalone_nsp.py --root <package-root> --keys <external-keys> --title-id <matching-title-id> --prepared-romfs <romfs>
```

The main-program NPDM and package title ID must match, and control-file names/identity must satisfy script checks. The current build script has a fixed program ID. If changing it, update related configuration too; changing only the packaging argument is insufficient. Study cartridge-entry tools only if needed, rather than making them mandatory for all users.

Verify packaging results, output sizes, and SHA-256 values, and record source commit, dependencies, and resource identity. Before deployment, confirm target device, actual SD paths, and original-save backup. Use the user's existing authorized installation/transfer method. Never install an ExeFS container as though it were a complete NSP or delete all resources after a single installation failure.

## 7. Saves, device validation, and rollback

Use the [save-editor guide](https://github.com/ClearwSea/BH-LOCRENASCI-ACT-EDIT/blob/main/docs/agent-guide.en.md) for starter or experience saves. At this guide's date the full NS starter “First login” opening flow is unverified; leave it off unless current documentation explicitly updates that conclusion.

Before editing/replacing a save, fully close the game with HOME → X. The default save is the SD file `sd:/switch/bh3_2017/server-stage3/data/account.json`, not a system save slot. Use available DBI/MTP, FTP, or direct SD access; copy to the computer and back up independently before editing and restoring to the same path. Custom backend paths and save formats require separate confirmation.

Validate progressively: startup/resource identity, backend ready, bridge entry, controls and audio/video, ordinary stage entry/exit/settlement, and persistence after restarting. Diagnose the current failing stage rather than treating a color example, HTTP demo, or offline package check as complete gameplay acceptance.

On failure, retain runtime logs, screen details, build identity, and reproduction steps. Check ABI/library fingerprints, paths, standard library/business modules, backend interfaces, manifests, and save compatibility first. Roll back to retained program/resource combinations and restore matching saves only after fully exiting. Do not promise arbitrary old/new save compatibility. Obtain consent before sharing real saves/logs and exclude personal data and keys.

## 8. Final handoff

List completed stages and missing integration work, commit/configuration, artifacts/checksums, deployment procedure, actual data paths, backup/rollback locations, tested devices, and unverified scope. If only source or compatibility-layer compilation is complete, say exactly that. When the goal is playing, continue authorized dependency and integration work; if blocked, identify the specific missing materials or decisions.
