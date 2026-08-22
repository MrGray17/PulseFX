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
    'content music',
    'low_latency true',
    'signature_strength 1.1',
    'spatial_calibration true 1.05 1.02 0.90 -0.5',
    'scene_set game.exe game true 90',
    'scene_enable true',
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
if ($lines.Count -ne 15) {
    throw "Expected 15 JSON lines (startup + 14 responses), got $($lines.Count): $($lines -join ' | ')"
}

$messages = @()
foreach ($line in $lines) {
    try {
        $messages += ($line | ConvertFrom-Json)
    } catch {
        throw "Native host emitted invalid JSON: $line"
    }
}

$expectedTypes = @(
    'status','pong','ack','ack','ack','ack','ack','ack','status','ack','status','ack','status','error','ack'
)
for ($index = 0; $index -lt $expectedTypes.Count; $index++) {
    if ($messages[$index].type -ne $expectedTypes[$index]) {
        throw "Response $index had type '$($messages[$index].type)', expected '$($expectedTypes[$index])'"
    }
}

foreach ($index in @(0,1,2,3,4,5,6,7,8,9,10,11,12,14)) {
    if ($messages[$index].ok -ne $true) {
        throw "Response $index was not successful: $($lines[$index])"
    }
}
if ($messages[13].ok -ne $false) {
    throw "Invalid processing mode unexpectedly succeeded: $($lines[13])"
}

if ($messages[0].controls.mode -ne 'signature') {
    throw "Native host did not start in Signature mode: $($lines[0])"
}
if ($messages[8].controls.mode -ne 'signature' -or $messages[8].controls.content -ne 'music') {
    throw "Signature policy status did not preserve content intent: $($lines[8])"
}
if ($messages[8].controls.lowLatency -ne $true) {
    throw "Low-latency policy was not reported active: $($lines[8])"
}
if ([Math]::Abs([double]$messages[8].controls.signatureStrength - 1.1) -gt 0.001) {
    throw "Signature strength was not reported correctly: $($lines[8])"
}
if ($messages[8].calibration.enabled -ne $true) {
    throw "Spatial calibration was not reported enabled: $($lines[8])"
}
if ($messages[8].scene.ruleCount -ne 1) {
    throw "Scene rule was not retained: $($lines[8])"
}
if ($messages[8].stats.processorLatencyFrames -lt 0 -or $messages[8].stats.masterWetMix -lt 0 -or $messages[8].stats.masterWetMix -gt 1) {
    throw "Native diagnostics escaped valid bounds: $($lines[8])"
}
if ($messages[10].controls.mode -ne 'manual') {
    throw "Native host status did not report Manual after switching: $($lines[10])"
}
if ($messages[12].controls.mode -ne 'signature') {
    throw "Native host status did not report Signature after switching back: $($lines[12])"
}
if (-not $messages[13].error.Contains('signature or manual')) {
    throw "Invalid mode returned an unexpected error: $($lines[13])"
}
if ($messages[14].command -ne 'quit') {
    throw "Final acknowledgement was not for quit: $($lines[14])"
}

Write-Host 'PulseFX host smoke test passed: Signature/Manual, content, interactive policy, strength, calibration, scenes, diagnostics.'
