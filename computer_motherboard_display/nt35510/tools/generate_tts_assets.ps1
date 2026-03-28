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

function Join-WavFiles {
    param(
        [string]$LeftPath,
        [string]$RightPath,
        [string]$OutputPath
    )

    $left = Trim-WavDataBytes -PcmData (Get-WavDataBytes -Path $LeftPath)
    $right = Trim-WavDataBytes -PcmData (Get-WavDataBytes -Path $RightPath)
    $joined = New-Object byte[] ($left.Length + $right.Length)
    [Array]::Copy($left, 0, $joined, 0, $left.Length)
    [Array]::Copy($right, 0, $joined, $left.Length, $right.Length)
    Write-WavFile -Path $OutputPath -PcmData $joined
}

$resolvedOutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
$tempScript = Join-Path $env:TEMP "codex_generate_tts_assets.vbs"

New-SapiVbs -Path $tempScript

$assets = [ordered]@{
    "prefix_beijing_time.wav" = ([string][char]0x5317) + ([string][char]0x4EAC) + ([string][char]0x65F6) + ([string][char]0x95F4)
    "suffix_oclock.wav" = [string][char]0x70B9
    "suffix_minute.wav" = [string][char]0x5206
}

for ($hour = 0; $hour -lt 24; $hour++) {
    $assets[("hour_{0:D2}.wav" -f $hour)] = Get-ChineseNumber -Value $hour
}

for ($minute = 0; $minute -lt 60; $minute++) {
    $assets[("minute_{0:D2}.wav" -f $minute)] = Get-ChineseNumber -Value $minute -Minute
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

for ($minute = 11; $minute -lt 60; $minute++) {
    if (($minute % 10) -eq 0) {
        continue
    }

    $tensValue = [int][Math]::Floor($minute / 10) * 10
    $onesValue = $minute % 10
    $minuteName = "minute_{0:D2}.wav" -f $minute
    $tensName = "minute_{0:D2}.wav" -f $tensValue
    $onesName = "hour_{0:D2}.wav" -f $onesValue

    Join-WavFiles `
        -LeftPath (Join-Path $resolvedOutputDir $tensName) `
        -RightPath (Join-Path $resolvedOutputDir $onesName) `
        -OutputPath (Join-Path $resolvedOutputDir $minuteName)
    Write-Host ("patched {0}" -f $minuteName)
}
