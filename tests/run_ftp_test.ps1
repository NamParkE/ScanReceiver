<#
Runs tests\ftp_test.py against build\ScanReceiver.exe with a throwaway config.

  pwsh -File tests\run_ftp_test.ps1 [-Port 2121]

- Refuses to run while ScanReceiver is already running (it would be the user's instance).
- build\ScanReceiver.ini holds the user's real settings: it is backed up and restored byte-for-byte.
- Receives into a temp folder that is deleted afterwards.
- The app is killed at the end, so Windows may leave a ghost tray icon until the mouse passes over it.
#>
param([int]$Port = 2121)

$repo = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $repo 'build\ScanReceiver.exe'
$ini = Join-Path $repo 'build\ScanReceiver.ini'
$backup = "$ini.testbak"

if (Get-Process ScanReceiver -ErrorAction SilentlyContinue) {
    Write-Host 'ScanReceiver가 실행 중입니다. 트레이에서 종료한 뒤 다시 실행하세요 (사용자 설정 보호).'
    exit 2
}
if (-not (Test-Path $exe)) {
    Write-Host "먼저 build.bat로 빌드하세요: $exe"
    exit 2
}

$root = Join-Path ([IO.Path]::GetTempPath()) ('ScanReceiverTest_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory $root | Out-Null
$hadIni = Test-Path $ini
if ($hadIni) { Copy-Item $ini $backup -Force }

$code = 1
$proc = $null
try {
    "[Settings]`r`nRootDir=$root`r`nPort=$Port`r`nUser=scan`r`nPassword=scan1234`r`nAutoStart=1`r`nMinimizeToTray=1" |
        Set-Content -Encoding Unicode $ini
    $proc = Start-Process $exe -PassThru
    Start-Sleep -Seconds 2
    python -I (Join-Path $PSScriptRoot 'ftp_test.py') $root $Port
    $code = $LASTEXITCODE
}
finally {
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id
        $proc.WaitForExit(5000) | Out-Null
    }
    if ($hadIni) { Move-Item $backup $ini -Force } elseif (Test-Path $ini) { Remove-Item $ini }
    Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}
exit $code
