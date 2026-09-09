param(
    [Parameter(Mandatory = $true)]
    [string]$EdenPath,
    [ValidateSet('Tls', 'Context', 'Memory', 'Threads', 'System', 'Crypto', 'Network', 'NetworkManaged', 'SslStream', 'SocketPeek')]
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

if ($Suite -eq 'SslStream')
{
    $bundle = Join-Path $outputRoot 'sslstream/cert.pem'
    $trust = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'openssl/ca-bundle.lock.json') -Raw | ConvertFrom-Json
    if ((Get-FileHash -LiteralPath $bundle -Algorithm SHA256).Hash -ne $trust.sha256) { throw 'CA bundle checksum mismatch.' }
    $trustDirectory = Join-Path $profileRoot 'sdmc/dotnet/ssl'
    New-Item -ItemType Directory -Force -Path $trustDirectory | Out-Null
    Copy-Item -LiteralPath $bundle -Destination (Join-Path $trustDirectory 'cert.pem') -Force
}

$allPassed = $true
$modes = if ($Suite -eq 'Tls') { @('libnx', 'linux_control') } else { @($Suite.ToLowerInvariant()) }
$probeDirectory = if ($Suite -eq 'NetworkManaged') { 'network-managed' } elseif ($Suite -eq 'SslStream') { 'sslstream' } else { $Suite.ToLowerInvariant() + '-probe' }
$prefix = switch ($Suite) { 'Tls' { 'AOTTLS' } 'Context' { 'AOTCTX' } 'Memory' { 'AOTMEM' } 'Threads' { 'AOTTHR' } 'System' { 'AOTSYS' } 'Crypto' { 'AOTCRYPTO' } 'Network' { 'AOTNET' } 'NetworkManaged' { 'AOTMANET' } 'SslStream' { 'AOTSSL' } 'SocketPeek' { 'AOTPEEK' } }
foreach ($mode in $modes)
{
    $nroName = if ($Suite -eq 'Tls') { "nativeaot-tls-$mode.nro" } elseif ($Suite -in @('NetworkManaged', 'SslStream')) { 'managed-probe.nro' } else { "nativeaot-$mode.nro" }
    $nro = Join-Path $outputRoot "$probeDirectory/$nroName"
    $nro = (Resolve-Path -LiteralPath $nro).Path
    $emulatorLog = Join-Path $profileRoot 'log/eden_log.txt'
    if (Test-Path -LiteralPath $emulatorLog)
    {
        Remove-Item -LiteralPath $emulatorLog
    }
    $process = Start-Process -FilePath $executablePath -WindowStyle Hidden -PassThru `
        -ArgumentList @('-g', ('"{0}"' -f $nro))
    $actual = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    if ($actual -and $actual.ExecutablePath -ne $executablePath) { throw 'Unexpected emulator executable.' }
    $limitSeconds = if ($Suite -eq 'SslStream') { 120 } else { 45 }
    $waitTimer = [Diagnostics.Stopwatch]::StartNew()
    do { $finished = $process.WaitForExit(1000) } while (-not $finished -and $waitTimer.Elapsed.TotalSeconds -lt $limitSeconds)
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
    $required = if ($Suite -eq 'SocketPeek')
    {
        @('begin=1', 'peek.count=1', 'peek.byte=22', 'read.count=4', 'read.first=22', 'peek.preserves_data=1', 'pass=1')
    }
    elseif ($Suite -eq 'SslStream')
    {
        @('socket.init=0', 'begin=1', 'crypto.certificate=1',
          'memory.Tls12.fragment_alpn_cancel_close=1', 'memory.Tls13.fragment_alpn_cancel_close=1',
          'reject.RemoteCertificateNameMismatch=1', 'reject.RemoteCertificateChainErrors=1',
          'handshake.cancel=1', 'https.local=1', 'wss.binary_close=1',
          'https.public_default_trust=1', 'https.reject.self-signed.badssl.com=1',
          'https.reject.wrong.host.badssl.com=1', 'https.reject.expired.badssl.com=1',
          'native.quiesce=1', 'pass=1')
    }
    elseif ($Suite -eq 'NetworkManaged')
    {
        @('socket.init=0', 'dns=1', 'tcp.async_rearm=64', 'receive.cancel=1', 'receive.after_cancel=1',
          'receive.dispose=1', 'udp.async=16', 'udp.packet_info_fallback=1', 'http.get=1', 'websocket.roundtrip_close=1',
          'interfaces.native=1', 'connect.refused=1', 'native.quiesce=1', 'pass=1')
    }
    elseif ($Suite -eq 'Network')
    {
        @('begin=1', 'dns.error=0', 'tcp.roundtrip=1', 'async.rearm_cleanup=1', 'udp.roundtrip=1', 'pass=1')
    }
    elseif ($Suite -eq 'Crypto')
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
        ManagedRuntimeVerified = $passed -and $Suite -in @('NetworkManaged', 'SslStream')
        HardwareVerified = $false
        ExitCode = $process.ExitCode
        TimedOut = -not $finished
        EmulatorSha256 = $expectedHash
        NroSha256 = (Get-FileHash -LiteralPath $nro -Algorithm SHA256).Hash
        Markers = $observed
        MissingMarkers = $missing
    }
    if ($Suite -eq 'NetworkManaged')
    {
        $result.Scope = 'IPv4 DNS/TCP/UDP unicast/HTTP/ws/async cancellation and cleanup'
        $result.MulticastVerified = 'udp.multicast=1' -in $observed
        $result.PacketInformationVerified = 'udp.packet_info=1' -in $observed
        $result.BclNetworkInterfacesVerified = 'bcl_interfaces.available=1' -in $observed
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
