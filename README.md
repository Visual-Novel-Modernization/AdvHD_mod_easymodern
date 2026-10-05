# AdvHD EasyModern

Repository: `AdvHD_mod_easymodern`

Runtime hooks and a transcoding pipeline for WillPlus / RioShiina AdvHD Engine titles. They let an
older engine stream JPEG XL textures and decode AV1 / Opus video. Nothing is written to the registry
and no administrator rights are needed.

---

## Architectural Principles

1. Texture decoding (`jxl_hook.dll`) and video playback (`av1_hook.dll`) are independent hotpatch modules. Neither relies on the other.
2. DirectShow filters (LAV Filters) are instantiated in-process through COM class factories (`DllGetClassObject`), so the pipeline works on a clean Windows installation without `regsvr32`.
3. Both hook DLLs link only to `KERNEL32.dll` and `msvcrt.dll`. The target libraries (`libjxl.dll`, `LAVSplitter.ax`, `LAVVideo.ax`, `LAVAudio.ax`) are resolved at runtime.
4. Untouched assets (raw PNGs, legacy ASF/WMV streams) are identified by magic-number header introspection and passed through to the native engine pipeline unmodified.

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

Legacy textures are PNG files stored inside ARC V2 containers. Target encoding is lossless or
near-lossless JPEG XL (`.jxl`, retaining the `.png` entry name inside the ARC index to prevent script
reference breakage). See [Texture Modding: PNG to JXL](#texture-modding-png-to-jxl) for the tooling.

Whole-installation results:

| Game | Original Size | Post-JXL Size | Net Space Saved | Notes |
| :--- | :---: | :---: | :---: | :--- |
| *Otome no Tsurugi to Himegoto Concerto* | 6.75 GB | **4.19 GB** | **−2.56 GB** | 1,902 PNGs converted; 11 ARC packages rebuilt |
| *Hoshi no Otome to Rokka no Shimai* | 4.66 GB | **2.74 GB** | **−1.92 GB** | 11 ARC packages converted |

ARC package breakdown for *Hoshi no Otome to Rokka no Shimai*:

| Package | Original Size | Post-JXL Size | Net Space Saved | Contents |
| :--- | :---: | :---: | :---: | :--- |
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

When introducing `LAVSplitter`, mapping `L"Output"` strictly to the video pin caused `FindPin` to succeed. AdvHD proceeded down the single-pin path (`pGraph->Render(videoPin)`), leaving the audio pin completely unrendered and disconnected, and playback was silent.

The fix is in `Hook_AddSourceFilter`, which traverses all output pins on `LAVSplitterSource` and pre-renders any pin containing `"Audio"` directly to the DirectShow graph before returning:
```cpp
// Wire the audio pin up before we hand the splitter back, or you get a silent movie
IEnumPins* pEnumPins = NULL;
if (SUCCEEDED(pSplitter->EnumPins(&pEnumPins))) {
    IPin* pPin = NULL;
    while (pEnumPins->Next(1, &pPin, NULL) == S_OK) {
        PIN_INFO pi;
        pPin->QueryPinInfo(&pi);
        if (pi.pFilter) pi.pFilter->Release();
        if (wcsstr(pi.achName, L"Audio") != NULL) {
            HRESULT hrAudio = pThis->Render(pPin); // and that finishes the chain to the default audio renderer
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

## Environment Setup & Toolchain Installation

Everything here builds as 32-bit (x86) to match the AdvHD engine's memory architecture.

### Step 1: Install 32-bit MinGW-w64 GCC Compiler

The build scripts invoke `g++` directly from system `PATH`. A 32-bit toolchain (`i686-w64-mingw32`) is required.

#### Install via MSYS2

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
   [Environment]::SetEnvironmentVariable("PATH", $env:PATH + ";C:\msys64\mingw32\bin", "User")
   ```

If you would rather not install MSYS2, download a standalone 32-bit GCC archive from
https://winlibs.com (for example `winlibs-i686-posix-dwarf-gcc-...zip`), extract it to a local
directory such as `C:\mingw32`, and append that directory to `PATH`:

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

#### Install via WinGet

```cmd
winget install HandBrake.HandBrake.CLI
```

Or download the command-line interface archive from https://handbrake.fr/downloads2.php or GitHub
Releases:

```powershell
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
2. Copy the resulting runtime DLLs from `C:\msys64\mingw32\bin\` into the game root.

All **ten** files below are required. `jxl_hook.dll` itself only links `KERNEL32.dll` and
`msvcrt.dll`, but `libjxl.dll` statically imports the rest of this closure, so a missing member
anywhere in it makes `LoadLibraryA("libjxl.dll")` fail with `err=126` — and the failure is silent
from the game's point of view: the hook still logs `Hook installed`, but not one texture decodes.

| DLL | Imported by | Notes |
| :--- | :--- | :--- |
| `libjxl.dll` | `jxl_hook.dll` (runtime) | codec proper |
| `libjxl_cms.dll` | `libjxl.dll` | colour management |
| `libhwy.dll` | `libjxl.dll`, `libjxl_cms.dll` | SIMD helpers |
| `libbrotlidec.dll` | `libjxl.dll` | Brotli decoder |
| `libbrotlienc.dll` | `libjxl.dll` | Brotli encoder |
| `libbrotlicommon.dll` | brotli dec/enc | shared Brotli |
| `liblcms2-2.dll` | `libjxl_cms.dll` | **easy to miss** |
| `libgcc_s_dw2-1.dll` | `libjxl.dll` and friends | MinGW runtime |
| `libstdc++-6.dll` | `libjxl.dll`, `libhwy.dll` | MinGW runtime |
| `libwinpthread-1.dll` | `libstdc++-6.dll`, `libgcc_s_dw2-1.dll` | MinGW runtime |

The three MinGW runtime DLLs appear because a stock `mingw-w64-i686-libjxl` is not built with
`-static-libgcc -static-libstdc++`. You can drop them and `liblcms2-2.dll` only if you replace
`libjxl.dll`/`libjxl_cms.dll` with fully static builds.

**Do not rely on `PATH` to supply any of these.** `liblcms2-2.dll` in particular is present in
`C:\msys64\mingw32\bin`, so a game launched from an MSYS2 shell will appear to work while the very
same install fails when the user double-clicks the launcher. Ship all ten in the game root.

To confirm a deployment, run `scripts\check_runtime_deps.ps1 -GameDir <game> -RequireLav`. It walks the
import closure from `jxl_hook.dll` / `av1_hook.dll` outwards through the PE import tables (standard and
delay-load) and reports every non-system DLL that does not resolve from the game directory — which is
the failure mode above. A 64-bit PowerShell cannot test this by calling `LoadLibrary` on these 32-bit
DLLs; that returns `err=193` and proves nothing.

---

### Step 5: Fetch cjxl (JPEG XL Encoder)

Needed to turn PNG textures into `.jxl` before repacking them into an ARC. Grab a libjxl release
build and put `cjxl.exe` (and `djxl.exe`, if you want to verify your work) wherever you keep tools:

```powershell
Invoke-WebRequest -Uri "https://github.com/libjxl/libjxl/releases/latest/download/jxl-x64-windows-static.zip" -OutFile "jxl.zip"
Expand-Archive -Path "jxl.zip" -DestinationPath "C:\Tools\jxl" -Force
Remove-Item "jxl.zip"
```

Then point the repacker at it with `-CjxlPath`, or put it on `PATH`.

---

## Texture Modding: PNG to JXL

The texture side of this project is three steps per archive: unpack, convert, repack.

```powershell
# one archive
.\scripts\arc_jxl.ps1 -ArcPath 'E:\Game\Chip6.arc' -CjxlPath 'C:\Tools\jxl\cjxl.exe'

# a whole set, lower quality to save more space, and see what would change first
.\scripts\arc_jxl.ps1 -ArcPath 'E:\Game\Chip*.arc' -CjxlPath 'C:\Tools\jxl\cjxl.exe' -Quality 90 -WhatIfRepack
```

| Parameter | Default | Meaning |
| :--- | :--- | :--- |
| `-ArcPath` | required | One or more `.arc` files. Wildcards work. |
| `-CjxlPath` | `cjxl` on `PATH` | Path to the encoder. |
| `-Quality` | `100` | `cjxl -q`. **100 is lossless**, which is what the published benchmarks used. Lower it to trade fidelity for size. |
| `-Effort` | `7` | `cjxl --effort`, 1-10. Slower means smaller. |
| `-CacheDir` | beside the archive | Scratch space for unpacked members. |
| `-KeepCache` | off | Leave the unpacked files for inspection. |
| `-WhatIfRepack` | off | Do the work, write a `.repacked` file, and leave your archive alone. |

### The one thing to understand

The manifest keeps the **`.png` entry name**, while the payload inside becomes JPEG XL bytes. The
name has to stay because AdvHD looks textures up by name, and `jxl_hook.dll` identifies JXL by its
magic bytes rather than by the file extension. So a converted game archive still contains entries
called `.png`, and they are not PNGs. That is intentional, not a bug.

### The ARC V2 container

```
[0x00] UInt32  fileCount
[0x04] UInt32  manifestSize (bytes)
[0x08] manifest: fileCount x { UInt32 length; UInt32 offset; UTF-16LE name + 0x0000 }
[0x08+manifestSize] data area; member i lives at dataStart+offset, length bytes
trailing bytes after the last member = checksum/footer, preserved verbatim
```

`scripts/advhd_arc_v2.ps1` is the library that reads and writes this layout, and `arc_jxl.ps1` drives
it. Repacking keeps the manifest byte-for-byte apart from the `length` and `offset` fields, never
reorders members, and carries the trailer across.

Two failure modes worth knowing about, because both silently corrupt an archive:

* **Reordering.** `Get-ChildItem` returns entries alphabetically, not in archive order. A repacker
  built on it shuffles the data area while the manifest still points at the old offsets.
* **Dropping the trailer.** The last member ends before the end of the file. Whatever follows it
  (checksum or footer) has to be copied explicitly or it disappears.

The round trip is verified: repacking an archive with **no** replacements reproduces it byte for
byte, trailer included.

---

## Build Instructions

Once prerequisites are configured and available in `PATH`, run `scripts\build_all.bat` from the repository root:
```cmd
cd AdvHD_mod_easymodern
call scripts\build_all.bat
```

Each target can also be built on its own with `scripts\build_jxl.bat`, `scripts\build_av1.bat`, or `scripts\build_launcher.bat`.

Outputs:
* `jxl_hook/jxl_hook.dll` (Target: 32-bit PE DLL)
* `av1_hook/av1_hook.dll` (Target: 32-bit PE DLL)
* `launcher/AdvHD_EasyModern.exe` (Target: 32-bit PE Executable)

---

## Deployment & Game Packaging

### Target Executable Selection
`AdvHD_EasyModern.exe` resolves the target game binary via a 3-tier precedence cascade:
1. **Command-line arguments**:
   Specify the target binary directly via CLI flags:
   ```cmd
   AdvHD_EasyModern.exe AdvHD_unpacked.exe
   AdvHD_EasyModern.exe -t AdvHD_unpacked.exe
   AdvHD_EasyModern.exe --target AdvHD_unpacked.exe
   ```
2. **Configuration File Override (`launcher.ini`)**:
   For packaging distributions where users launch via double-click without a command prompt, place a `launcher.ini` beside the launcher:
   ```ini
   [Launcher]
   Target=AdvHD_unpacked.exe
   ```
   *(Alternatively, a single-line text file containing the `.exe` filename is also recognized).*
3. **Wildcard detection**:
   Failing both of the above, the launcher takes the first `AdvHD*.exe` sitting next to it, skipping
   itself.

   If your game executable is named something else, change `TARGET_PATTERN` near the top of
   `launcher/launcher.cpp` and rebuild, or add a `launcher.ini` pinning the target.

### Hook Discovery and Fallback
* Probes for `jxl_hook.dll` and `av1_hook.dll` in its directory before injecting.
* If either hook DLL is missing, the launcher logs it and continues without error, so texture modding alone, video modding alone, or the stock engine all work.

### Placing the Files
Place `AdvHD_EasyModern.exe`, `jxl_hook.dll` and `av1_hook.dll` alongside the game executable. The libjxl runtime DLLs from Step 4 go in the same folder, the LAV Filters from Step 3 go in a `lav\` subfolder, and the transcoded `OP.dat` and `ED_01.dat` ~ `ED_05.dat` replace the original movie files. Launch the game by running `AdvHD_EasyModern.exe`.

---

## License

MIT. See [LICENSE](LICENSE). Copyright (c) 2026 AdvHD EasyModern contributors.

Nothing links at build time, so MIT puts no copyleft obligation on the GPL-licensed LAV Filters and
HandBrakeCLI you fetch yourself.

---

## Disclaimer

This project is written and published from Canada and is meant to be used outside China. That said,
nothing here stops you using it in China. If you do, including to play visual novels or galgame, that
is your own call and your own risk. The project and its contributors accept no responsibility or
liability for what follows. Canadian permissions do not apply in China, and this project is not
written for Chinese law.

It contains no game content, no third-party binaries and no cracked or pre-patched executables, and
it is free. Nobody is authorized to charge you for it.

If the project conflicts with the laws, religion or customs where you are, stop using it and remove
it.

See [DISCLAIMER.md](DISCLAIMER.md) for the full text.
