[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Stage','Launch','Release','Status')][string]$Action,
    [Parameter(Mandatory)][ValidateSet('server','client-a','client-b','tests')][string]$Slot,
    [string]$RunId,
    [string]$Source,
    [string]$Executable,
    [string]$Arguments = '',
    [string]$RuntimeRoot
)
$ErrorActionPreference = 'Stop'
if (-not $RuntimeRoot) {
    $common = & git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the main Framework checkout.' }
    $RuntimeRoot = Join-Path (Split-Path -Parent $common) 'builds\qa-runtime'
}
$root = [IO.Path]::GetFullPath($RuntimeRoot).TrimEnd('\')
$destination = Join-Path $root $Slot
$null = New-Item -ItemType Directory -Force -Path $root
$leasePath = Join-Path $root "$Slot.json"
# Serialize even the first reservation, when there is no lease file yet.
$lock = [IO.File]::Open((Join-Path $root "$Slot.lock"), 'OpenOrCreate', 'ReadWrite', 'None')
try {
    $lease = if (Test-Path -LiteralPath $leasePath) { Get-Content -Raw -LiteralPath $leasePath | ConvertFrom-Json } else { $null }
    if ($Action -eq 'Status') { $lease | ConvertTo-Json -Depth 8; return }
    if ([string]::IsNullOrWhiteSpace($RunId)) { throw 'RunId is required for mutations.' }
    if ($lease -and $lease.runId -ne $RunId) { throw "Slot belongs to run '$($lease.runId)'. Inspect its owner; do not steal its lease." }
    $ancestor = $destination
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse point is not allowed: $ancestor" }
        $ancestor = Split-Path -Parent $ancestor
    }
    $allowed = switch ($Slot) {
        'server' { @('KCDCServer.exe') }
        'tests' { @('KCDCTests.exe','FrameworkTests.exe') }
        default { @('KCDCLauncher.exe') }
    }
    if ($Action -eq 'Launch') {
        if (-not $lease -or -not $lease.ready) { throw 'Stage a complete bundle first.' }
        if (@(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($destination + '\', [StringComparison]::OrdinalIgnoreCase) }).Count) { throw 'An executable from this slot is already running.' }
        if ($Executable -notin $allowed) { throw "Allowed executable names: $allowed" }
        if (@(Get-ChildItem -LiteralPath $destination -File -Recurse).Count -ne @($lease.files).Count) { throw 'Staged artifact set changed.' }
        foreach ($file in $lease.files) {
            if ((Get-FileHash -LiteralPath (Join-Path $destination $file.name) -Algorithm SHA256).Hash -ne $file.sha256) { throw "Staged artifact changed: $($file.name)" }
        }
        $program = Join-Path $destination $Executable
        $filter = @(Get-NetFirewallApplicationFilter -PolicyStore ActiveStore | Where-Object Program -eq $program)
        $rules = @(if ($filter.Count) { $filter | Get-NetFirewallRule | Where-Object { $_.Enabled -eq 'True' -and $_.Direction -eq 'Inbound' } })
        $managed = @($rules | Where-Object { $_.Action -eq 'Allow' -and $_.Group -eq 'KCDC permanent QA slots' -and ([string]$_.Profile -match 'Private|Any') })
        $protocols = @(if ($managed.Count) { $managed | Get-NetFirewallPortFilter | Select-Object -ExpandProperty Protocol })
        if (@($rules | Where-Object Action -eq 'Block').Count -or 'TCP' -notin $protocols -or 'UDP' -notin $protocols) { throw 'Run kcdc-qa-firewall.ps1 -Action Apply as administrator before launching.' }
        $launch = @{FilePath=$program; WorkingDirectory=$destination; PassThru=$true; WindowStyle='Hidden'}
        if ($Arguments) { $launch.ArgumentList = $Arguments }
        $process = Start-Process @launch
        [pscustomobject]@{processId=$process.Id; path=$program; runId=$RunId; slot=$Slot} | ConvertTo-Json
        return
    }
    # A game child loads DLLs from the client slot even after its launcher exits.
    $busy = @(Get-CimInstance Win32_Process | Where-Object {
        ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($destination + '\', [StringComparison]::OrdinalIgnoreCase)) -or
        ($Slot -like 'client-*' -and $_.Name -eq 'KingdomCome.exe')
    })
    if ($busy.Count) { throw "Slot may be in use (PIDs $($busy.ProcessId -join ', ')); stop owned runtimes first." }
    if ($Action -eq 'Release') {
        if ($lease) { Remove-Item -LiteralPath $leasePath }
        return
    }
    if (-not $Source) { throw 'Source must be a complete, prepared runtime bundle.' }
    $sourcePath = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\')
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) { throw 'Source must be a directory.' }
    if ($sourcePath.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) -or $root.StartsWith($sourcePath + '\', [StringComparison]::OrdinalIgnoreCase) -or $sourcePath -eq $root) { throw 'Source and runtime root must not overlap.' }
    if (-not @($allowed | Where-Object { Test-Path -LiteralPath (Join-Path $sourcePath $_) -PathType Leaf }).Count) { throw "Bundle must contain $allowed" }
    $sourceItems = @(Get-ChildItem -LiteralPath $sourcePath -Recurse -Force)
    if ((Get-Item -LiteralPath $sourcePath).Attributes -band [IO.FileAttributes]::ReparsePoint -or @($sourceItems | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Stage real files, not junctions or symlinks.' }
    $lease = [ordered]@{runId=$RunId; source=$sourcePath; stagedUtc=[DateTime]::UtcNow.ToString('o'); ready=$false; files=@()}
    $lease | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $leasePath -Encoding UTF8
    # Destructive replacement is confined to this validated, reserved slot.
    if ([IO.Path]::GetFullPath($destination) -ne (Join-Path $root $Slot)) { throw 'Invalid destination.' }
    if (Test-Path -LiteralPath $destination) {
        if (@(Get-ChildItem -LiteralPath $destination -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Destination contains a reparse point.' }
        Get-ChildItem -LiteralPath $destination -Force | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
    } else { $null = New-Item -ItemType Directory -Path $destination }
    Get-ChildItem -LiteralPath $sourcePath -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $destination -Recurse -Force }
    $lease.files = @(Get-ChildItem -LiteralPath $destination -File -Recurse | ForEach-Object {
        [pscustomobject]@{name=$_.FullName.Substring($destination.Length + 1); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
    })
    $lease.ready = $true
    $lease | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $leasePath -Encoding UTF8
    Write-Output $destination
} finally { $lock.Dispose() }
