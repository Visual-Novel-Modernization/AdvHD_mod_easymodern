#requires -Version 5
<#
.SYNOPSIS
    Unpack an AdvHD ARC V2 archive, convert its PNG textures to JPEG XL, repack it.

.DESCRIPTION
    Three steps, in order:
      1. unpack  - every member of the .arc is written into a cache directory
      2. convert - each .png is encoded to .jxl with cjxl and replaces the .png on disk
      3. repack  - the archive is rebuilt with the .jxl payloads but the ORIGINAL
                   entry names, order, and trailer preserved

    Step 3 is the part that matters. The manifest keeps the ".png" entry name while
    the payload becomes JPEG XL bytes. AdvHD looks entries up by name, so the name has
    to stay; jxl_hook.dll sniffs the magic bytes instead of trusting the extension.

    Repacking goes through advhd_arc_v2.ps1, which never reorders members and keeps
    the trailer. A repacker that enumerates the folder (alphabetical order) and drops
    the trailer will produce an archive the engine cannot read.

.PARAMETER ArcPath
    One or more .arc files to process. Accepts wildcards.

.PARAMETER CacheDir
    Scratch space for unpacked members. Defaults to a folder beside the archive.

.PARAMETER CjxlPath
    Path to cjxl.exe. Defaults to whatever `cjxl` resolves to on PATH.

.PARAMETER Quality
    cjxl -q value. 100 is lossless, which is what the published benchmarks used. Lower
    it to save more space at some visual cost.

.PARAMETER Effort
    cjxl --effort value, 1-10. Higher is slower and smaller.

.PARAMETER KeepCache
    Leave the unpacked members on disk for inspection.

.PARAMETER WhatIfRepack
    Do everything except overwrite the archive, and report what would change.

.EXAMPLE
    .\arc_jxl.ps1 -ArcPath 'E:\Game\Chip6.arc'

.EXAMPLE
    .\arc_jxl.ps1 -ArcPath 'E:\Game\Chip*.arc' -Quality 90 -Effort 7

.EXAMPLE
    .\arc_jxl.ps1 -ArcPath 'E:\Game\Graphic.arc' -WhatIfRepack
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string[]]$ArcPath,

    [string]$CacheDir,

    [string]$CjxlPath,

    [ValidateRange(0, 100)]
    [int]$Quality = 100,

    [ValidateRange(1, 10)]
    [int]$Effort = 7,

    [switch]$KeepCache,

    [switch]$WhatIfRepack
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'advhd_arc_v2.ps1')

# --- locate cjxl -------------------------------------------------------------
if (-not $CjxlPath) {
    $cmd = Get-Command cjxl -ErrorAction SilentlyContinue
    if ($cmd) { $CjxlPath = $cmd.Source }
}
if (-not $CjxlPath -or -not (Test-Path -LiteralPath $CjxlPath)) {
    throw "cjxl.exe not found. Pass -CjxlPath, or put cjxl on PATH. It ships with libjxl releases (jxl-x64-windows-static)."
}
$CjxlPath = (Resolve-Path -LiteralPath $CjxlPath).Path

$files = @()
foreach ($pattern in $ArcPath) {
    $resolved = Get-ChildItem -Path $pattern -File -ErrorAction SilentlyContinue
    if (-not $resolved) { Write-Warning "No files matched: $pattern"; continue }
    $files += $resolved
}
if (-not $files) { throw "Nothing to do." }

Write-Host "cjxl  : $CjxlPath"
Write-Host "mode  : -q $Quality --effort $Effort"
Write-Host "arcs  : $($files.Count)"
Write-Host ''

$grandOrig = 0L
$grandNew = 0L
$failed = @()

