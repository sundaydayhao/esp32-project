param(
    [string]$OutputDir = "main/audio/tts",
    [int]$VoiceIndex = 1,
    [int]$WaveFormatType = 6
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-ChineseNumber {
    param(
        [int]$Value,
        [switch]$Minute
    )

    $digits = @(
        [string][char]0x96F6,
        [string][char]0x4E00,
        [string][char]0x4E8C,
        [string][char]0x4E09,
        [string][char]0x56DB,
        [string][char]0x4E94,
        [string][char]0x516D,
        [string][char]0x4E03,
        [string][char]0x516B,
        [string][char]0x4E5D
    )

    if ($Value -lt 0 -or $Value -gt 99) {
        throw "Unsupported value: $Value"
    }

    if ($Minute -and $Value -lt 10) {
        if ($Value -eq 0) {
            return $digits[0]
        }
        return $digits[0] + $digits[$Value]
    }

    if ($Value -lt 10) {
        return $digits[$Value]
    }

    if ($Value -lt 20) {
        if ($Value -eq 10) {
            return [string][char]0x5341
        }
        return ([string][char]0x5341) + $digits[$Value % 10]
    }

    $tens = [int]($Value / 10)
    $ones = $Value % 10
    if ($ones -eq 0) {
        return $digits[$tens] + ([string][char]0x5341)
    }

    return $digits[$tens] + ([string][char]0x5341) + $digits[$ones]
}

function Convert-ToSapiXml {
    param([string]$Text)

    $entityText = [string]::Concat(($Text.ToCharArray() | ForEach-Object {
        if ($_ -eq ' ') {
            ' '
        } else {
            '&#' + [int][char]$_ + ';'
        }
    }))

    return "<speak version='1.0' xml:lang='zh-CN'><lang langid='804'>$entityText</lang></speak>"
}

function New-SapiVbs {
    param([string]$Path)

    $script = @'
Dim outPath, xmlText, voiceIndex
outPath = WScript.Arguments.Item(0)
xmlText = WScript.Arguments.Item(1)
voiceIndex = CInt(WScript.Arguments.Item(2))

Set voice = CreateObject("SAPI.SpVoice")
Set voices = voice.GetVoices()
If voiceIndex >= 0 And voiceIndex < voices.Count Then
    Set voice.Voice = voices.Item(voiceIndex)
End If

Set stream = CreateObject("SAPI.SpFileStream")
stream.Format.Type = 6
stream.Open outPath, 3, False
Set voice.AudioOutputStream = stream
voice.Speak xmlText, 8
stream.Close
'@

    Set-Content -LiteralPath $Path -Value $script -Encoding ASCII
}

function Read-Le32 {
    param(
        [byte[]]$Bytes,
        [int]$Offset
    )

    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Get-WavDataBytes {
    param([string]$Path)

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 44) {
        throw "WAV too small: $Path"
    }

    $offset = 12
    while ($offset + 8 -le $bytes.Length) {
        $chunkId = [System.Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $chunkSize = [int](Read-Le32 -Bytes $bytes -Offset ($offset + 4))
        $dataStart = $offset + 8
        $nextOffset = $dataStart + $chunkSize + ($chunkSize % 2)

        if ($nextOffset -gt $bytes.Length) {
            break
        }

        if ($chunkId -eq "data") {
            $data = New-Object byte[] $chunkSize
            [Array]::Copy($bytes, $dataStart, $data, 0, $chunkSize)
            return $data
        }

        $offset = $nextOffset
    }

    throw "WAV data chunk missing: $Path"
}

function Trim-WavDataBytes {
    param(
        [byte[]]$PcmData,
        [int]$Threshold = 200,
        [int]$PaddingSamples = 160
    )

    if ($PcmData.Length -lt 2) {
        return $PcmData
    }

    $sampleCount = [int]($PcmData.Length / 2)
    $first = -1
    $last = -1

    for ($i = 0; $i -lt $sampleCount; $i++) {
        $sample = [Math]::Abs([int][BitConverter]::ToInt16($PcmData, $i * 2))
        if ($sample -ge $Threshold) {
            $first = $i
            break
        }
    }

    if ($first -lt 0) {
        return $PcmData
    }

    for ($i = $sampleCount - 1; $i -ge 0; $i--) {
        $sample = [Math]::Abs([int][BitConverter]::ToInt16($PcmData, $i * 2))
        if ($sample -ge $Threshold) {
            $last = $i
            break
        }
    }

    $startSample = [Math]::Max(0, $first - $PaddingSamples)
    $endSample = [Math]::Min($sampleCount - 1, $last + $PaddingSamples)
    $trimmedLength = ($endSample - $startSample + 1) * 2

    $trimmed = New-Object byte[] $trimmedLength
    [Array]::Copy($PcmData, $startSample * 2, $trimmed, 0, $trimmedLength)
    return $trimmed
}

function Write-WavFile {
    param(
        [string]$Path,
        [byte[]]$PcmData
    )

    $fileStream = [System.IO.File]::Create($Path)
    $writer = New-Object System.IO.BinaryWriter($fileStream)
    try {
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
        $writer.Write([uint32](36 + $PcmData.Length))
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))
        $writer.Write([uint32]16)
        $writer.Write([uint16]1)
        $writer.Write([uint16]1)
        $writer.Write([uint32]$WaveFormatType * 0 + 8000)
        $writer.Write([uint32](8000 * 2))
        $writer.Write([uint16]2)
        $writer.Write([uint16]16)
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
        $writer.Write([uint32]$PcmData.Length)
        $writer.Write($PcmData)
    } finally {
        $writer.Dispose()
        $fileStream.Dispose()
    }
}

$resolvedOutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
$tempScript = Join-Path $env:TEMP "codex_generate_tts_assets.vbs"

New-SapiVbs -Path $tempScript

$assets = [ordered]@{
    "prefix_beijing_time.wav" = ([string][char]0x5317) + ([string][char]0x4EAC) + ([string][char]0x65F6) + ([string][char]0x95F4)
    "suffix_point.wav" = [string][char]0x70B9
    "suffix_exact.wav" = [string][char]0x6574
    "phrase_today_weather.wav" = ([string][char]0x4ECA) + ([string][char]0x5929) + ([string][char]0x5929) + ([string][char]0x6C14)
    "phrase_current_temperature.wav" = ([string][char]0x5F53) + ([string][char]0x524D) + ([string][char]0x6C14) + ([string][char]0x6E29)
    "phrase_weather_not_ready.wav" = ([string][char]0x5929) + ([string][char]0x6C14) + ([string][char]0x6570) + ([string][char]0x636E) + ([string][char]0x672A) + ([string][char]0x66F4) + ([string][char]0x65B0)
    "conjunction_to.wav" = [string][char]0x5230
    "suffix_degree.wav" = [string][char]0x5EA6
    "prefix_below_zero.wav" = ([string][char]0x96F6) + ([string][char]0x4E0B)
    "weather_sunny.wav" = [string][char]0x6674
    "weather_cloudy.wav" = ([string][char]0x591A) + ([string][char]0x4E91)
    "weather_overcast.wav" = [string][char]0x9634
    "weather_rain_snow.wav" = ([string][char]0x96E8) + ([string][char]0x96EA)
    "weather_thunder_rain.wav" = ([string][char]0x96F7) + ([string][char]0x96E8)
    "weather_rain.wav" = ([string][char]0x6709) + ([string][char]0x96E8)
    "weather_snow.wav" = ([string][char]0x6709) + ([string][char]0x96EA)
    "weather_sand_dust.wav" = ([string][char]0x6C99) + ([string][char]0x5C18)
    "weather_haze.wav" = ([string][char]0x96FE) + ([string][char]0x973E)
    "weather_unknown.wav" = ([string][char]0x672A) + ([string][char]0x77E5)
}

for ($number = 0; $number -le 10; $number++) {
    $assets[("number_{0:D2}.wav" -f $number)] = Get-ChineseNumber -Value $number
}

foreach ($entry in $assets.GetEnumerator()) {
    $targetPath = Join-Path $resolvedOutputDir $entry.Key
    $xml = Convert-ToSapiXml -Text $entry.Value
    & cscript //nologo $tempScript $targetPath $xml $VoiceIndex | Out-Null

    $trimmed = Trim-WavDataBytes -PcmData (Get-WavDataBytes -Path $targetPath)
    Write-WavFile -Path $targetPath -PcmData $trimmed

    $size = (Get-Item -LiteralPath $targetPath).Length
    if ($size -le 46) {
        throw "Generated WAV is empty: $targetPath"
    }

    Write-Host ("generated {0}" -f $entry.Key)
}
