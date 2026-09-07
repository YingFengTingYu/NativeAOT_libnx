param(
    [Parameter(Mandatory = $true)]
    [string]$EdenPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputRoot = Join-Path $repoRoot 'artifacts/libnx'
$emulatorRoot = Join-Path $outputRoot 'emulator'
$profileRoot = Join-Path $emulatorRoot 'user'
$sourceExecutable = (Resolve-Path -LiteralPath $EdenPath).Path
$expectedHash = '1FC704C297AA80F52EF3776C1B07077616B414C316FA0A7F2E538B32CCBB876A'
if ((Get-FileHash -LiteralPath $sourceExecutable -Algorithm SHA256).Hash -ne $expectedHash)
{
    throw '本轮固定使用 Eden v0.2.1 Windows amd64 MSVC 的 eden-cli.exe。'
}

New-Item -ItemType Directory -Force -Path $emulatorRoot | Out-Null
$executablePath = Join-Path $emulatorRoot 'eden-cli.exe'
if ($sourceExecutable -ne $executablePath)
{
    Copy-Item -LiteralPath $sourceExecutable -Destination $executablePath -Force
    $sourceRoot = Split-Path -Parent $sourceExecutable
    foreach ($licenseItem in @('LICENSE.txt', 'LICENSES'))
    {
        $licenseSource = Join-Path $sourceRoot $licenseItem
        if (Test-Path -LiteralPath $licenseSource)
        {
            Copy-Item -LiteralPath $licenseSource -Destination $emulatorRoot -Recurse -Force
        }
    }
}

foreach ($directory in @('config', 'log', 'nand', 'sdmc', 'load', 'dump', 'tas', 'screenshots', 'shader'))
{
    New-Item -ItemType Directory -Force -Path (Join-Path $profileRoot $directory) | Out-Null
}
$configPath = Join-Path $profileRoot 'config/sdl2-config.ini'
$config = @'
[Core]
use_multi_core\default=false
use_multi_core=true
[Cpu]
cpu_accuracy\default=false
cpu_accuracy=1
[Renderer]
backend\default=false
backend=2
[Debugging]
log_filter\default=false
log_filter="*:Info"
[Miscellaneous]
flush_line\default=false
flush_line=true
'@
[System.IO.File]::WriteAllText($configPath, $config, [System.Text.UTF8Encoding]::new($false))

$allPassed = $true
foreach ($mode in @('libnx', 'linux_control'))
{
    $nro = Join-Path $outputRoot "tls-probe/nativeaot-tls-$mode.nro"
    $nro = (Resolve-Path -LiteralPath $nro).Path
    $emulatorLog = Join-Path $profileRoot 'log/eden_log.txt'
    if (Test-Path -LiteralPath $emulatorLog)
    {
        Remove-Item -LiteralPath $emulatorLog
    }
    $process = Start-Process -FilePath $executablePath -WindowStyle Hidden -PassThru `
        -ArgumentList @('-g', ('"{0}"' -f $nro))
    $finished = $process.WaitForExit(45000)
    if (-not $finished)
    {
        Stop-Process -Id $process.Id
        $process.WaitForExit()
    }
    $logText = ''
    $savedLog = Join-Path $outputRoot "tls-probe/$mode-eden.log"
    if (Test-Path -LiteralPath $emulatorLog)
    {
        Copy-Item -LiteralPath $emulatorLog -Destination $savedLog -Force
        $logText = [System.IO.File]::ReadAllText($savedLog)
    }
    $observed = @([regex]::Matches($logText, '\[AOTTLS\] ([^\r\n]+)') |
        ForEach-Object { $_.Groups[1].Value })
    $required = if ($mode -eq 'libnx')
    {
        @('begin=1', 'libnx.target=1', 'main.address_and_registers=1',
          'workers.isolated=1', 'main.unchanged=1', 'pass=1')
    }
    else
    {
        @('begin=1', 'linux.control=1', 'main.address_and_registers=0', 'fail=1')
    }
    $missing = @($required | Where-Object { $_ -notin $observed })
    $passed = $finished -and $process.ExitCode -eq 0 -and $missing.Count -eq 0
    $result = [ordered]@{
        Case = $mode
        TestPassed = $passed
        ExpectedFailure = $mode -eq 'linux_control'
        ManagedRuntimeVerified = $false
        HardwareVerified = $false
        ExitCode = $process.ExitCode
        TimedOut = -not $finished
        EmulatorSha256 = $expectedHash
        NroSha256 = (Get-FileHash -LiteralPath $nro -Algorithm SHA256).Hash
        Markers = $observed
        MissingMarkers = $missing
    }
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputRoot "tls-probe/$mode-result.json") -Encoding utf8NoBOM
    Write-Output "$mode : TestPassed=$passed"
    $observed | ForEach-Object { Write-Output "  [AOTTLS] $_" }
    $allPassed = $allPassed -and $passed
}
if (-not $allPassed)
{
    throw 'TLS 模拟器对照测试未通过，检查 artifacts/libnx/tls-probe 内的日志。'
}
