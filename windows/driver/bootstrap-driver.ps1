[CmdletBinding()]
param(
    [switch]$Build,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateSet('x64', 'Win32', 'ARM64')]
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$UpstreamRevision = '717778a20ba4dd2440fe609f69153a1f8a64f597'
$UpstreamUrl = 'https://github.com/microsoft/Windows-driver-samples.git'
$WorkRoot = Join-Path $PSScriptRoot '.work'
$UpstreamRoot = Join-Path $WorkRoot 'Windows-driver-samples'
$PreparedRoot = Join-Path $WorkRoot 'PulseFXVirtualAudio'
$SampleRoot = Join-Path $UpstreamRoot 'audio\simpleaudiosample'
$OutRoot = Join-Path $PSScriptRoot 'out'

function Invoke-Checked {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [string[]]$Arguments,
        [string]$WorkingDirectory
    )

    if ($WorkingDirectory) { Push-Location $WorkingDirectory }
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$FilePath exited with code $LASTEXITCODE"
        }
    }
    finally {
        if ($WorkingDirectory) { Pop-Location }
    }
}

function Replace-Required {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$Old,
        [Parameter(Mandatory)] [string]$New
    )

    $text = Get-Content -LiteralPath $Path -Raw
    if (-not $text.Contains($Old)) {
        throw "Expected text was not found in $Path`: $Old"
    }
    $text = $text.Replace($Old, $New)
    Set-Content -LiteralPath $Path -Value $text -Encoding utf8
}

if (-not (Get-Command git.exe -ErrorAction SilentlyContinue)) {
    throw 'Git is required to prepare the PulseFX virtual-audio driver.'
}

New-Item -ItemType Directory -Path $WorkRoot -Force | Out-Null

if (-not (Test-Path (Join-Path $UpstreamRoot '.git'))) {
    Invoke-Checked git.exe @('clone', '--filter=blob:none', '--no-checkout', $UpstreamUrl, $UpstreamRoot)
}

Invoke-Checked git.exe @('-C', $UpstreamRoot, 'fetch', '--depth', '1', 'origin', $UpstreamRevision)
Invoke-Checked git.exe @('-C', $UpstreamRoot, 'checkout', '--detach', $UpstreamRevision)

if (-not (Test-Path $SampleRoot)) {
    throw "Pinned Microsoft SimpleAudioSample was not found at $SampleRoot"
}

if (Test-Path $PreparedRoot) {
    Remove-Item -LiteralPath $PreparedRoot -Recurse -Force
}
Copy-Item -LiteralPath $SampleRoot -Destination $PreparedRoot -Recurse

# Keep only the render endpoint at runtime. The Microsoft sample already uses
# the no-offload/no-loopback KSPIN_WAVE_RENDER3 speaker topology.
$miniPairsPath = Join-Path $PreparedRoot 'Source\Filters\minipairs.h'
Replace-Required \
    -Path $miniPairsPath \
    -Old '#define g_cCaptureEndpoints (SIZEOF_ARRAY(g_CaptureEndpoints))' \
    -New '#define g_cCaptureEndpoints 0'

$infPath = Join-Path $PreparedRoot 'Source\Main\SimpleAudioSample.inx'
Replace-Required -Path $infPath -Old 'ROOT\SimpleAudioSample' -New 'ROOT\PulseFXVirtualAudio'
Replace-Required -Path $infPath -Old 'ProviderName = "TODO-Set-Provider"' -New 'ProviderName = "PulseFX"'
Replace-Required -Path $infPath -Old 'MfgName      = "TODO-Set-Manufacturer"' -New 'MfgName      = "PulseFX"'
Replace-Required -Path $infPath -Old 'SIMPLEAUDIOSAMPLE_SA.DeviceDesc="Virtual Audio Device (WDM) - Simple Audio Sample"' -New 'SIMPLEAUDIOSAMPLE_SA.DeviceDesc="PulseFX Output"'
Replace-Required -Path $infPath -Old 'SimpleAudioSample.SvcDesc="Virtual Audio Device (WDM) - Simple Audio Sample Driver"' -New 'SimpleAudioSample.SvcDesc="PulseFX Virtual Audio Driver"'
Replace-Required -Path $infPath -Old 'SIMPLEAUDIOSAMPLE.WaveSpeaker.szPname="Simple Audio Sample Wave Speaker"' -New 'SIMPLEAUDIOSAMPLE.WaveSpeaker.szPname="PulseFX Output Wave"'
Replace-Required -Path $infPath -Old 'SIMPLEAUDIOSAMPLE.TopologySpeaker.szPname="Simple Audio Sample Topology Speaker"' -New 'SIMPLEAUDIOSAMPLE.TopologySpeaker.szPname="PulseFX Output Topology"'

# Do not publish capture interfaces for the disabled sample microphone.
$infLines = Get-Content -LiteralPath $infPath
$filteredInfLines = $infLines | Where-Object {
    $_ -notmatch '^AddInterface=.*(WaveMicArray1|TopologyMicArray1)'
}
Set-Content -LiteralPath $infPath -Value $filteredInfLines -Encoding utf8

$marker = @(
    "PulseFX virtual audio upstream",
    "repository=$UpstreamUrl",
    "revision=$UpstreamRevision",
    "sample=audio/simpleaudiosample",
    "capture_endpoints=disabled",
    "hardware_id=ROOT\PulseFXVirtualAudio"
) -join "`r`n"
Set-Content -LiteralPath (Join-Path $PreparedRoot 'PULSEFX_UPSTREAM.txt') -Value $marker -Encoding ascii

Write-Host "Prepared PulseFX virtual-audio source at: $PreparedRoot"
Write-Host "Pinned Microsoft revision: $UpstreamRevision"

if (-not $Build) {
    Write-Host 'Preparation complete. Re-run with -Build on a Windows machine with Visual Studio + WDK to compile the driver package.'
    exit 0
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (-not $msbuild) {
    $candidate = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($candidate) { $msbuild = $candidate.Source }
}
if (-not $msbuild) {
    throw 'MSBuild was not found. Install Visual Studio C++ build tools plus the Windows Driver Kit (WDK).'
}

$solution = Join-Path $PreparedRoot 'SimpleAudioSample.sln'
if (-not (Test-Path $solution)) {
    throw "SimpleAudioSample.sln was not found at $solution"
}

Invoke-Checked $msbuild @(
    $solution,
    '/m',
    '/t:Build',
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    '/p:RunCodeAnalysis=true'
)

$package = Get-ChildItem -Path $PreparedRoot -Directory -Recurse -Filter package |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if (-not $package) {
    throw 'Build completed but no driver package directory was found.'
}

if (Test-Path $OutRoot) { Remove-Item -LiteralPath $OutRoot -Recurse -Force }
New-Item -ItemType Directory -Path $OutRoot -Force | Out-Null
Copy-Item -Path (Join-Path $package.FullName '*') -Destination $OutRoot -Recurse -Force

Write-Host "PulseFX virtual-audio package copied to: $OutRoot"
Write-Host 'This script intentionally does not enable test-signing, disable Secure Boot, or install the driver.'
