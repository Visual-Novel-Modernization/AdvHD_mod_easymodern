#requires -Version 5
<#
  AdvHD / RioShiina ARC V2 - safe, layout-preserving toolkit.

  Container layout (verified empirically):
    [0x00] UInt32  fileCount
    [0x04] UInt32  manifestSize (bytes)
    [0x08] manifest: fileCount x { UInt32 length; UInt32 offset; UTF-16LE name + 0x0000 }
    [0x08+manifestSize] data area; member i lives at dataStart+offset, length bytes
    trailing bytes after last member = opaque (checksum/footer) -> preserved

  Design rule for the JXL mod: the manifest is preserved BYTE-FOR-BYTE; only the
  length fields of replaced members and all offset fields are rewritten, and
  only the data area is regenerated. Member order is NEVER changed.

  Do NOT use a naive repacker that enumerates the directory and sorts the entries
  (Get-ChildItem order is alphabetical, not archive order) and that omits the
  trailer. That silently reorders members and drops the footer. This file exists
  because that approach corrupts archives.
#>

function Read-AdvhdArcIndex {
    param([Parameter(Mandatory)][string]$ArcPath)
    $fs = [IO.File]::OpenRead($ArcPath)
    try {
        $br = New-Object IO.BinaryReader($fs, [Text.Encoding]::Unicode)
        $count = $br.ReadUInt32()
        $manifestSize = $br.ReadUInt32()
        $manifestStart = 8
        $rawManifest = New-Object byte[] $manifestSize
        $fs.Position = $manifestStart
        [void]$fs.Read($rawManifest, 0, [int]$manifestSize)
        $fs.Position = $manifestStart
        $entries = New-Object System.Collections.ArrayList
        for ($i = 0; $i -lt $count; $i++) {
            $len = $br.ReadUInt32()
            $off = $br.ReadUInt32()
            $sb = New-Object Text.StringBuilder
            while ($true) { $c = $br.ReadUInt16(); if ($c -eq 0) { break }; [void]$sb.Append([char]$c) }
            [void]$entries.Add([pscustomobject]@{ Index = $i; Name = $sb.ToString(); Length = $len; Offset = $off })
        }
        $dataStart = 8 + $manifestSize
        $last = $entries | Sort-Object Offset | Select-Object -Last 1
        $dataEnd = $dataStart + $last.Offset + $last.Length
        # -LiteralPath everywhere: game folders routinely carry '[' / ']' in their
        # names (e.g. "[250725] [Valve] ..."), which PowerShell's path cmdlets
        # treat as a wildcard character class.
        $fullPath = (Get-Item -LiteralPath $ArcPath).FullName
        $totalSize = (Get-Item -LiteralPath $ArcPath).Length
        $idx = [pscustomobject]@{
            ArcPath      = $fullPath
            FileCount    = $count
            ManifestSize = $manifestSize
            Manifest     = $rawManifest
            DataStart    = $dataStart
            DataEnd      = $dataEnd
            Trailer      = $totalSize - $dataEnd
            TotalSize    = $totalSize
            Entries      = $entries
            Br           = $br
            Fs           = $fs
        }
    } catch {
        # Nothing to keep open if we failed mid-parse.
        if ($br) { $br.Dispose() }
        $fs.Dispose()
        throw
    }
    # Deliberately NOT closing $fs/$br here. .NET file handles are closed when the
    # object is collected, but the GC may not run before the caller wants to
    # overwrite the same file in place, and Windows refuses that while a read
    # handle is open. The caller releases via Close-AdvhdArcIndex.
    return $idx
}

# Release the read handle held by an index. Safe to call more than once.
function Close-AdvhdArcIndex {
    param([Parameter(Mandatory)]$Index)
    if ($Index.PSObject.Properties['Br'] -and $Index.Br) { $Index.Br.Dispose() }
    if ($Index.PSObject.Properties['Fs'] -and $Index.Fs) { $Index.Fs.Dispose() }
}

function Get-AdvhdArcMember {
    param(
        [Parameter(Mandatory)][string]$ArcPath,
        [Parameter(Mandatory)]$Entry,
        [Parameter(Mandatory)][string]$OutFile,
        [int]$DataStart = -1     # pass the index's DataStart to skip re-parsing the header
    )
    if ($DataStart -lt 0) {
        $idx = Read-AdvhdArcIndex -ArcPath $ArcPath
        $DataStart = $idx.DataStart
        Close-AdvhdArcIndex -Index $idx
    }
    $fs = [IO.File]::OpenRead($ArcPath)
    try {
        $fs.Position = $DataStart + $Entry.Offset
        $buf = New-Object byte[] (1MB)
        $out = [IO.File]::Create($OutFile)
        try {
            $left = [int64]$Entry.Length
            while ($left -gt 0) {
                $want = [int][Math]::Min($buf.Length, $left)
                $got = $fs.Read($buf, 0, $want)
                if ($got -le 0) { throw "unexpected EOF reading $($Entry.Name)" }
                $out.Write($buf, 0, $got)
                $left -= $got
            }
        } finally { $out.Dispose() }
    } finally { $fs.Dispose() }
}

