![psprism Header](header.png)

# psprism 💎

> Static recompilation engine for PlayStation Portable executables.  
> Translates Allegrex MIPS binaries into clean, portable C++20 code.

---

## 🚀 Overview

**psprism** converts PlayStation Portable executables (ISOs, ELFs, or PRXs) into readable C++20 projects. Exported repositories can use a bring-your-own-game workflow: original code and assets remain untracked and are recreated locally from each user's legally obtained dump.

Rather than interpreting instructions at runtime like a traditional emulator, `psprism` statically recompiles MIPS Allegrex machine code, VFPU vector operations, relocations, and imports ahead of time. The resulting C++20 project can run through the native `refract` compatibility layer or be rebuilt as a hybrid PSP executable for validation on PPSSPP and real hardware.

---

## 🔄 Verification & Diagnostics: PSP ➔ C++ ➔ PSP

To test and verify instruction lifting, relocations, and ABI semantics early in development, `psprism` includes an automated PSP back-compilation harness:

```text
       PSP ISO / ELF / PRX
                │
                ▼
      ┌──────────────────┐
      │     psprism      │
      └──────────────────┘
                │
                ▼
       Portable C++20 Code
                │
        ┌───────┴───────┐
        ▼               ▼
 ┌─────────────┐ ┌─────────────┐
 │ PSPSDK Build│ │ Host Runtime│
 │ (Testbench) │ │ Translation │
 └─────────────┘ └─────────────┘
```

The PSP backend can compile translated C++ functions back into a PRX using the PSPSDK. However, **this is designed as a verification testbench and patching harness, not a production target for full-speed gameplay**:

* **CPU State Indirection Overhead:** The generated C++ passes a software `psprecomp::State` structure around in memory rather than allocating Guest variables directly into physical MIPS registers. On the PSP's 333 MHz MIPS Allegrex CPU, repeatedly reading and writing register state from RAM and cache creates severe CPU bottlenecks and heavy framerate drops.
* **Memory Constraints (32 MB RAM):** Translating entire games into individual C++ translation units leads to significant binary bloat. Full-game recompilation frequently exhausts the PSP's tight user memory pool, causing complex titles to crash or fail to boot entirely.
* **Hybrid Overlay Model:** For targeted modding and bugfixing, a code-map `overlay` mode allows compiling only selected functions to C++ while leaving unmodified functions native. Calls cross through ABI-, GP-, FPU-, and VFPU-safe trampolines. While this drastically reduces binary size, trampoline transitions still add latency when invoked in hot code paths.

An automated PSP roundtrip test exercises this pipeline by lifting a fixture binary, editing a generated function, rebuilding the PRX, and observing the modified output in PPSSPP. Full-speed execution is intended for native host platforms (`refract` currently supports macOS and Windows, with more host backends planned).

---

## 🎮 Current Status & Compatibility

### Daxter (2006) running in PSPRISM with refract compared to PPSSPP:

<table width="100%">
  <thead>
    <tr>
      <th width="50%" align="center">PPSSPP Reference</th>
      <th width="50%" align="center">Native macOS (<code>psprism</code> + <code>refract</code>)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="center"><img src="dax_ppsspp.png" alt="Daxter on PPSSPP" width="100%"></td>
      <td align="center"><img src="dax_psprism.png" alt="Daxter on psprism refract engine" width="100%"></td>
    </tr>
  </tbody>
</table>

---

## 🗺️ Supported Platforms & Roadmap

`psprism` translates PSP executables ahead of time into standard C++20. By combining translated game code with the modular `refract` runtime layer, projects can target modern desktop operating systems as well as resource-constrained homebrew consoles where traditional PSP emulators cannot achieve full speed.

