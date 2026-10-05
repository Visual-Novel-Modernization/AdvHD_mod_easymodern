# AdvHD Modernization Suite: Decoupled JXL & AV1 Plugins

Engine-level runtime hooks and asset transcoding pipeline for WillPlus / RioShiina **AdvHD Engine** titles.
Enables high-efficiency **JPEG XL (JXL)** texture streaming and **AV1 / Opus** in-engine video decoding without requiring system registry modifications, elevated privileges, or static third-party binary dependencies.

---

## Architectural Principles

1. **Strict Modular Decoupling**: Texture decoding (`jxl_hook.dll`) and video playback (`av1_hook.dll`) operate as independent hotpatch modules. Neither relies on the other.
2. **Registration-Free DirectShow Pipeline**: In-process instantiation of DirectShow filters (`LAV Filters`) via COM Class Factories (`DllGetClassObject`). Works seamlessly on clean Windows installations without running `regsvr32`.
3. **Zero Static Third-Party Dependencies**: Both hook DLLs link exclusively to `KERNEL32.dll` and `msvcrt.dll`. Target libraries (`libjxl.dll`, `LAVSplitter.ax`, `LAVVideo.ax`, `LAVAudio.ax`) are dynamically resolved at runtime.
4. **Transparent Binary Pass-Through**: Magic-number header introspection ensures untouched assets (raw PNGs, legacy ASF/WMV streams) bypass modification routines and flow into native engine pipelines untouched.

---

## Performance & Storage Audit

### 1. AV1 Video Transcoding Metrics (SVT-AV1)

Tested on *Otome no Tsurugi to Himegoto Concerto* (AdvHD 1.9.9.10).
Legacy encoding: WMV3 (Main Profile @ ~8000 kbps, 1280x720, 29.97 fps) + WMA9 Pro (44.1 kHz, 128 kbps).
Target encoding: HandBrakeCLI `svt_av1` (CRF 26) + Opus (128 kbps, 48 kHz).

| Stream | Original WMV Size | AV1 Transcoded Size | Net Savings | Compression Ratio | Playback Status |
| :--- | :---: | :---: | :---: | :---: | :---: |
| `OP.dat` / `OP.cmv` | 96.37 MB | **32.72 MB** | −63.65 MB | 33.9% (2.95×) | **PASS** |
| `ED_01.dat` / `.cmv` | 125.92 MB | **33.46 MB** | −92.46 MB | 26.6% (3.76×) | **PASS** |
| `ED_02.dat` / `.cmv` | 125.82 MB | **32.71 MB** | −93.10 MB | 26.0% (3.85×) | **PASS** |
| `ED_03.dat` / `.cmv` | 125.82 MB | **31.86 MB** | −93.96 MB | 25.3% (3.95×) | **PASS** |
| `ED_04.dat` / `.cmv` | 126.03 MB | **31.84 MB** | −94.19 MB | 25.3% (3.96×) | **PASS** |
| `ED_05.dat` / `.cmv` | 125.06 MB | **30.51 MB** | −94.55 MB | 24.4% (4.10×) | **PASS** |
| **Total** | **725.02 MB** | **193.10 MB** | **−531.92 MB** | **−73.4% (3.75×)** | **100% PASS** |

### 2. JPEG XL Texture Optimization Metrics

Evaluated across production benchmarks (*Otome no Tsurugi to Himegoto Concerto* & *Hoshi no Otome to Rokka no Shimai*).
Legacy textures: 32-bit ARGB PNG stored inside ARC V2 containers.
Target encoding: Lossless / near-lossless JPEG XL (`.jxl`, retaining `.png` entry name inside ARC index to prevent script reference breakage).

| Game / Target | Original Size | Post-JXL Mod Size | Net Space Saved | Notes |
| :--- | :---: | :---: | :---: | :--- |
| *Otome no Tsurugi* (Unpacked) | 6.75 GB | **4.19 GB** | **−2.56 GB (−71.3%)** | 1,902 PNGs converted; 11 ARC packages rebuilt |
| *Hoshi no Otome* (Enigma Encrypted) | 4.66 GB | **2.74 GB** | **−1.92 GB (−58.8%)** | 11 ARC packages converted |
| `Chip2.arc` | 15.3 MB | **9.16 MB** | −6.14 MB | CG background layers |
| `Chip3.arc` | 154.1 MB | **35.60 MB** | −118.50 MB | Full-screen event illustrations |
| `Chip6.arc` | 221.9 MB | **139.00 MB** | −82.90 MB | Cut-in sprite layers |
| `Chip7.arc` | 156.8 MB | **128.70 MB** | −28.10 MB | Composite UI & character assets |

---

## Reverse Engineering & Implementation Details

### Video Pipeline (`av1_hook`)

