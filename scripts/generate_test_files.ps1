# PowerShell script to generate synthetic test files
$sampleDir = "sample_data"
if (-not (Test-Path $sampleDir)) {
    New-Item -ItemType Directory -Path $sampleDir | Out-Null
}

Write-Host "Generating synthetic test files in ./sample_data/ ..." -ForegroundColor Green

# 1. Uniform random file (5 MB)
$rand5mb = "$sampleDir/random_5mb.bin"
$bytes = New-Object byte[] (5 * 1024 * 1024)
(New-Object Random(42)).NextBytes($bytes)
[System.IO.File]::WriteAllBytes($rand5mb, $bytes)

# 2. Duplicate file
Copy-Item $rand5mb "$sampleDir/random_5mb_dup.bin"

# 3. Near-duplicate file
$nearDup = "$sampleDir/random_5mb_near_dup.bin"
$bytesMod = [byte[]]$bytes.Clone()
for ($i = 2000000; $i -lt 2000010; $i++) {
    $bytesMod[$i] = $bytesMod[$i] -bxor 0xFF
}
[System.IO.File]::WriteAllBytes($nearDup, $bytesMod)

# 4. Pattern repeated file (8 MB)
$patternFile = "$sampleDir/repeated_pattern_8mb.bin"
$patternStr = "ChunkDedupe test pattern repeating data line 1234567890`n"
$sb = New-Object System.Text.StringBuilder
for ($i = 0; $i -lt 160000; $i++) {
    [void]$sb.Append($patternStr)
}
[System.IO.File]::WriteAllText($patternFile, $sb.ToString())

# 5. Small file (100 KB)
$small100kb = "$sampleDir/small_100kb.bin"
$smallBytes = New-Object byte[] (100 * 1024)
(New-Object Random(99)).NextBytes($smallBytes)
[System.IO.File]::WriteAllBytes($small100kb, $smallBytes)

Write-Host "Generated test files successfully:" -ForegroundColor Green
Get-ChildItem $sampleDir | Select-Object Name, Length