| Platform / Target | Architecture | Graphics API | Audio / Host Subsystem | Status | Implementation Details & Roadmap |
|---|---|---|---|:---:|---|
| **macOS** | ARM64 / x86_64 | Metal | CoreAudio / AudioToolbox | 🟢 **Working** | Reference native host. Full 3D rendering with Metal shaders, native gamepads, FFmpeg cutscene decoding, and Qt desktop dialogs. Daxter is fully playable (other titles WIP). |
| **PSP Hardware / PPSSPP** | MIPS Allegrex | Native GE | Native MediaEngine / `sceAudio` | 🟡 **Diagnostics & Overlays** | Automated testbench and surgical function overlay patching. Full-game C++ recompilation on original hardware suffers from severe CPU state indirection overhead (`State` struct) and 32MB RAM limits, causing massive framerate drops or boot failures. |
| **Windows** | x86_64 | Direct3D 11 | Win32 / XAudio2 | 🟢 **Working** | Native Win32 window and message loop, D3D11 GE rendering, XInput controllers, keyboard fallback, XAudio2 output, and MSVC/CMake builds. |
| **Linux** | x86_64 / ARM64 | Vulkan | PulseAudio / PipeWire / SDL2 | 🟡 **Planned** | Native Vulkan graphics pipeline and Linux event loop. Targeted for Desktop Linux and Steam Deck / SteamOS. |
| **Nintendo Switch** | ARM64 (Cortex-A57) | deko3d / Vulkan | libnx / SDL2 | ⏳ **Roadmap (Homebrew)** | Future homebrew target via devkitA64 and libnx. AOT recompilation avoids the CPU virtualization/JIT overhead of emulators. |
| **Nintendo Wii U** | PowerPC (Espresso) | GX2 | Mocha / WUT | ⏳ **Roadmap (Homebrew)** | Planned homebrew target via devkitPPC and WUT, mapping the PSP GE pipeline to Nintendo GX2. |
| **Nintendo 3DS** | ARM11 | PICA200 / Citro3D | libctru / CSND | ⏳ **Roadmap (Homebrew)** | Long-term homebrew exploration. Eliminates the CPU emulation bottleneck that prevents emulators like PPSSPP from running on 3DS hardware. |
| **Nintendo Wii** | PowerPC (Broadway) | GX | libogc / ASND | ⏳ **Roadmap (Homebrew)** | Long-term homebrew exploration via devkitPPC / libogc and Nintendo GX graphics. |

---

## 🧩 Subsystem & SCE Module Support

For native execution on host platforms, `psprism` implements native host bridges for Sony `sce` kernel and firmware modules:

| Subsystem / Module | Description | Status | Implementation Details |
|---|---|:---:|---|
| `sceDisplay` | Display & VBlank | 🟢 | Framebuffer presentation, double/triple buffering, VBlank interrupts, frame rate timing, and frame counters. |
| `sceCtrl` | Controller & Input | 🟢 | Apple GameController and Windows XInput integration, analog stick deadzones, and keyboard fallback mapping. |
| `sceGe` | Graphics Engine | 🟢 | Native Metal and Direct3D 11 rendering pipelines. Scheduled display lists, stall address updates, full vertex decoder (3D transformed & 2D through mode), offscreen render targets, depth/stencil buffers, alpha blending, color doubling, and hash-based texture caching with 4/8/16/32-bit CLUT palettes. |
| `sceKernel` | Threading, Sync & Memory | 🟢 | Cooperative and preemptive guest threading model, priority scheduling, Mutexes, Semaphores, Event Flags, and FIFO/priority Mailboxes (`Mbx`). Memory partition management, Fixed Pools (`Fpl`), Variable Pools (`Vpl`), microsecond alarms, and software interrupts. |
| `sceIo` | Filesystem & Storage | 🟢 | ISO 9660 disc streaming, case-insensitive path normalization, virtual MemoryStick (`ms0:/`) mapping, raw LBN sector reads (`disc0:/sce_lbn*`), synchronous and non-blocking asynchronous I/O (`OpenAsync`, `ReadAsync`, `PollAsync`, `WaitAsync`), and FAT `devctl` capacity queries. |
| `sceAudio` / `sceSas` / `sceAtrac` / `sceMp3` | Audio Subsystem | 🟢 | Low-latency multi-channel host mixer with queued blocking output (`AudioOutput2`). Complete 32-voice `sceSasCore` software synthesizer with ADSR envelopes, pitch modulation, noise, and VAG/PCM mixing. Streamed ATRAC3/ATRAC3+ decoding via standalone decoder with loop handling; basic MP3 stream decoding. |
| `sceUtility` | OS Dialogs & Savedata | 🟢 | Interactive Qt-based desktop UI frontend for Savedata operations (multi-slot carousel, icons, timestamps, save/load/delete flow), On-Screen Keyboard (OSK), and system message dialogs. Host system parameter queries and module loader bridges. |
| `sceMpeg` | Video / Cutscenes | 🟡 | PSMF/PES demuxing, ringbuffer streaming (`sceMpegRingbufferPut`), and FFmpeg-backed H.264 (AVC) video decode directly into PSP framebuffer formats, paired with demuxed ATRAC3+ audio. Seeking and certain title-specific edge cases remain WIP. |
| `sceUmd` | UMD Drive Emulation | 🟢 | Disc medium detection, drive state transitions, asynchronous drive wait conditions, and UMD event callbacks. |
| `sceRtc` | Real-Time Clock | 🟢 | Microsecond tick conversion (`SysClock2USec`), accumulative runtime tracking, and UTC/local clock conversions. |
| `scePower` / `sceImpose` | Power & System State | 🟢 | Battery level, charging state, CPU/bus clock frequency reporting (333/166 MHz), language mode configuration, and power callbacks. |
| `sceDmac` | Direct Memory Access | 🟢 | Hardware DMA memory copy emulation (`sceDmacMemcpy`). |
| `sceNet` / `sceWlan` | Networking & WLAN | 🟡 | 70+ stubs for BSD sockets (`sceNetInet*`), Ad-hoc matching/connections, access point queries (`sceNetApctl*`), and WLAN switch detection. Allows games to safely bypass network initialization; multiplayer networking is not yet implemented. |
| `sceUsb` | USB Subsystem | ⚪ | Compatibility stubs (`sceUsbStart`, `sceUsbActivate`) returning success for games querying USB peripheral interfaces. |

