# Samples the machine's load while harness games run (tools/game_load.py starts it; WB13).
# One JSON line per sample (about every 2 s) to -Out: the GPU (nvidia-smi: memory used, utilisation, power,
# temperature, clock, performance state), the adapter's dedicated memory, total CPU, free RAM, and for every
# game process (ei_standalone.exe, game.exe; started after -Since, so an old leftover game is left out) its
# private bytes, working set, CPU seconds so far, dedicated GPU memory and 3D engine utilisation.
# Read-only: it reads counters and process lists; it stops when -Stop exists or after -MaxSeconds.
param([string]$Out, [string]$Stop, [int]$MaxSeconds = 1800, [string]$Since = '')

$ErrorActionPreference = 'SilentlyContinue'
$since = if ($Since) { [datetime]::Parse($Since) } else { (Get-Date).AddMinutes(-1) }
$started = Get-Date
$paths = '\GPU Process Memory(*)\Dedicated Usage', '\GPU Engine(*engtype_3D)\Utilization Percentage',
         '\GPU Adapter Memory(*)\Dedicated Usage', '\Processor(_Total)\% Processor Time', '\Memory\Available MBytes'
while (-not (Test-Path $Stop) -and ((Get-Date) - $started).TotalSeconds -lt $MaxSeconds) {
    $s = Get-Counter -Counter $paths -SampleInterval 1 -MaxSamples 1
    $t = [DateTimeOffset]::Now.ToUnixTimeMilliseconds() / 1000.0
    $gpuMem = @{}; $gpu3d = @{}; $adapter = 0.0; $cpu = $null; $avail = $null
    foreach ($c in $s.CounterSamples) {
        $p = $c.Path
        if ($p -like '*gpu process memory(pid_*') {
            $id = [int]($p -replace '.*\(pid_(\d+)_.*', '$1'); $gpuMem[$id] = $gpuMem[$id] + $c.CookedValue
        } elseif ($p -like '*gpu engine(pid_*') {
            $id = [int]($p -replace '.*\(pid_(\d+)_.*', '$1'); $gpu3d[$id] = $gpu3d[$id] + $c.CookedValue
        } elseif ($p -like '*gpu adapter memory*') { $adapter += $c.CookedValue }
        elseif ($p -like '*processor(_total)*') { $cpu = $c.CookedValue }
        elseif ($p -like '*available mbytes') { $avail = $c.CookedValue }
    }
    $games = @(Get-Process -Name ei_standalone, game | Where-Object { $_.StartTime -gt $since } | ForEach-Object {
        [ordered]@{ pid = $_.Id; exe = $_.ProcessName; priv_mb = [math]::Round($_.PrivateMemorySize64 / 1MB, 1)
                    ws_mb = [math]::Round($_.WorkingSet64 / 1MB, 1); cpu_s = $_.TotalProcessorTime.TotalSeconds
                    gpu_mb = [math]::Round(($gpuMem[$_.Id] + 0) / 1MB, 1); gpu_3d = [math]::Round(($gpu3d[$_.Id] + 0), 2) } })
    $nv = (nvidia-smi --query-gpu=memory.used,utilization.gpu,power.draw,temperature.gpu,clocks.gr,pstate --format=csv,noheader,nounits) -split ',\s*'
    $line = [ordered]@{ t = $t; cpu = [math]::Round($cpu, 1); avail_mb = $avail; adapter_mb = [math]::Round($adapter / 1MB, 1)
                        nv_mem = [double]$nv[0]; nv_util = [double]$nv[1]; nv_w = [double]$nv[2]; nv_temp = [double]$nv[3]
                        nv_clock = [double]$nv[4]; nv_pstate = "$($nv[5])".Trim(); games = $games }
    ($line | ConvertTo-Json -Compress -Depth 4) | Out-File -FilePath $Out -Append -Encoding utf8
}
