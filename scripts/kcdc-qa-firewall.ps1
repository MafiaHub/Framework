[CmdletBinding()]
param(
    [ValidateSet('Audit','Apply')][string]$Action = 'Audit',
    [string]$RuntimeRoot,
    [string[]]$DevelopmentRoots,
    [string]$ReportDirectory
)
$ErrorActionPreference = 'Stop'
if (-not $RuntimeRoot) {
    $common = & git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the main Framework checkout.' }
    $RuntimeRoot = Join-Path (Split-Path -Parent $common) 'builds\qa-runtime'
}
$root = [IO.Path]::GetFullPath($RuntimeRoot).TrimEnd('\')
if (-not $DevelopmentRoots) { $DevelopmentRoots = @((Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $root)))) }
$development = @($DevelopmentRoots | ForEach-Object { [IO.Path]::GetFullPath($_).TrimEnd('\') + '\' })
if (-not $ReportDirectory) { $ReportDirectory = Join-Path $root ('firewall-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')) }
if (Test-Path -LiteralPath (Join-Path $ReportDirectory 'before.wfw')) { throw 'Choose a fresh report directory; never overwrite a firewall backup.' }
$null = New-Item -ItemType Directory -Force -Path $ReportDirectory
$programs = @('server\KCDCServer.exe','client-a\KCDCLauncher.exe','client-b\KCDCLauncher.exe','tests\KCDCTests.exe','tests\FrameworkTests.exe')
$programs = @($programs | ForEach-Object { Join-Path $root $_ })
$group = 'KCDC permanent QA slots'
$rulesByName = @{}
Get-NetFirewallRule -PolicyStore PersistentStore | ForEach-Object { $rulesByName[$_.Name] = $_ }
$candidates = @(Get-NetFirewallApplicationFilter -PolicyStore PersistentStore | ForEach-Object {
    $filter = $_
    $program = [Environment]::ExpandEnvironmentVariables($filter.Program)
    if (-not [IO.Path]::IsPathRooted($program)) { return }
    $inDevelopment = @($development | Where-Object { $program.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0
    $qaName = [IO.Path]::GetFileName($program) -in @('KCDCServer.exe','KCDCLauncher.exe','KCDCTests.exe','FrameworkTests.exe')
    $rule = $rulesByName[$filter.InstanceID]
    if ($rule -and $rule.Group -ne $group -and $inDevelopment -and $qaName -and -not (Test-Path -LiteralPath $program) -and $program -notin $programs) {
        [pscustomobject]@{name=$rule.Name; displayName=$rule.DisplayName; program=$program; action=[string]$rule.Action; enabled=[string]$rule.Enabled; profile=[string]$rule.Profile}
    }
})
$report = [ordered]@{action=$Action; runtimeRoot=$root; developmentRoots=$development; candidates=$candidates; slotPrograms=$programs; removed=@(); installed=@(); completed=$false; error=$null}
$reportPath = Join-Path $ReportDirectory 'report.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($Action -eq 'Audit') {
    [pscustomobject]@{candidates=$candidates.Count; report=$reportPath; slotPrograms=$programs} | ConvertTo-Json
    return
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Apply requires an elevated PowerShell. Audit needs no elevation.' }
try {
    $backup = Join-Path $ReportDirectory 'before.wfw'
    & netsh.exe advfirewall export $backup | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $backup)) { throw 'Firewall backup failed; nothing changed.' }
    # Install first, so a failed setup does not remove historical rules.
    foreach ($program in $programs) {
        foreach ($protocol in @('TCP','UDP')) {
            $relative = $program.Substring($root.Length + 1).Replace('\','-')
            $name = "KCDC-QA-$relative-$protocol"
            $existing = Get-NetFirewallRule -PolicyStore PersistentStore -Name $name -ErrorAction SilentlyContinue
            if ($existing -and $existing.Group -ne $group) { throw "Rule name is owned by another group: $name" }
            if ($existing) { Remove-NetFirewallRule -PolicyStore PersistentStore -Name $name }
            # NetSecurity rejects ::1 as a remote-address rule operand.
            $null = New-NetFirewallRule -PolicyStore PersistentStore -Name $name -DisplayName $name -Group $group -Direction Inbound -Action Allow -Enabled True -Profile Private -Program $program -Protocol $protocol -RemoteAddress LocalSubnet,127.0.0.1
            $report.installed += $name
        }
    }
    foreach ($candidate in $candidates) {
        # Recheck the path immediately before deleting its stale rules.
        if (Test-Path -LiteralPath $candidate.program) { continue }
        Remove-NetFirewallRule -PolicyStore PersistentStore -Name $candidate.name
        $report.removed += $candidate.name
    }
    $report.completed = $true
} catch {
    $report.error = $_.ToString() + "`n" + $_.ScriptStackTrace
    throw
} finally {
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}
Write-Output $reportPath