#### 1. AdvHD DirectShow Topology
The engine delegates playback initialization to internal subroutine `sub_4CC910`:
```
Engine: sub_497D40 (CMovieLayer instantiation)
  └── sub_4CC910 (DirectShow Graph Construction)
        ├── CoCreateInstance(&CLSID_FilterGraph, ..., &IID_IGraphBuilder)
        ├── Enum Audio Devices -> AddFilter(pAudioRenderer, L"AudioRenderer")
        ├── new CMovieTexture (AdvHD Custom CBaseVideoRenderer) -> AddFilter(..., L"TextureRenderer")
        ├── IGraphBuilder::AddSourceFilter(path, L"Source", &pSource)
        └── Connect / Render pin -> IMediaControl::Run()
```

#### 2. The Audio Pin Isolation Deadlock & Fix
In unmodded binaries, legacy ASF streams (`WM ASF Reader`) expose pins named `"Raw Video 1"` and `"Raw Audio 1"`. Consequently, AdvHD's internal lookup:
```c
hr = pSourceFilter->FindPin(L"Output", &pPinOut);
```
always fails on original files (`hr < 0`). When it fails, AdvHD falls back to `IGraphBuilder::RenderFile(path, 0)`, which renders both audio and video streams simultaneously.

When introducing `LAVSplitter`, mapping `L"Output"` strictly to the video pin caused `FindPin` to succeed. AdvHD proceeded down the single-pin path (`pGraph->Render(videoPin)`), leaving the audio pin completely unrendered and disconnected—resulting in silent video playback.

**Fix**: `Hook_AddSourceFilter` automatically traverses all output pins on `LAVSplitterSource` and pre-renders any pin containing `"Audio"` directly to the DirectShow graph before returning:
```cpp
// Pre-render Audio Pin to ensure audio graph completion
IEnumPins* pEnumPins = NULL;
if (SUCCEEDED(pSplitter->EnumPins(&pEnumPins))) {
    IPin* pPin = NULL;
    while (pEnumPins->Next(1, &pPin, NULL) == S_OK) {
        PIN_INFO pi;
        pPin->QueryPinInfo(&pi);
        if (pi.pFilter) pi.pFilter->Release();
        if (wcsstr(pi.achName, L"Audio") != NULL) {
            HRESULT hrAudio = pThis->Render(pPin); // Completes audio chain to Default AudioRenderer
        }
        pPin->Release();
    }
    pEnumPins->Release();
}
```

#### 3. Color Space Constraints
AdvHD's custom `CMovieTexture::CheckMediaType` (`sub_4CC550`) strictly rejects all media subtypes except **`MEDIASUBTYPE_RGB32`** (`FORMAT_VideoInfo`). In-memory `LAVVideo.ax` negotiations natively fulfill this contract without intermediate DMO conversions.

---

### Texture Pipeline (`jxl_hook`)

#### 1. Interception Point
Hooks `d3dx9_43.dll!D3DXCreateTextureFromFileInMemoryEx` via a 5-byte relative `jmp` trampoline:
```
Engine ARC decompression -> Memory Buffer -> D3DXCreateTextureFromFileInMemoryEx -> VRAM
                                                   ▲
                                             [jxl_hook.dll]
```

#### 2. Format Discrimination & Pool Fallback
Data headers are checked for container (`00 00 00 0C 4A 58 4C 20`) or raw stream (`FF 0A`) magic bytes. Non-JXL buffers are passed through the trampoline to original DirectX code.

To prevent Direct3D 9Ex `D3DERR_INVALIDCALL` (`0x8876086C`) exceptions across differing driver implementations, texture surface creation executes a 4×3 matrix search over:
- `D3DPOOL`: `ReqPool`, `D3DPOOL_MANAGED`, `D3DPOOL_DEFAULT`, `D3DPOOL_SYSTEMMEM`
- `D3DUSAGE`: `ReqUsage`, `0`, `D3DUSAGE_DYNAMIC`
- Direct pixel pitch alignment via `pTex->LockRect()` / `UnlockRect()`.

---

## Repository Layout

```
AdvHD_Mod_Plugins/
├── README.md
├── LICENSE                     # MIT
├── DISCLAIMER.md               # Legal disclaimer & compliance notice
├── jxl_hook/
│   └── jxl_hook.cpp            # Pure JXL hook implementation
├── av1_hook/
│   └── av1_hook.cpp            # Pure DirectShow AV1 registration-free hook
├── launcher/
│   └── launcher.cpp            # Multi-hook 32-bit remote thread injector
└── scripts/
    ├── build_all.bat           # Compiles all targets using MinGW32
    ├── build_jxl.bat
    ├── build_av1.bat
    ├── build_launcher.bat
    └── encode_av1.ps1          # HandBrakeCLI SVT-AV1 automated batch transcoder
```

