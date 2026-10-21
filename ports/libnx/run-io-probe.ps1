param(
    [Parameter(Mandatory = $true)][string]$EdenPath,
    [string]$ProbeDirectory,
    [string]$RunName = 'baseline'
)

$ErrorActionPreference = 'Stop'
if ($RunName -notmatch '^[a-zA-Z0-9_-]+$') { throw 'RunName 只能包含字母、数字、下划线和连字符。' }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$ProbeDirectory) { $ProbeDirectory = Join-Path $repoRoot 'artifacts/libnx/io-probe' }
$probeRoot = (Resolve-Path -LiteralPath $ProbeDirectory).Path
$nro = (Resolve-Path -LiteralPath (Join-Path $probeRoot 'nativeaot-io.nro')).Path
$sourceExecutable = (Resolve-Path -LiteralPath $EdenPath).Path
$expectedHash = '1FC704C297AA80F52EF3776C1B07077616B414C316FA0A7F2E538B32CCBB876A'
if ((Get-FileHash -LiteralPath $sourceExecutable).Hash -ne $expectedHash) { throw '需要固定版本 Eden v0.2.1 eden-cli.exe。' }
$emulatorRoot = Join-Path $repoRoot 'artifacts/libnx/io-emulator'
$resultRoot = Join-Path $probeRoot $RunName
if (Test-Path -LiteralPath $resultRoot) { throw '结果目录已存在，请选择新的 RunName 以保留原始证据。' }
New-Item -ItemType Directory -Force -Path $emulatorRoot, $resultRoot | Out-Null
$executable = Join-Path $emulatorRoot 'eden-cli.exe'
Copy-Item -LiteralPath $sourceExecutable -Destination $executable -Force
foreach ($license in @('LICENSE.txt', 'LICENSES'))
{
    $licensePath = Join-Path (Split-Path -Parent $sourceExecutable) $license
    if (Test-Path -LiteralPath $licensePath) { Copy-Item -LiteralPath $licensePath -Destination $emulatorRoot -Recurse -Force }
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
Copy-Item -LiteralPath $nro -Destination $resultRoot
$process = Start-Process -FilePath $executable -WindowStyle Hidden -PassThru -ArgumentList @('-g', ('"{0}"' -f $nro))
$finished = $process.WaitForExit(45000)
if (!$finished)
{
    Stop-Process -Id $process.Id
    $process.WaitForExit()
}
$log = if (Test-Path -LiteralPath $logPath) { [IO.File]::ReadAllText($logPath) } else { '' }
[IO.File]::WriteAllText((Join-Path $resultRoot 'eden.log'), $log, [Text.UTF8Encoding]::new($false))
$markers = @([regex]::Matches($log, '\[AOTIO\] ([^\r\n]+)') | ForEach-Object { $_.Groups[1].Value })
$required = @('begin=1','sd.heap.pass=1','sd.mapped.pass=1','romfs.heap.pass=1','romfs.mapped.pass=1','cleanup=1','pass=1')
$missing = @($required | Where-Object { $_ -notin $markers })
$passed = $finished -and $process.ExitCode -eq 0 -and $missing.Count -eq 0
$result = [ordered]@{
    Passed = $passed
    HardwareVerified = $false
    EmulatorExited = $finished
    EmulatorExitCode = $process.ExitCode
    EmulatorSha256 = (Get-FileHash -LiteralPath $executable).Hash
    NroSha256 = (Get-FileHash -LiteralPath $nro).Hash
    Markers = $markers
    Missing = $missing
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $resultRoot 'result.json') -Encoding utf8NoBOM
$result | ConvertTo-Json -Depth 5
if (!$passed) { throw '文件读取探针未通过，详见 eden.log 和 result.json。' }
