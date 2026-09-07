param(
    [Parameter(Mandatory = $true)]
    [string]$EdenPath,
    [ValidateSet('Tls', 'Context', 'Memory', 'Threads', 'System', 'Crypto')]
    [string]$Suite = 'Tls'
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
$modes = if ($Suite -eq 'Tls') { @('libnx', 'linux_control') } else { @($Suite.ToLowerInvariant()) }
$probeDirectory = $Suite.ToLowerInvariant() + '-probe'
$prefix = switch ($Suite) { 'Tls' { 'AOTTLS' } 'Context' { 'AOTCTX' } 'Memory' { 'AOTMEM' } 'Threads' { 'AOTTHR' } 'System' { 'AOTSYS' } 'Crypto' { 'AOTCRYPTO' } }
foreach ($mode in $modes)
{
    $nroName = if ($Suite -eq 'Tls') { "nativeaot-tls-$mode.nro" } else { "nativeaot-$mode.nro" }
    $nro = Join-Path $outputRoot "$probeDirectory/$nroName"
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
    $savedLog = Join-Path $outputRoot "$probeDirectory/$mode-eden.log"
    if (Test-Path -LiteralPath $emulatorLog)
    {
        Copy-Item -LiteralPath $emulatorLog -Destination $savedLog -Force
        $logText = [System.IO.File]::ReadAllText($savedLog)
    }
    $observed = @([regex]::Matches($logText, ('\[' + $prefix + '\] ([^\r\n]+)')) |
        ForEach-Object { $_.Groups[1].Value })
    $required = if ($Suite -eq 'Crypto')
    {
        @('begin=1', 'errors.report_and_clear=1', 'errors.thread_local=1', 'capabilities.unsupported=1', 'pass=1')
    }
    elseif ($Suite -eq 'System')
    {
        @('begin=1', 'mapping.protection=1', 'mapping.release=1', 'monitor.timeout=1',
          'monitor.signal=1', 'time.monotonic=1', 'random.sanity=1', 'romfs.mount=1',
          'romfs.read_seek=1', 'romfs.read_only=1', 'romfs.paths_and_sd=1', 'pass=1')
    }
    elseif ($Suite -eq 'Threads')
    {
        @('begin=1', 'stack.bounds=1', 'thread.pause=1', 'thread.stopped_context=1',
          'thread.resume=1', 'thread.rendezvous=1', 'thread.exit_callback=1', 'system.info=1', 'pass=1')
    }
    elseif ($Suite -eq 'Memory')
    {
        @('begin=1', 'memory.reserve=1', 'memory.repeat_commit=1', 'memory.bounds=1',
          'memory.recommit_zero=1', 'memory.commit_rollback=1', 'memory.release=1', 'memory.checks=1',
          'event.auto_reset=1', 'event.manual_reset=1', 'events.checks=1', 'pass=1')
    }
    elseif ($Suite -eq 'Context')
    {
        @('begin=1', 'context.checks=1', 'thread.ids=1', 'pass=1')
    }
    elseif ($mode -eq 'libnx')
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
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputRoot "$probeDirectory/$mode-result.json") -Encoding utf8NoBOM
    $archiveRoot = Join-Path $outputRoot ("$probeDirectory/history/" + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Force -Path $archiveRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $outputRoot "$probeDirectory/$mode-result.json") -Destination $archiveRoot
    if (Test-Path -LiteralPath $savedLog)
    {
        Copy-Item -LiteralPath $savedLog -Destination $archiveRoot
    }
    Write-Output "$mode : TestPassed=$passed"
    $observed | ForEach-Object { Write-Output "  [$prefix] $_" }
    $allPassed = $allPassed -and $passed
}
if (-not $allPassed)
{
    throw "模拟器测试未通过，检查 artifacts/libnx/$probeDirectory 内的日志。"
}
