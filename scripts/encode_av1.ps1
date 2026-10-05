param(
    [Parameter(Mandatory=$true)]
    [string]$InputPath,

    [Parameter(Mandatory=$false)]
    [string]$OutputPath,

    [int]$Quality = 26,
    [int]$AudioBitrate = 128
)

$cliCmd = Get-Command HandBrakeCLI -ErrorAction SilentlyContinue; if (-not $cliCmd) { $cliCmd = Get-Command HandBrakeCLI.exe -ErrorAction SilentlyContinue }; if (-not $cliCmd) { Write-Error 'HandBrakeCLI executable not found in PATH.'; exit 1 }; $cli = $cliCmd.Source
if (-not (Test-Path $cli)) {
    Write-Error "HandBrakeCLI.exe not found at $cli"
    exit 1
}

if (-not (Test-Path $InputPath)) {
    Write-Error "Input file not found: $InputPath"
    exit 1
}

if (-not $OutputPath) {
    $dir = Split-Path $InputPath
    $base = [System.IO.Path]::GetFileNameWithoutExtension($InputPath)
    $OutputPath = Join-Path $dir "$base.mp4"
}

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host " AdvHD AV1 Video Transcoder (SVT-AV1)" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host " Input   : $InputPath"
Write-Host " Output  : $OutputPath"
Write-Host " Encoder : svt_av1 (Quality: RF $Quality)"
Write-Host " Audio   : opus ($AudioBitrate kbps)"

$sw = [System.Diagnostics.Stopwatch]::StartNew()

& $cli -i $InputPath -o $OutputPath -e svt_av1 -q $Quality -E opus -B $AudioBitrate

$sw.Stop()

if (Test-Path $OutputPath) {
    $inLen = (Get-Item $InputPath).Length
    $outLen = (Get-Item $OutputPath).Length
    $ratio = ($outLen / $inLen) * 100
    Write-Host "`n[SUCCESS] Transcoding finished in $($sw.Elapsed.TotalSeconds.ToString('F1'))s" -ForegroundColor Green
    Write-Host ("  Original Size : {0:N0} bytes ({1:F2} MB)" -f $inLen, ($inLen / 1MB))
    Write-Host ("  AV1 Size      : {0:N0} bytes ({1:F2} MB)" -f $outLen, ($outLen / 1MB))
    Write-Host ("  Compression   : {0:F1}% ({1:F2}x smaller)" -f $ratio, ($inLen / $outLen)) -ForegroundColor Yellow
} else {
    Write-Error "[FAILED] Output file was not generated!"
    exit 1
}
