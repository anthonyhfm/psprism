![psprism Header](header.png)

# psprism 💎

> Static recompilation engine for PlayStation Portable executables.  
> Translates Allegrex MIPS binaries into clean, portable C++20 code.

---

## 🚀 Overview

**psprism** converts PlayStation Portable executables (ISOs, ELFs, or PRXs) into readable, self-contained C++20 projects.

Rather than interpreting instructions at runtime like a traditional emulator, `psprism` statically recompiles MIPS Allegrex machine code, VFPU vector operations, relocations, and imports ahead of time. The resulting C++ codebase can be compiled natively for any target platform equipped with a standard C++ toolchain—enabling native ports to modern PCs, macOS, Linux, or consoles such as the Nintendo Switch or Wii U.

---

## 🔄 Verification: PSP ➔ C++ ➔ PSP

Before targeting native host operating systems, generated code is validated directly on original PSP hardware:

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
 │ (Real PSP)  │ │ Translation │
 └─────────────┘ └─────────────┘
```

Recompiling generated C++ code back into a PSP executable (`EBOOT.PBP`) confirms CPU instruction translation, VFPU math accuracy, branch delays, and memory behavior directly on the reference architecture before introducing host subsystem translation layers.

---

## 🎮 Current Status & Compatibility

Pipeline validation has been performed on **Daxter** (*Ready at Dawn*) and **Tetris**:

| Title | Hardware Validation (`PSP ➔ C++ ➔ PSP`) | Host Translation Layer |
|---|:---:|:---:|
| **Daxter** *(Ready at Dawn)* | ✅ **Fully Playable** (100% completion verified) | 🟡 Start Menu |
| **Tetris** | ✅ **Fully Playable** (100% completion verified) | 🟡 Start Menu |

* 🕹️ **Hardware Roundtrip:** Recompiled back to PSP binaries via PSPSDK, both games execute fully from start to finish on real hardware with complete graphics, audio, logic, and VFPU operations intact.
* 🖥️ **Host Translation Layer:** Running via the host runtime layer on desktop, both games initialize and reach the start menu.

---

## 🧩 Subsystem & SCE Module Support

For native execution on host platforms, `psprism` implements native host bridges for Sony `sce` kernel and firmware modules:

| Subsystem / Module | Description | Status | Implementation Details |
|---|---|:---:|---|
| `sceDisplay` | Display & VBlank | ✅ | Framebuffer presentation, refresh timing, frame counters |
| `sceCtrl` | Controller & Input | ✅ | Native gamepads, analog stick input, keyboard fallback |
| `sceGe` | Graphics Engine | 🟡 | Geometry pipelines, textures, CLUT, depth, blending, color tests |
| `sceKernel` | Threading & Sync | 🟡 | Threads, guest stacks, semaphores, event flags, memory pools |
| `sceIo` | Filesystem & I/O | 🟡 | Disc ISO reading, virtual MemoryStick paths, sync/async operations |
| `sceUtility` | OS Utilities | 🟡 | On-screen keyboard (OSK), message dialogs, savedata UI |
| `sceUmd` | UMD Drive | 🟡 | Disc state detection, drive events, media checks |
| `sceRtc` | Real-Time Clock | 🟡 | Microsecond tick conversion, system timers |
| `scePower` / `sceImpose` | System State | 🟡 | Battery state, volume settings, regional configuration stubs |
| `sceAudio` | Audio Subsystem | ❌ | Multi-channel audio output pipeline in development |
| `sceMpeg` | Video Decoder | ❌ | Hardware PMF video stream decoding planned |

---

## 🛠️ Quickstart

### 1. Build psprism
```bash
git clone https://github.com/anthonyhfm/psp-recomp.git psprism
cd psprism

cmake -S . -B build
cmake --build build -j
```

### 2. Recompile a PSP Executable
```bash
./build/psprecomp game.iso
```
The interactive wizard parses executable relocations, extracts disc assets, and generates a standalone C++ CMake project.

### 3. Build the Generated Project
```bash
cd game_recompiled

# Build for PSP Hardware / PPSSPP
make psp

# Build for Native Desktop Host
make native
```

---

## ⚡ Key Features

* 🧬 **Allegrex MIPS & VFPU Lifting:** Translates integer, FPU, and vector operations into standard C++20.
* 📦 **Self-Contained Output:** Generated C++ projects build independently without requiring `psprism` source trees.
* 🗺️ **Ghidra Map Integration:** Imports function symbol boundaries and names for readable code structure.
* 🎯 **Hardware Roundtrip Verification:** Guarantees translation accuracy through real hardware testing.
