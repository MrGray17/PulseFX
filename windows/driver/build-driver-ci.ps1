[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64', 'arm64')]
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$UpstreamRevision = '717778a20ba4dd2440fe609f69153a1f8a64f597'
$UpstreamUrl = 'https://github.com/microsoft/Windows-driver-samples.git'
$WorkRoot = Join-Path $PSScriptRoot '.ci-work'
$RepoRoot = Join-Path $WorkRoot 'Windows-driver-samples'
$SampleRoot = Join-Path $RepoRoot 'audio\simpleaudiosample'
$OutRoot = Join-Path $PSScriptRoot 'out-ci'

. (Join-Path $PSScriptRoot 'patch-speaker-formats.ps1')

function Invoke-Checked {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [string[]]$Arguments,
        [string]$WorkingDirectory
    )
    if ($WorkingDirectory) { Push-Location $WorkingDirectory }
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$FilePath exited with code $LASTEXITCODE" }
    } finally {
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
    if (-not $text.Contains($Old)) { throw "Expected text was not found in $Path`: $Old" }
    Set-Content -LiteralPath $Path -Value ($text.Replace($Old, $New)) -Encoding utf8
}

function Add-WdkNuGetToolsToPath {
    param([Parameter(Mandatory)] [string]$PackagesRoot)

    $requiredTools = @('stampinf.exe', 'inf2cat.exe')
    $toolDirectories = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($toolName in $requiredTools) {
        $matches = Get-ChildItem -LiteralPath $PackagesRoot -Recurse -File -Filter $toolName -ErrorAction SilentlyContinue
        if (-not $matches) { throw "NuGet WDK restore did not contain required tool: $toolName" }

        # Prefer a native x64 host tool on the x64 GitHub runner, but add every
        # matching parent directory so companion WDK tools remain discoverable.
        foreach ($match in ($matches | Sort-Object @{ Expression = { if ($_.FullName -match '[\\/]x64[\\/]') { 0 } else { 1 } } }, FullName)) {
            [void]$toolDirectories.Add($match.Directory.FullName)
        }
    }

    if ($toolDirectories.Count -eq 0) { throw 'No NuGet WDK tool directories were discovered.' }
    $prefix = ($toolDirectories | Sort-Object) -join ';'
    $env:PATH = "$prefix;$env:PATH"

    foreach ($toolName in $requiredTools) {
        $resolved = Get-Command $toolName -ErrorAction SilentlyContinue
        if (-not $resolved) { throw "WDK tool was found in the package but is still not executable from PATH: $toolName" }
        Write-Host "WDK tool: $toolName -> $($resolved.Source)"
    }
}

if (Test-Path $WorkRoot) { Remove-Item -LiteralPath $WorkRoot -Recurse -Force }
New-Item -ItemType Directory -Path $WorkRoot -Force | Out-Null

Invoke-Checked git.exe @('clone', '--filter=blob:none', '--no-checkout', $UpstreamUrl, $RepoRoot)
Invoke-Checked git.exe @('-C', $RepoRoot, 'fetch', '--depth', '1', 'origin', $UpstreamRevision)
Invoke-Checked git.exe @('-C', $RepoRoot, 'checkout', '--detach', $UpstreamRevision)
Invoke-Checked git.exe @('-C', $RepoRoot, 'submodule', 'update', '--init', '--depth', '1')

if (-not (Test-Path $SampleRoot)) { throw "Pinned SimpleAudioSample was not found at $SampleRoot" }

$miniPairsPath = Join-Path $SampleRoot 'Source\Filters\minipairs.h'
Replace-Required -Path $miniPairsPath -Old '#define g_cCaptureEndpoints (SIZEOF_ARRAY(g_CaptureEndpoints))' -New '#define g_cCaptureEndpoints 0'
Set-PulseFxSpeakerFormats -SampleRoot $SampleRoot

$infPath = Join-Path $SampleRoot 'Source\Main\SimpleAudioSample.inx'
Replace-Required -Path $infPath -Old 'ROOT\SimpleAudioSample' -New 'ROOT\PulseFXVirtualAudio'
Replace-Required -Path $infPath -Old 'ProviderName = "TODO-Set-Provider"' -New 'ProviderName = "PulseFX"'
Replace-Required -Path $infPath -Old 'MfgName      = "TODO-Set-Manufacturer"' -New 'MfgName      = "PulseFX"'
Replace-Required -Path $infPath -Old 'SIMPLEAUDIOSAMPLE_SA.DeviceDesc="Virtual Audio Device (WDM) - Simple Audio Sample"' -New 'SIMPLEAUDIOSAMPLE_SA.DeviceDesc="PulseFX Output"'
Replace-Required -Path $infPath -Old 'SimpleAudioSample.SvcDesc="Virtual Audio Device (WDM) - Simple Audio Sample Driver"' -New 'SimpleAudioSample.SvcDesc="PulseFX Virtual Audio Driver"'
Replace-Required -Path $infPath -Old 'SIMPLEAUDIOSAMPLE.WaveSpeaker.szPname="Simple Audio Sample Wave Speaker"' -New 'SIMPLEAUDIOSAMPLE.WaveSpeaker.szPname="PulseFX Output Wave"'
Replace-Required -Path $infPath -Old 'SIMPLEAUDIOSAMPLE.TopologySpeaker.szPname="Simple Audio Sample Topology Speaker"' -New 'SIMPLEAUDIOSAMPLE.TopologySpeaker.szPname="PulseFX Output Topology"'

