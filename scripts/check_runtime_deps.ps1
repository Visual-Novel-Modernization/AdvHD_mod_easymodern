#requires -Version 5
<#
.SYNOPSIS
    Verify that a deployed AdvHD EasyModern install has a complete DLL closure.

.DESCRIPTION
    Walks the static import closure of the hook DLLs from the game directory outwards and
    reports any non-system DLL that is not present next to them.

    This exists because the failure mode is silent and looks like something else entirely.
    libjxl.dll statically imports libjxl_cms.dll, which statically imports liblcms2-2.dll.
    Ship the "9 runtime DLLs" and forget liblcms2-2.dll, and LoadLibraryA("libjxl.dll")
    returns NULL with err=126. jxl_hook.dll then behaves exactly as documented for a clean
    install: it logs "Hook installed", it does not crash, and not one texture ever decodes -
    the game just draws nothing where the JXL textures should be. The usual reason this goes
    unnoticed during development is that liblcms2-2.dll happens to live in an MSYS2
    mingw32\bin directory that is on the developer's PATH, so the install only breaks for
    the end user who double-clicks the launcher.

    A 64-bit PowerShell cannot validate this by calling LoadLibrary on the 32-bit DLLs
    (that returns err=193 and proves nothing), so the closure is computed from the PE
    import tables instead. The README's own build is 32-bit x86.

.PARAMETER GameDir
    Folder holding AdvHD_EasyModern.exe, the hook DLLs and the runtime DLLs.

.PARAMETER RootDll
    DLLs whose closure should be checked. Defaults to the two hooks.

.PARAMETER RequireLav
    Also require the registration-free LAV Filters under <GameDir>\lav.

.EXAMPLE
    .\check_runtime_deps.ps1 -GameDir 'E:\Games\MyGame' -RequireLav
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDir,
    [string[]]$RootDll = @('jxl_hook.dll', 'av1_hook.dll'),
    [switch]$RequireLav
)

$ErrorActionPreference = 'Stop'

# Everything Windows itself provides, plus the CRT redistributables that ship with the OS.
$systemPrefixes = @('api-ms-win-', 'ext-ms-win-')
$systemNames = @(
    'kernel32.dll', 'user32.dll', 'gdi32.dll', 'advapi32.dll', 'shell32.dll', 'shlwapi.dll',
    'ole32.dll', 'oleaut32.dll', 'comctl32.dll', 'comdlg32.dll', 'winmm.dll', 'imm32.dll',
    'version.dll', 'ws2_32.dll', 'crypt32.dll', 'dwmapi.dll', 'wininet.dll', 'setupapi.dll',
    'rpcrt4.dll', 'secur32.dll', 'iphlpapi.dll', 'dhcpcsvc.dll', 'urlmon.dll', 'winhttp.dll',
    'psapi.dll', 'userenv.dll', 'usp10.dll', 'wtsapi32.dll', 'oleacc.dll', 'd3d9.dll',
    'd3d11.dll', 'dxgi.dll', 'msvcrt.dll', 'msvcp140.dll', 'vcruntime140.dll', 'vcomp140.dll',
    'ucrtbase.dll', 'ntdll.dll', 'd3dx9_43.dll'
)

function Get-PeImports {
    param([Parameter(Mandatory)][string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40) { return @() }
    $peOff = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOff -le 0 -or ($peOff + 0x18) -ge $bytes.Length) { return @() }
    if ([Text.Encoding]::ASCII.GetString($bytes, $peOff, 4) -ne "PE`0`0") { return @() }

    $is64 = ([BitConverter]::ToUInt16($bytes, $peOff + 24) -eq 0x20B)
    $numSections = [BitConverter]::ToUInt16($bytes, $peOff + 6)
    $optSize = [BitConverter]::ToUInt16($bytes, $peOff + 20)
    $ddOff = $peOff + 24 + $(if ($is64) { 112 } else { 96 })
    $secOff = $peOff + 24 + $optSize

    $sections = @()
    for ($i = 0; $i -lt $numSections; $i++) {
        $o = $secOff + 40 * $i
        $sections += [pscustomobject]@{
            VSize  = [BitConverter]::ToUInt32($bytes, $o + 8)
            VAddr  = [BitConverter]::ToUInt32($bytes, $o + 12)
            RawSize = [BitConverter]::ToUInt32($bytes, $o + 16)
            RawPtr = [BitConverter]::ToUInt32($bytes, $o + 20)
        }
    }
    $script:curBytes = $bytes
    $script:curSections = $sections

    function Convert-Rva([uint32]$rva) {
        foreach ($s in $script:curSections) {
            if ($rva -ge $s.VAddr -and $rva -lt ($s.VAddr + [Math]::Max($s.VSize, $s.RawSize))) {
                return [int]($s.RawPtr + ($rva - $s.VAddr))
            }
        }
        return -1
    }
    function Read-CStr([int]$off) {
        $sb = New-Object Text.StringBuilder
        while ($off -lt $script:curBytes.Length -and $script:curBytes[$off] -ne 0) {
            [void]$sb.Append([char]$script:curBytes[$off]); $off++
        }
        return $sb.ToString()
    }

    $names = New-Object System.Collections.Generic.List[string]
    # data directory 1 = import table, 13 = delay-load import table
    foreach ($dirIndex in 1, 13) {
        $rva = [BitConverter]::ToUInt32($bytes, $ddOff + 8 * $dirIndex)
        if ($rva -eq 0) { continue }
        $entrySize = if ($dirIndex -eq 1) { 20 } else { 32 }
        $io = Convert-Rva $rva
        while ($io -gt 0 -and ($io + $entrySize) -le $bytes.Length) {
            $nameRva = if ($dirIndex -eq 1) { [BitConverter]::ToUInt32($bytes, $io + 12) }
            else { [BitConverter]::ToUInt32($bytes, $io + 4) }
            if ($nameRva -eq 0) { break }
            $no = Convert-Rva $nameRva
            if ($no -lt 0) { break }
            $dll = Read-CStr $no
            if ($dll) { $names.Add($dll) }
            $io += $entrySize
        }
    }
    return ($names | Sort-Object -Unique)
}

