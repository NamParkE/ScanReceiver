<#
Runs test\ftp_test.py against build\ScanReceiver.exe with a throwaway config.

  pwsh -File test\run_ftp_test.ps1 [-Port 2121] [-Exe <path to ScanReceiver.exe>]

- Refuses to run while ScanReceiver is already running (single instance: a second copy would only
  bring the user's window forward).
- The app gets "--ini <temp file>", so the user's %APPDATA%\ScanReceiver\ScanReceiver.ini is never touched.
- Receives into a temp folder that is deleted afterwards.
- The app is killed at the end, so Windows may leave a ghost tray icon until the mouse passes over it.
#>
param([int]$Port = 2121, [string]$Exe = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\ScanReceiver.exe'))

if (Get-Process ScanReceiver -ErrorAction SilentlyContinue) {
    Write-Host 'ScanReceiver가 실행 중입니다. 트레이에서 종료한 뒤 다시 실행하세요.'
    exit 2
}
if (-not (Test-Path $Exe)) {
    Write-Host "먼저 build.bat로 빌드하세요: $Exe"
    exit 2
}

$work = Join-Path ([IO.Path]::GetTempPath()) ('ScanReceiverTest_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$root = Join-Path $work 'recv'
$ini = Join-Path $work 'test.ini'
New-Item -ItemType Directory $root | Out-Null

$code = 1
$proc = $null
try {
    "[Settings]`r`nRootDir=$root`r`nPort=$Port`r`nUser=scan`r`nPassword=scan1234`r`nAutoStart=1`r`nMinimizeToTray=1" |
        Set-Content -Encoding Unicode $ini
    $proc = Start-Process $Exe -ArgumentList '--ini', "`"$ini`"" -PassThru
    Start-Sleep -Seconds 2
    python -I (Join-Path $PSScriptRoot 'ftp_test.py') $root $Port
    $code = $LASTEXITCODE
}
finally {
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id
        $proc.WaitForExit(5000) | Out-Null
    }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
exit $code