---

## 🛠️ Quickstart

### 1. Build psprism
```bash
git clone https://github.com/anthonyhfm/psprism.git
cd psprism

make -j
make test
```

### 2. Recompile a PSP Executable
```bash
./build/bin/psprism game.iso
```
The interactive wizard parses executable relocations, extracts disc assets, and generates a standalone C++ project.

Game functions can be replaced directly from recovered Ghidra prototypes,
original translated functions can be called safely, and reverse-engineered
globals can be read or overwritten through the generated project's patch
framework. See [Game-function patching](docs/game-patching.md).

### 3. Build the Generated Project
```bash
cd game_recompiled

# In a clean public clone, supply your own matching dump first.
cp /path/to/game.iso original/disc.iso

# Build for PSP Hardware / PPSSPP
make psp

# Build or run the native macOS app
make macos
make macos-run

# Or, from a Visual Studio developer shell on Windows (x86-64)
cmake -S . -B build/windows -A x64
cmake --build build/windows --config Release
```

The first build runs `psprism hydrate`: it verifies the disc ID and SHA-256,
then generates the translated sources, disc assets, platform glue and matching
`refract` runtime locally. Those files and the original dump are ignored by
Git. Later builds use a fast local hydration cache. Select a toolchain with
`PSPRISM=/path/to/psprism`; a checkout at `toolchain/psprism` is also
supported.

Native PMF cutscenes use the system FFmpeg libraries (`avcodec`, `avformat`,
`avutil` and `swscale`). On macOS, install them before configuring a generated
project:

```bash
brew install ffmpeg
```

On Windows, provide native libraries matching the selected CMake architecture
through a package-manager toolchain or set `REFRACT_FFMPEG_ROOT` to an FFmpeg
prefix containing `include`, `lib` and (for shared builds) `bin`. Qt 6.5 or
newer is required for the in-window savedata, message and keyboard dialogs.
`windeployqt` and FFmpeg DLLs are copied into the executable directory after a
successful build.

Set `-DREFRACT_USE_FFMPEG=OFF` only for a build that intentionally omits native
H.264 cutscene decoding.

---

## ⚡ Key Features

* 🧬 **Allegrex MIPS & VFPU Lifting:** Translates integer, FPU, and vector operations into standard C++20.
* 📦 **Bring-Your-Own-Game Output:** Public project repositories contain metadata and patches; verified code and assets are hydrated locally from the user's own dump.
* 🗺️ **Ghidra Map Integration:** Imports function symbol boundaries and names for readable code structure.
* 🧷 **Hybrid PSP Overlays:** Keeps unchanged Allegrex functions native while compiling edited generated functions back into the PSP build.
* 🎯 **Hardware Roundtrip Verification:** Exercises generated changes through automated PSP tests and real-hardware gameplay validation.

---

## 🤖 AI Acknowledgment & Transparency

A significant portion of the `psprism` codebase and translation runtime was built with the assistance of AI technology, developed under close human guidance, architectural review, and testing.

This project uses AI as a force multiplier for software preservation—accelerating complex MIPS/VFPU instruction lifting, subsystem stubbing, and host translation layers that would otherwise require thousands of hours of manual labor. Every component is audited and verified against real PSP hardware to ensure correctness, stability, and open-source longevity.

---

## 📜 License and game-content policy

psprism and Refract are licensed under
[GNU GPL version 3 or later](LICENSE). Bundled third-party components retain
their own compatible licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The license covers only material the project has the right to license. It does
not cover PSP games, translated game code, disc images, decrypted executables,
assets, screenshots, logos, or trademarks. Exported repositories use a
bring-your-own-game workflow and must not publish hydrated or compiled game
output. See [LICENSING.md](LICENSING.md) for the exact boundary and publication
guidance.

psprism is unofficial and is not affiliated with or endorsed by Sony
Interactive Entertainment, Ready at Dawn, or any game publisher or
rightsholder.