function Test-SystemDll([string]$name) {
    $lower = $name.ToLowerInvariant()
    if ($systemNames -contains $lower) { return $true }
    foreach ($p in $systemPrefixes) { if ($lower.StartsWith($p)) { return $true } }
    return $false
}

if (-not (Test-Path -LiteralPath $GameDir)) { throw "GameDir not found: $GameDir" }
$GameDir = (Get-Item -LiteralPath $GameDir).FullName

Write-Host "Checking DLL closure in: $GameDir"
Write-Host ''

$seen = @{}
$missing = New-Object System.Collections.Generic.List[string]
$found = New-Object System.Collections.Generic.List[string]
$queue = New-Object System.Collections.Generic.Queue[string]

foreach ($r in $RootDll) {
    if (Test-Path -LiteralPath (Join-Path $GameDir $r)) { $queue.Enqueue($r) }
    else { $missing.Add("$r (root hook not deployed)") }
}

# jxl_hook.dll only statically imports KERNEL32/msvcrt - it reaches libjxl.dll through
# LoadLibraryA at runtime - so the static walk cannot discover libjxl.dll on its own.
# Seed it explicitly, otherwise the whole point of this check is skipped.
if (Test-Path -LiteralPath (Join-Path $GameDir 'jxl_hook.dll')) {
    if (Test-Path -LiteralPath (Join-Path $GameDir 'libjxl.dll')) { $queue.Enqueue('libjxl.dll') }
    else { $missing.Add('libjxl.dll   (loaded at runtime by jxl_hook.dll)') }
}

while ($queue.Count -gt 0) {
    $dll = $queue.Dequeue()
    $key = $dll.ToLowerInvariant()
    if ($seen.ContainsKey($key)) { continue }
    $seen[$key] = $true

    $path = Join-Path $GameDir $dll
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $found.Add($dll)
    try {
        foreach ($dep in (Get-PeImports -Path $path)) {
            $depKey = $dep.ToLowerInvariant()
            if ($seen.ContainsKey($depKey)) { continue }
            if (Test-SystemDll $dep) { $seen[$depKey] = $true; continue }
            if (Test-Path -LiteralPath (Join-Path $GameDir $dep)) {
                $queue.Enqueue($dep)
            } else {
                $missing.Add("$dep   (required by $dll)")
                $seen[$depKey] = $true
            }
        }
    } catch {
        Write-Warning "could not parse imports of ${dll}: $($_.Exception.Message)"
    }
}

Write-Host ("non-system DLLs present : {0}" -f ($found | Sort-Object -Unique).Count)
foreach ($f in ($found | Sort-Object -Unique)) { Write-Host "    $f" }

if ($RequireLav) {
    Write-Host ''
    Write-Host 'registration-free LAV Filters (<GameDir>\lav):'
    foreach ($f in 'LAVSplitter.ax', 'LAVVideo.ax', 'LAVAudio.ax', 'LAVFilters.Dependencies.manifest') {
        $p = Join-Path (Join-Path $GameDir 'lav') $f
        if (Test-Path -LiteralPath $p) { Write-Host "    OK      $f" }
        else { Write-Host "    MISSING $f"; $missing.Add("lav\$f") }
    }
}

Write-Host ''
if ($missing.Count -gt 0) {
    Write-Host 'RESULT: INCOMPLETE - these are missing:' -ForegroundColor Red
    foreach ($m in ($missing | Sort-Object -Unique)) { Write-Host "    $m" -ForegroundColor Red }
    Write-Host ''
    Write-Host 'A missing member of this closure makes LoadLibraryA("libjxl.dll") fail with'
    Write-Host 'err=126. The hook still logs "Hook installed" and simply never decodes anything.'
    exit 1
}

Write-Host 'RESULT: OK - every non-system dependency resolves from the game directory.' -ForegroundColor Green
exit 0
