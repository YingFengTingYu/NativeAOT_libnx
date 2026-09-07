param([int]$Port = 24680, [ValidateRange(10, 600)][int]$LifetimeSeconds = 180)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$executable = Join-Path $repoRoot 'artifacts/libnx/emulator/eden-cli.exe'
$nro = Join-Path $repoRoot 'artifacts/libnx/managed-probe/managed-probe.nro'
$process = Start-Process -FilePath $executable -WindowStyle Hidden -PassThru `
    -ArgumentList @('-d', "$Port", '-g', ('"{0}"' -f $nro))
Write-Output "Eden GDB port=$Port PID=$($process.Id)"
try
{
    for ($elapsed = 0; $elapsed -lt $LifetimeSeconds; $elapsed += 10)
    {
        if ($process.WaitForExit(10000))
        {
            break
        }
    }
}
finally
{
    if (-not $process.HasExited)
    {
        Stop-Process -Id $process.Id
    }
}