$infLines = Get-Content -LiteralPath $infPath
$filteredInfLines = $infLines | Where-Object { $_ -notmatch '^AddInterface=.*(WaveMicArray1|TopologyMicArray1)' }
Set-Content -LiteralPath $infPath -Value $filteredInfLines -Encoding utf8

$waveTable = Get-Content -LiteralPath (Join-Path $SampleRoot 'Source\Filters\speakerwavtable.h') -Raw
foreach ($required in @(
    'SPEAKER_DEVICE_MAX_CHANNELS                 8',
    'KSAUDIO_SPEAKER_5POINT1_SURROUND',
    'KSAUDIO_SPEAKER_7POINT1_SURROUND',
    '                6,',
    '                8,'
)) {
    if (-not $waveTable.Contains($required)) { throw "Patched speaker table is missing: $required" }
}

$nuget = Get-Command nuget.exe -ErrorAction SilentlyContinue
if (-not $nuget) { throw 'nuget.exe is required for the WDK NuGet restore.' }
$packagesRoot = Join-Path $RepoRoot 'packages'
Invoke-Checked $nuget.Source @('restore', (Join-Path $RepoRoot 'packages.config'), '-PackagesDirectory', $packagesRoot) $RepoRoot
Add-WdkNuGetToolsToPath -PackagesRoot $packagesRoot

# Do not depend on the Windows-driver-samples Build-Samples.ps1 environment
# discovery wrapper here. Hosted VS/WDK runner metadata has changed across
# versions and the wrapper can fail before MSBuild ever sees the sample. Build
# the pinned sample solution directly so this gate validates our actual driver.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (-not $msbuild) {
    $candidate = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($candidate) { $msbuild = $candidate.Source }
}
if (-not $msbuild) { throw 'MSBuild was not found on the Windows CI runner.' }

$solution = Join-Path $SampleRoot 'SimpleAudioSample.sln'
if (-not (Test-Path $solution)) { throw "SimpleAudioSample.sln was not found at $solution" }

# The NuGet WDK's legacy Prefast plug-in currently cannot load under the hosted
# VS 2026 runner (C1250 before analysis begins). Disable only that unavailable
# analyzer in this environment. The sample projects still compile with /W4 /WX,
# so ordinary compiler warnings and errors remain release-blocking.
Invoke-Checked $msbuild @(
    $solution,
    '/m',
    '/t:Build',
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    '/p:RunCodeAnalysis=false'
) $SampleRoot

$driver = Get-ChildItem -LiteralPath $SampleRoot -Recurse -File -Filter 'SimpleAudioSample.sys' |
    Where-Object { $_.FullName -match '[\\/]package[\\/]' } |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if (-not $driver) { throw 'Driver build completed but no packaged SimpleAudioSample.sys was found.' }

$packageDir = $driver.Directory.FullName
if (Test-Path $OutRoot) { Remove-Item -LiteralPath $OutRoot -Recurse -Force }
New-Item -ItemType Directory -Path $OutRoot -Force | Out-Null
Copy-Item -Path (Join-Path $packageDir '*') -Destination $OutRoot -Recurse -Force

$builtInf = Get-ChildItem -LiteralPath $OutRoot -File -Filter '*.inf' | Select-Object -First 1
if (-not $builtInf) { throw 'Built driver package does not contain an INF.' }
$infText = Get-Content -LiteralPath $builtInf.FullName -Raw
if (-not $infText.Contains('ROOT\PulseFXVirtualAudio')) { throw 'Built INF is missing the PulseFX hardware ID.' }
if (-not $infText.Contains('PulseFX Output')) { throw 'Built INF is missing PulseFX output branding.' }
if ($infText -match 'KSCATEGORY_CAPTURE.*WaveMicArray1') { throw 'Built INF still publishes the sample capture endpoint.' }

$manifest = @(
    "upstream=$UpstreamUrl",
    "revision=$UpstreamRevision",
    "configuration=$Configuration",
    "platform=$Platform",
    "hardware_id=ROOT\PulseFXVirtualAudio",
    "capture_endpoints=disabled",
    "render_formats=stereo,5.1-surround,7.1-surround",
    "sample_rate=48000",
    "bits_per_sample=16",
    "compiler_warnings_as_errors=true",
    "hosted_prefast=disabled-unavailable",
    "wdk_tools=nuget-discovered"
) -join "`r`n"
Set-Content -LiteralPath (Join-Path $OutRoot 'PULSEFX_BUILD.txt') -Value $manifest -Encoding ascii

Write-Host "PulseFX driver package validated at $OutRoot"