# Unpack every member into $OutputDir. Names come from the manifest, so the
# original index spelling is preserved (a .jxl payload can still be stored under
# a ".png" entry name).
function Expand-AdvhdArc {
    param(
        [Parameter(Mandatory)][string]$ArcPath,
        [Parameter(Mandatory)][string]$OutputDir
    )
    if (-not (Test-Path -LiteralPath $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    }
    $idx = Read-AdvhdArcIndex -ArcPath $ArcPath
    $written = 0
    try {
        foreach ($e in ($idx.Entries | Sort-Object Index)) {
            $target = Join-Path $OutputDir $e.Name
            $parent = Split-Path $target -Parent
            if ($parent -and -not (Test-Path -LiteralPath $parent)) {
                New-Item -ItemType Directory -Path $parent -Force | Out-Null
            }
            Get-AdvhdArcMember -ArcPath $ArcPath -Entry $e -OutFile $target -DataStart $idx.DataStart
            $written++
        }
    } finally {
        # Release the parser's handle so the caller can repack the same file in place.
        Close-AdvhdArcIndex -Index $idx
    }
    return [pscustomobject]@{
        Arc      = (Split-Path $ArcPath -Leaf)
        Entries  = $written
        OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
    }
}

# Rebuild an ARC: keep header+manifest bytes semantically identical (only
# Length/Offset of each entry updated), regenerate data area in the ORIGINAL
# member order, preserve trailer.
function Write-AdvhdArc {
    param(
        [Parameter(Mandatory)]$Index,                       # from Read-AdvhdArcIndex
        [Parameter(Mandatory)][hashtable]$NewMemberPaths,   # name -> file path (replacement)
        [Parameter(Mandatory)][string]$OutArcPath
    )
    $src = $Index.ArcPath

    # Release the parser's read handle up front. Writing in place means opening the
    # same path for Create, and Windows refuses that while any read handle is open.
    Close-AdvhdArcIndex -Index $Index

    # 1. compute new offsets in ORIGINAL order
    $entries = @($Index.Entries | Sort-Object Index)
    $offset = [uint32]0
    $plan = New-Object System.Collections.ArrayList
    foreach ($e in $entries) {
        $path = $null
        if ($NewMemberPaths.ContainsKey($e.Name)) { $path = $NewMemberPaths[$e.Name] }
        $len = if ($path) { [uint32](Get-Item -LiteralPath $path).Length } else { [uint32]$e.Length }
        [void]$plan.Add([pscustomobject]@{ Entry = $e; Path = $path; NewLength = $len; NewOffset = $offset })
        $offset = [uint32]($offset + $len)
    }
    if ($offset -gt [uint32]::MaxValue) { throw "data area exceeds 4 GiB uint32 offset limit" }

    # 2. rebuild manifest bytes
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter($ms, [Text.Encoding]::Unicode)
    foreach ($p in $plan) {
        $bw.Write([uint32]$p.NewLength)
        $bw.Write([uint32]$p.NewOffset)
        $bw.Write([Text.Encoding]::Unicode.GetBytes($p.Entry.Name + [char]0))
    }
    $bw.Flush()
    $manifest = $ms.ToArray()
    $bw.Dispose(); $ms.Dispose()

    # 3. snapshot the ORIGINAL members we are going to keep, plus the trailer.
    #    Windows will not let a read handle coexist with the Create below when
    #    OutArcPath is the same file, so everything we need from the source has to
    #    be pulled into memory first.
    $keepBytes = @{}
    $trailerBytes = $null
    if ($OutArcPath -eq $src) {
        $srcFs = [IO.File]::OpenRead($src)
        try {
            foreach ($p in $plan) {
                if ($p.Path) { continue }   # replaced member: no need
                $srcFs.Position = $Index.DataStart + $p.Entry.Offset
                $bufBytes = New-Object byte[] ([int]$p.Entry.Length)
                $read = 0
                while ($read -lt $bufBytes.Length) {
                    $got = $srcFs.Read($bufBytes, $read, $bufBytes.Length - $read)
                    if ($got -le 0) { throw "EOF on original member $($p.Entry.Name)" }
                    $read += $got
                }
                $keepBytes[$p.Entry.Index] = $bufBytes
            }
            if ($Index.Trailer -gt 0) {
                $srcFs.Position = $Index.DataEnd
                $trailerBytes = New-Object byte[] ([int]$Index.Trailer)
                $read = 0
                while ($read -lt $trailerBytes.Length) {
                    $got = $srcFs.Read($trailerBytes, $read, $trailerBytes.Length - $read)
                    if ($got -le 0) { throw "EOF reading trailer" }
                    $read += $got
                }
            }
        } finally { $srcFs.Dispose() }
    }

    # 4. write the new archive
    $out = [IO.File]::Create($OutArcPath)
    try {
        $w = New-Object IO.BinaryWriter($out)
        $w.Write([uint32]$Index.FileCount)
        $w.Write([uint32]$manifest.Length)
        $w.Write($manifest)
        $buf = New-Object byte[] (4MB)
        foreach ($p in $plan) {
            if ($p.Path) {
                $in = [IO.File]::OpenRead($p.Path)
                try {
                    while (($got = $in.Read($buf, 0, $buf.Length)) -gt 0) { $out.Write($buf, 0, $got) }
                } finally { $in.Dispose() }
            } elseif ($keepBytes.ContainsKey($p.Entry.Index)) {
                $out.Write($keepBytes[$p.Entry.Index], 0, $keepBytes[$p.Entry.Index].Length)
            } else {
                # OutArcPath differs from source: stream straight from source
                $in = [IO.File]::OpenRead($src)
                try {
                    $in.Position = $Index.DataStart + $p.Entry.Offset
                    $left = [int64]$p.Entry.Length
                    while ($left -gt 0) {
                        $want = [int][Math]::Min($buf.Length, $left)
                        $got = $in.Read($buf, 0, $want)
                        if ($got -le 0) { throw "EOF on original member $($p.Entry.Name)" }
                        $out.Write($buf, 0, $got)
                        $left -= $got
                    }
                } finally { $in.Dispose() }
            }
        }
        $w.Flush()

        # 5. write the trailing bytes we snapshotted in step 3, or stream them from
        #    the source when writing to a different file.
        if ($trailerBytes) {
            $out.Write($trailerBytes, 0, $trailerBytes.Length)
        } elseif ($Index.Trailer -gt 0 -and $OutArcPath -ne $src) {
            $srcFs2 = [IO.File]::OpenRead($src)
            try {
                $srcFs2.Position = $Index.DataEnd
                $left = [int64]$Index.Trailer
                while ($left -gt 0) {
                    $want = [int][Math]::Min($buf.Length, $left)
                    $got = $srcFs2.Read($buf, 0, $want)
                    if ($got -le 0) { throw "EOF reading trailer" }
                    $out.Write($buf, 0, $got)
                    $left -= $got
                }
            } finally { $srcFs2.Dispose() }
        }
    } finally { $out.Dispose() }

    return [pscustomobject]@{ Entries = $plan.Count; DataBytes = $offset; ManifestBytes = $manifest.Length }
}

function Test-AdvhdArc {
    param([Parameter(Mandatory)][string]$ArcPath, [string]$ReferenceArc)
    $idx = Read-AdvhdArcIndex -ArcPath $ArcPath
    $problems = New-Object System.Collections.ArrayList
    $seen = @{}
    foreach ($e in $idx.Entries) {
        if ($seen.ContainsKey($e.Name)) { [void]$problems.Add("duplicate name: $($e.Name)") }
        $seen[$e.Name] = $true
        if (($idx.DataStart + $e.Offset + $e.Length) -gt $idx.TotalSize) {
            [void]$problems.Add("out of range: $($e.Name)")
        }
    }
    $sum = ($idx.Entries | Measure-Object Length -Sum).Sum
    $contiguous = ($sum + $idx.DataStart -le $idx.TotalSize)
    [pscustomobject]@{
        Arc            = (Split-Path $ArcPath -Leaf)
        Entries        = $idx.FileCount
        ManifestSize   = $idx.ManifestSize
        DataStart      = $idx.DataStart
        LogicalData    = $sum
        TotalSize      = $idx.TotalSize
        Trailer        = $idx.Trailer
        Contiguous     = $contiguous
        Problems       = $problems.Count
        ProblemList    = ($problems | Select-Object -First 5) -join '; '
    }
}
