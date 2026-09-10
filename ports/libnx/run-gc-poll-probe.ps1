param(
    [Parameter(Mandatory = $true)][string]$EdenPath,
    [string]$ProbeDirectory,
    [switch]$NegativeControl
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$ProbeDirectory) { $ProbeDirectory = Join-Path $repoRoot 'artifacts/libnx/gc-poll-probe' }
$probeRoot = (Resolve-Path -LiteralPath $ProbeDirectory).Path
$nro = (Resolve-Path -LiteralPath (Join-Path $probeRoot 'managed-probe.nro')).Path
$sourceExecutable = (Resolve-Path -LiteralPath $EdenPath).Path
$expectedHash = '1FC704C297AA80F52EF3776C1B07077616B414C316FA0A7F2E538B32CCBB876A'
if ((Get-FileHash -LiteralPath $sourceExecutable).Hash -ne $expectedHash) { throw '需要固定版本 Eden v0.2.1 eden-cli.exe。' }
$emulatorRoot = Join-Path $repoRoot 'artifacts/libnx/gc-poll-emulator'
New-Item -ItemType Directory -Force -Path $emulatorRoot | Out-Null
$executable = Join-Path $emulatorRoot 'eden-cli.exe'
if ($sourceExecutable -ne $executable) { Copy-Item -LiteralPath $sourceExecutable -Destination $executable -Force }
foreach ($license in @('LICENSE.txt', 'LICENSES'))
{
    $licensePath = Join-Path (Split-Path -Parent $sourceExecutable) $license
    if ($sourceExecutable -ne $executable -and (Test-Path -LiteralPath $licensePath))
    {
        Copy-Item -LiteralPath $licensePath -Destination $emulatorRoot -Recurse -Force
    }
}
foreach ($directory in @('config','log','nand','sdmc','load','dump','tas','screenshots','shader'))
{
    New-Item -ItemType Directory -Force -Path (Join-Path $emulatorRoot "user/$directory") | Out-Null
}
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
[IO.File]::WriteAllText((Join-Path $emulatorRoot 'user/config/sdl2-config.ini'), $config, [Text.UTF8Encoding]::new($false))
$logPath = Join-Path $emulatorRoot 'user/log/eden_log.txt'
if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath }
$process = Start-Process -FilePath $executable -WindowStyle Hidden -PassThru -ArgumentList @('-g', ('"{0}"' -f $nro))
$finished = $process.WaitForExit(45000)
if (!$finished)
{
    Stop-Process -Id $process.Id
    $process.WaitForExit()
}
$log = if (Test-Path -LiteralPath $logPath) { [IO.File]::ReadAllText($logPath) } else { '' }
[IO.File]::WriteAllText((Join-Path $probeRoot 'eden.log'), $log, [Text.UTF8Encoding]::new($false))
$markers = @([regex]::Matches($log, '\[AOTGCPOLL\] ([^\r\n]+)') | ForEach-Object { $_.Groups[1].Value })
$required = if ($NegativeControl) { @('stage.10=0', 'stage.80=1', 'stage.100=10') } else
{
    @('stage.10=1','stage.12=1','stage.14=1','stage.15=1','stage.16=1','stage.18=1','stage.20=1',
      'stage.40=1','stage.42=1','stage.44=1','stage.45=1','stage.46=1','stage.48=1','stage.50=1',
      'stage.70=64','stage.71=64','stage.100=0',
      'stage.80=0','stage.82=0','stage.84=0','stage.85=0','stage.86=0','stage.88=0','stage.90=0')
}
$missing = @($required | Where-Object { $_ -notin $markers })
$passed = $finished -and $process.ExitCode -eq 0 -and $missing.Count -eq 0
$result = [ordered]@{
    Passed = $passed
    NegativeControl = [bool]$NegativeControl
    HardwareVerified = $false
    EmulatorExited = $finished
    EmulatorExitCode = $process.ExitCode
    NroSha256 = (Get-FileHash -LiteralPath $nro).Hash
    Markers = $markers
    Missing = $missing
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $probeRoot 'result.json') -Encoding utf8NoBOM
$result | ConvertTo-Json -Depth 5
if (!$passed) { throw 'GC 检查点探针未通过，详见 eden.log 和 result.json。' }
