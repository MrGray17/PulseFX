[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackagePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolved = Resolve-Path -LiteralPath $PackagePath -ErrorAction Stop
$root = $resolved.Path
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    throw "Driver package path is not a directory: $root"
}

$catalog = Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.cat' | Select-Object -First 1
$driver = Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.sys' | Select-Object -First 1
$inf = Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.inf' | Select-Object -First 1
if (-not $catalog -or -not $driver -or -not $inf) {
    throw 'Release driver package must contain .cat, .sys, and .inf files.'
}

$signtool = Get-Command signtool.exe -ErrorAction SilentlyContinue
if (-not $signtool) {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path $kits) {
        $candidate = Get-ChildItem -LiteralPath $kits -Recurse -File -Filter 'signtool.exe' |
            Where-Object { $_.FullName -match '[\\/]x64[\\/]signtool\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) { $signtool = $candidate }
    }
}
if (-not $signtool) { throw 'SignTool was not found. Install the Windows SDK/WDK.' }
$signToolPath = if ($signtool.Source) { $signtool.Source } else { $signtool.FullName }

function Invoke-SignTool {
    param([Parameter(Mandatory)] [string[]]$Arguments)
    & $signToolPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "SignTool kernel-policy verification failed with exit code $LASTEXITCODE."
    }
}

# Verify the release-signed catalog itself against Windows kernel signing policy.
Invoke-SignTool @('verify', '/kp', '/v', $catalog.FullName)

# Verify that the driver and INF are members of the trusted release catalog.
Invoke-SignTool @('verify', '/kp', '/v', '/c', $catalog.FullName, $driver.FullName)
Invoke-SignTool @('verify', '/kp', '/v', '/c', $catalog.FullName, $inf.FullName)

Write-Host "PulseFX driver package passed kernel-policy signature verification: $root"
