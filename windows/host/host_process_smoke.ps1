[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$HostPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $HostPath)) {
    throw "PulseFX native host was not found: $HostPath"
}

$commands = @(
    'ping',
    'status',
    'mode manual',
    'status',
    'mode signature',
    'status',
    'mode unsupported',
    'quit'
)
$lines = @($commands | & $HostPath)
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "PulseFX native host exited with code $exitCode"
}

# The native host emits one startup status before responding to stdin.
if ($lines.Count -ne 9) {
    throw "Expected 9 JSON lines (startup + 8 responses), got $($lines.Count): $($lines -join ' | ')"
}

$messages = @()
foreach ($line in $lines) {
    try {
        $messages += ($line | ConvertFrom-Json)
    } catch {
        throw "Native host emitted invalid JSON: $line"
    }
}

$expectedTypes = @('status', 'pong', 'status', 'ack', 'status', 'ack', 'status', 'error', 'ack')
for ($index = 0; $index -lt $expectedTypes.Count; $index++) {
    if ($messages[$index].type -ne $expectedTypes[$index]) {
        throw "Response $index had type '$($messages[$index].type)', expected '$($expectedTypes[$index])'"
    }
}

foreach ($index in @(0, 1, 2, 3, 4, 5, 6, 8)) {
    if ($messages[$index].ok -ne $true) {
        throw "Response $index was not successful: $($lines[$index])"
    }
}
if ($messages[7].ok -ne $false) {
    throw "Invalid processing mode unexpectedly succeeded: $($lines[7])"
}

if ($messages[0].controls.mode -ne 'signature') {
    throw "Native host did not start in Signature mode: $($lines[0])"
}
if ($messages[3].command -ne 'mode') {
    throw "Manual mode acknowledgement was malformed: $($lines[3])"
}
if ($messages[4].controls.mode -ne 'manual') {
    throw "Native host status did not report Manual after switching: $($lines[4])"
}
if ($messages[5].command -ne 'mode') {
    throw "Signature mode acknowledgement was malformed: $($lines[5])"
}
if ($messages[6].controls.mode -ne 'signature') {
    throw "Native host status did not report Signature after switching back: $($lines[6])"
}
if (-not $messages[7].error.Contains('signature or manual')) {
    throw "Invalid mode returned an unexpected error: $($lines[7])"
}
if ($messages[8].command -ne 'quit') {
    throw "Final acknowledgement was not for quit: $($lines[8])"
}

Write-Host 'PulseFX native host process smoke test passed, including Signature/Manual transitions.'
