$ErrorActionPreference = 'Stop'
$helper = Join-Path $PSScriptRoot 'kcdc-qa-slots.ps1'
$common = & git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir
if ($LASTEXITCODE -ne 0) { throw 'Git root unavailable.' }
$fixture = Join-Path (Split-Path -Parent $common) ('builds\qa-slot-tests\' + [Guid]::NewGuid().ToString('N'))
$bundle = Join-Path $fixture 'bundle'
$runtime = Join-Path $fixture 'runtime'
$null = New-Item -ItemType Directory -Force -Path $bundle
Set-Content -LiteralPath (Join-Path $bundle 'KCDCTests.exe') -Value 'fixture, never executed'
$options = @{Slot='tests'; RuntimeRoot=$runtime}
function Expect-Failure([scriptblock]$Operation, [string]$Message) {
    try { & $Operation } catch { if ($_.Exception.Message -like "*$Message*") { return }; throw }
    throw "Expected failure containing '$Message'"
}
& $helper @options -Action Stage -RunId first -Source $bundle | Out-Null
$lease = Get-Content -Raw -LiteralPath (Join-Path $runtime 'tests.json') | ConvertFrom-Json
if (-not $lease.ready -or $lease.files.Count -ne 1) { throw 'Stage did not record a ready artifact manifest.' }
Expect-Failure { & $helper @options -Action Stage -RunId second -Source $bundle } 'belongs to run'
Expect-Failure { & $helper @options -Action Release -RunId second } 'belongs to run'
Set-Content -LiteralPath (Join-Path $runtime 'tests\KCDCTests.exe') -Value 'tampered'
Expect-Failure { & $helper @options -Action Launch -RunId first -Executable KCDCTests.exe } 'artifact changed'
Set-Content -LiteralPath (Join-Path $runtime 'tests\stale.dll') -Value 'obsolete'
Expect-Failure { & $helper @options -Action Launch -RunId first -Executable KCDCTests.exe } 'artifact set changed'
& $helper @options -Action Stage -RunId first -Source $bundle | Out-Null
if (Test-Path -LiteralPath (Join-Path $runtime 'tests\stale.dll')) { throw 'Restage retained an obsolete file.' }
Expect-Failure { & $helper @options -Action Stage -RunId first -Source $runtime } 'must not overlap'
& $helper @options -Action Release -RunId first
if (Test-Path -LiteralPath (Join-Path $runtime 'tests.json')) { throw 'Release retained lease.' }
if (-not (Test-Path -LiteralPath (Join-Path $runtime 'tests\KCDCTests.exe'))) { throw 'Release deleted artifacts.' }
& $helper @options -Action Stage -RunId second -Source $bundle | Out-Null
& $helper @options -Action Release -RunId second
# Audit selection is tested with synthetic rules; no real firewall mutation.
function Get-NetFirewallRule {
    param($PolicyStore)
    foreach ($name in @('stale','live','outside','unrelated','managed')) {
        [pscustomobject]@{Name=$name; DisplayName=$name; Group=$(if ($name -eq 'managed') {'KCDC permanent QA slots'} else {''}); Action='Allow'; Enabled='True'; Profile='Private'}
    }
}
function Get-NetFirewallApplicationFilter {
    param($PolicyStore)
    [pscustomobject]@{InstanceID='stale'; Program=(Join-Path $fixture 'old\KCDCServer.exe')}
    [pscustomobject]@{InstanceID='live'; Program=(Join-Path $bundle 'KCDCTests.exe')}
    [pscustomobject]@{InstanceID='outside'; Program=(Join-Path (Split-Path -Parent $fixture) 'outside\KCDCServer.exe')}
    [pscustomobject]@{InstanceID='unrelated'; Program=(Join-Path $fixture 'old\Other.exe')}
    [pscustomobject]@{InstanceID='managed'; Program=(Join-Path $runtime 'server\KCDCServer.exe')}
}
$auditPath = Join-Path $fixture 'audit'
& (Join-Path $PSScriptRoot 'kcdc-qa-firewall.ps1') -Action Audit -RuntimeRoot $runtime -DevelopmentRoots $fixture -ReportDirectory $auditPath | Out-Null
$audit = Get-Content -Raw (Join-Path $auditPath 'report.json') | ConvertFrom-Json
if (@($audit.candidates).Count -ne 1 -or $audit.candidates[0].name -ne 'stale') { throw 'Firewall audit selected live, unrelated, external or managed rules.' }
Write-Output "PASS: staging, ownership, tamper rejection, stale-file cleanup, overlap rejection, release/reuse and firewall audit scope. Fixtures: $fixture"