---

## Environment Setup & Toolchain Installation

All mod modules build as 32-bit (x86) binaries to match the native AdvHD engine memory architecture. Follow these step-by-step instructions to prepare the compiler, transcoding tools, and runtime filters on a clean Windows workstation.

*(Note: Do not run these setup commands inside production deployment targets; execute them on your development/packaging environment.)*

### Step 1: Install 32-bit MinGW-w64 GCC Compiler

The build scripts invoke `g++` directly from system `PATH`. A 32-bit toolchain (`i686-w64-mingw32`) is required.

#### Method A: Via MSYS2 (Recommended)
1. Install MSYS2 from https://www.msys2.org or via Windows Package Manager:
   ```cmd
   winget install MSYS2.MSYS2
   ```
2. Open the MSYS2 MINGW32 shell and install the 32-bit compiler and DirectShow headers:
   ```bash
   pacman -Syu --noconfirm
   pacman -S --noconfirm mingw-w64-i686-gcc mingw-w64-i686-headers
   ```
3. Add the MinGW32 binary directory to your system `PATH`:
   ```powershell
   # In PowerShell (User environment variable)
   [Environment]::SetEnvironmentVariable("PATH", $env:PATH + ";C:\msys64\mingw32\bin", "User")
   ```

#### Method B: Via WinLibs Standalone Toolchain
1. Download the standalone 32-bit GCC archive from https://winlibs.com (e.g., `winlibs-i686-posix-dwarf-gcc-...zip`).
2. Extract to a local directory (e.g. `C:\mingw32`).
3. Append to system `PATH`:
   ```cmd
   setx PATH "%PATH%;C:\mingw32\bin"
   ```

Verify compiler configuration:
```cmd
g++ --version
where g++
```

---

### Step 2: Install HandBrakeCLI (SVT-AV1 Transcoder)

Required for running the automated batch transcoding script (`scripts/encode_av1.ps1`).

#### Method A: Via WinGet (Recommended)
```cmd
winget install HandBrake.HandBrake.CLI
```

#### Method B: Official Release Archive
1. Download the command-line interface archive from https://handbrake.fr/downloads2.php or GitHub Releases:
   ```powershell
   # In PowerShell: download and extract to tooling directory
   Invoke-WebRequest -Uri "https://github.com/HandBrake/HandBrake/releases/download/1.11.2/HandBrakeCLI-1.11.2-win-x86_64.zip" -OutFile "HandBrakeCLI.zip"
   Expand-Archive -Path "HandBrakeCLI.zip" -DestinationPath "C:\Tools\HandBrakeCLI" -Force
   Remove-Item "HandBrakeCLI.zip"
   setx PATH "%PATH%;C:\Tools\HandBrakeCLI"
   ```

Verify encoder presence:
```cmd
HandBrakeCLI --version
HandBrakeCLI --help | findstr "svt_av1"
```

---

### Step 3: Fetch Registration-Free LAV Filters (32-bit x86)

The AV1 DirectShow hook requires the 32-bit portable binaries of LAV Filters.

1. Download the 32-bit portable ZIP from GitHub:
   ```powershell
   # In PowerShell
   $url = "https://github.com/Nevcairiel/LAVFilters/releases/download/0.83/LAVFilters-0.83-x86.zip"
   Invoke-WebRequest -Uri $url -OutFile "LAVFilters-x86.zip"
   Expand-Archive -Path "LAVFilters-x86.zip" -DestinationPath ".\lav" -Force
   Remove-Item "LAVFilters-x86.zip"
   ```
2. Verify that `./lav/` contains the required DirectShow binaries and Side-by-Side manifest:
   * `LAVSplitter.ax`
   * `LAVVideo.ax`
   * `LAVAudio.ax`
   * `avcodec-lav-63.dll`
   * `avformat-lav-63.dll`
   * `avutil-lav-61.dll`
   * `swscale-lav-10.dll`
   * `swresample-lav-7.dll`
   * `avfilter-lav-12.dll`
   * `libbluray.dll`
   * `LAVFilters.Dependencies.manifest`

---

### Step 4: Fetch 32-bit libjxl Runtime Libraries

If utilizing `jxl_hook.dll` for texture streaming, the 32-bit shared runtime libraries must accompany the hook DLL in the game root:
1. In MSYS2:
   ```bash
   pacman -S --noconfirm mingw-w64-i686-libjxl
   ```