foreach ($f in $files) {
    $arc = $f.FullName
    $arcName = $f.Name
    $origSize = $f.Length
    $grandOrig += $origSize

    Write-Host "=== $arcName ($([Math]::Round($origSize / 1MB, 2)) MB) ==="

    # 1. unpack ---------------------------------------------------------------
    $workRoot = if ($CacheDir) { $CacheDir } else { Join-Path $f.DirectoryName '_arc_jxl_cache' }
    $unpackDir = Join-Path $workRoot ($arcName + '_raw')
    if (Test-Path -LiteralPath $unpackDir) { Remove-Item -LiteralPath $unpackDir -Recurse -Force }

    Write-Host "  [1/3] unpacking..."
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $unpacked = Expand-AdvhdArc -ArcPath $arc -OutputDir $unpackDir
    $sw.Stop()
    Write-Host "        $($unpacked.Entries) entries in $([Math]::Round($sw.Elapsed.TotalSeconds, 1))s"

    # 2. convert PNG -> JXL, in place ----------------------------------------
    $pngs = @(Get-ChildItem -LiteralPath $unpackDir -Filter '*.png' -File -Recurse)
    Write-Host "  [2/3] converting $($pngs.Count) PNG -> JXL..."

    $sw.Restart()
    $done = 0
    $converted = 0
    foreach ($png in $pngs) {
        $tmpJxl = $png.FullName + '.tmp.jxl'
        & $CjxlPath $png.FullName $tmpJxl -q $Quality --effort $Effort 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $tmpJxl)) {
            # Replace the .png file in place. The name on disk stays .png, which is
            # what keeps the manifest entry name untouched during repack.
            Move-Item -LiteralPath $tmpJxl -Destination $png.FullName -Force
            $converted++
        } elseif (Test-Path -LiteralPath $tmpJxl) {
            Remove-Item -LiteralPath $tmpJxl -Force
        }
        $done++
        if ($done % 100 -eq 0 -or $done -eq $pngs.Count) {
            Write-Host "        $done / $($pngs.Count)"
        }
    }
    $sw.Stop()
    Write-Host "        $converted converted in $([Math]::Round($sw.Elapsed.TotalSeconds, 1))s"

    if ($converted -eq 0) {
        Write-Warning "  no PNGs converted in $arcName, leaving the archive alone"
        if (-not $KeepCache) { Remove-Item -LiteralPath $unpackDir -Recurse -Force -ErrorAction SilentlyContinue }
        continue
    }

    # 3. repack ---------------------------------------------------------------
    # Map every manifest entry to its file on disk. Entries we converted now hold
    # JXL bytes under their original ".png" name, which is exactly what we want.
    $replacements = @{}
    $idx = Read-AdvhdArcIndex -ArcPath $arc
    foreach ($e in $idx.Entries) {
        $candidate = Join-Path $unpackDir $e.Name
        if (Test-Path -LiteralPath $candidate) { $replacements[$e.Name] = $candidate }
    }

    $outArc = if ($WhatIfRepack) { Join-Path $workRoot ($arcName + '.repacked') } else { $arc }
    Write-Host "  [3/3] repacking $(if ($WhatIfRepack) { "to $outArc (archive untouched)" } else { 'in place' })..."

    $sw.Restart()
    $null = Write-AdvhdArc -Index $idx -NewMemberPaths $replacements -OutArcPath $outArc
    $sw.Stop()

    $newSize = (Get-Item -LiteralPath $outArc).Length
    $grandNew += $newSize
    $savedMB = [Math]::Round(($origSize - $newSize) / 1MB, 2)
    $pct = if ($origSize -gt 0) { [Math]::Round((1 - $newSize / $origSize) * 100, 1) } else { 0 }

    # Structural check on what we just wrote.
    $v = Test-AdvhdArc -ArcPath $outArc
    $verdict = if ($v.Problems -eq 0 -and $v.Contiguous) { 'OK' } else { "PROBLEMS: $($v.ProblemList)" }

    Write-Host "        $([Math]::Round($origSize / 1MB, 2)) MB -> $([Math]::Round($newSize / 1MB, 2)) MB  (saved $savedMB MB, $pct%)  trailer=$($v.Trailer)  $verdict"
    if ($v.Problems -ne 0 -or -not $v.Contiguous) { $failed += $arcName }

    if (-not $KeepCache) { Remove-Item -LiteralPath $unpackDir -Recurse -Force -ErrorAction SilentlyContinue }
    Write-Host ''
}

if (-not $KeepCache -and $CacheDir -and (Test-Path -LiteralPath $CacheDir)) {
    Remove-Item -LiteralPath $CacheDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host '=== total ==='
if ($grandOrig -gt 0) {
    Write-Host "$([Math]::Round($grandOrig / 1MB, 2)) MB -> $([Math]::Round($grandNew / 1MB, 2)) MB  (saved $([Math]::Round(($grandOrig - $grandNew) / 1MB, 2)) MB, $([Math]::Round((1 - $grandNew / $grandOrig) * 100, 1))%)"
}
if ($failed.Count -gt 0) {
    Write-Warning "structural problems in: $($failed -join ', ')"
    exit 1
}
