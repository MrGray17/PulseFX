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

$commands = @('ping', 'status', 'devices', 'quit')
$lines = @($commands | & $HostPath)
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "PulseFX native host exited with code $exitCode"
}

if ($lines.Count -ne 5) {
    throw "Expected 5 JSON lines (startup + 4 responses), got $($lines.Count): $($lines -join ' | ')"
}

$messages = @()
foreach ($line in $lines) {
    try {
        $messages += ($line | ConvertFrom-Json)
    } catch {
        throw "Native host emitted invalid JSON: $line"
    }
}

$expectedTypes = @('status', 'pong', 'status', 'devices', 'ack')
for ($index = 0; $index -lt $expectedTypes.Count; $index++) {
    if ($messages[$index].type -ne $expectedTypes[$index]) {
        throw "Response $index had type '$($messages[$index].type)', expected '$($expectedTypes[$index])'"
    }
    if ($messages[$index].ok -ne $true) {
        throw "Response $index was not successful: $($lines[$index])"
    }
}

if ($messages[4].command -ne 'quit') {
    throw "Final acknowledgement was not for quit: $($lines[4])"
}

Write-Host 'PulseFX native host process smoke test passed.'