2. Copy the resulting runtime DLLs from `C:\msys64\mingw32\bin\` into the game root:
   * `libjxl.dll`
   * `libjxl_cms.dll`
   * `libbrotlicommon.dll`
   * `libbrotlidec.dll`
   * `libhwy.dll`

---

## Build Instructions

Once prerequisites are configured and available in `PATH`, run `scripts\build_all.bat` from the repository root:
```cmd
cd AdvHD_Mod_Plugins
call scripts\build_all.bat
```

Each target can also be built on its own with `scripts\build_jxl.bat`, `scripts\build_av1.bat`, or `scripts\build_launcher.bat`. The scripts resolve their own location via `%~dp0`, so they may be invoked from any working directory. Build outputs are written next to their sources (shown below).

Outputs:
* `jxl_hook/jxl_hook.dll` (Target: 32-bit PE DLL)
* `av1_hook/av1_hook.dll` (Target: 32-bit PE DLL)
* `launcher/advhd_mod_launcher.exe` (Target: 32-bit PE Executable)

---

## Deployment & Game Packaging

### Target Executable Selection
`advhd_mod_launcher.exe` resolves the target game binary via a 3-tier precedence cascade:
1. **Command-Line Arguments (Highest Priority)**:
   Specify the target binary directly via CLI flags:
   ```cmd
   advhd_mod_launcher.exe AdvHD_unpacked.exe
   advhd_mod_launcher.exe -t AdvHD_unpacked.exe
   advhd_mod_launcher.exe --target AdvHD_unpacked.exe
   ```
2. **Configuration File Override (`launcher.ini`)**:
   For packaging distributions where users launch via double-click without a command prompt, place a `launcher.ini` beside the launcher:
   ```ini
   [Launcher]
   Target=AdvHD_unpacked.exe
   ```
   *(Alternatively, a single-line text file containing the `.exe` filename is also recognized).*
3. **Automatic Detection Cascade (Default Fallback)**:
   If neither CLI parameters nor `launcher.ini` are provided, the launcher automatically scans for standard binaries in sequence:
   `AdvHD_CN.exe` -> `AdvHD_CHS.exe` -> `AdvHD_crack.exe` -> `AdvHD.exe`.

### Dynamic Hook Discovery & Graceful Fallback
* Probes for `jxl_hook.dll` and `av1_hook.dll` in its directory prior to process injection.
* If either hook DLL is missing, the launcher logs the condition and gracefully continues execution without error—enabling flexible deployment (texture modding alone, video modding alone, or stock engine fallback).

### Layout Structure
Place `advhd_mod_launcher.exe` and desired hook modules alongside the game executable:

```
<GameRoot>/
├── advhd_mod_launcher.exe   # Universal multi-hook launcher (graceful fallback)
├── jxl_hook.dll             # Optional: JPEG XL texture streaming hook
├── av1_hook.dll             # Optional: AV1 DirectShow video playback hook
├── libjxl.dll               # Required if jxl_hook.dll is used
├── libjxl_cms.dll
├── libbrotlidec.dll
├── libbrotlicommon.dll
├── libhwy.dll
├── lav/                     # Required if av1_hook.dll is used (in-process filters)
│   ├── LAVSplitter.ax
│   ├── LAVVideo.ax
│   ├── LAVAudio.ax
│   ├── avcodec-lav-63.dll
│   ├── avformat-lav-63.dll
│   ├── avutil-lav-61.dll
│   ├── swscale-lav-10.dll
│   ├── swresample-lav-7.dll
│   ├── avfilter-lav-12.dll
│   ├── libbluray.dll
│   └── LAVFilters.Dependencies.manifest
├── OP.dat                   (Transcoded AV1 file, retaining .dat extension)
└── ED_01.dat ~ ED_05.dat    (Transcoded AV1 files)
```

Launch the game by executing `advhd_mod_launcher.exe`.

---

## License

Released under the [MIT License](LICENSE). Copyright (c) 2026 AdvHD Modernization Suite contributors.

MIT was chosen deliberately: this repository contains only original interoperability source code, is
not linked against any third-party library at build time (every external symbol is resolved at
runtime via `LoadLibrary`/`GetProcAddress`), and imposes no copyleft obligation on the separate
components you obtain yourself. MIT is permissive, OSI-approved, and GPL-compatible, so it keeps the
project usable alongside the GPL-licensed LAV Filters and HandBrakeCLI that it interoperates with.

---

## Disclaimer

This project is developed and published from **Canada** in compliance with applicable Canadian law,
and it redistributes no game content, no third-party binaries, and no cracked or pre-patched
executables.

Users located in **mainland China** should read the jurisdiction-specific notice before use: the
interoperability permissions described in the disclaimer are features of Canadian law and do not
automatically apply under the laws of the People's Republic of China. You are responsible for your
own compliance.

See [`DISCLAIMER.md`](DISCLAIMER.md) for the full legal disclaimer, compliance notice, mainland
China guidance, third-party component licensing, and warranty limitations.
