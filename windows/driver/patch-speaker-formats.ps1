Set-StrictMode -Version Latest

function Set-PulseFxSpeakerFormats {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$SampleRoot
    )

    $path = Join-Path $SampleRoot 'Source\Filters\speakerwavtable.h'
    if (-not (Test-Path -LiteralPath $path)) {
        throw "speakerwavtable.h was not found at $path"
    }

    $text = Get-Content -LiteralPath $path -Raw
    $original = $text
    $text = $text.Replace(
        '#define SPEAKER_DEVICE_MAX_CHANNELS                 2',
        '#define SPEAKER_DEVICE_MAX_CHANNELS                 8')
    $text = $text.Replace(
        '#define SPEAKER_HOST_MAX_CHANNELS                   2',
        '#define SPEAKER_HOST_MAX_CHANNELS                   8')

    $formats = @'
static 
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerHostPinSupportedDeviceFormats[] =
{
    { // Stereo 48 kHz / 16-bit PCM
        {
            sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        {
            {
                WAVE_FORMAT_EXTENSIBLE,
                2,
                48000,
                192000,
                4,
                16,
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
            },
            16,
            KSAUDIO_SPEAKER_STEREO,
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)
        }
    },
    { // 5.1 Surround: FL FR FC LFE SL SR
        {
            sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        {
            {
                WAVE_FORMAT_EXTENSIBLE,
                6,
                48000,
                576000,
                12,
                16,
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
            },
            16,
            KSAUDIO_SPEAKER_5POINT1_SURROUND,
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)
        }
    },
    { // 7.1 Surround: FL FR FC LFE BL BR SL SR
        {
            sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE),
            0,
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        {
            {
                WAVE_FORMAT_EXTENSIBLE,
                8,
                48000,
                768000,
                16,
                16,
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
            },
            16,
            KSAUDIO_SPEAKER_7POINT1_SURROUND,
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)
        }
    }
};

'@

    $pattern = '(?s)static\s+KSDATAFORMAT_WAVEFORMATEXTENSIBLE\s+SpeakerHostPinSupportedDeviceFormats\[\]\s*=\s*\{.*?\};\s*(?=//\s*// Supported modes)'
    $updated = [regex]::Replace($text, $pattern, $formats, 1)
    if ($updated -eq $text) {
        throw 'Could not replace the pinned SimpleAudioSample speaker format table.'
    }
    $text = $updated

    if ($text -eq $original) {
        throw 'PulseFX multichannel format patch made no changes.'
    }
    if (-not $text.Contains('KSAUDIO_SPEAKER_5POINT1_SURROUND') -or
        -not $text.Contains('KSAUDIO_SPEAKER_7POINT1_SURROUND') -or
        -not $text.Contains('WAVE_FORMAT_EXTENSIBLE,' + "`r`n" + '                8,')) {
        throw 'PulseFX multichannel speaker format verification failed.'
    }

    Set-Content -LiteralPath $path -Value $text -Encoding utf8
}
