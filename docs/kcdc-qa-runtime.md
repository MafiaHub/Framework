# Permanent KCDC QA runtimes

Windows Firewall application rules match executable paths. Running each build
from a new worktree or evidence directory produces another first-launch prompt.
Build in the canonical build tree, but launch QA from permanent runtime slots.

`scripts/kcdc-qa-slots.ps1` resolves the main Framework checkout through Git's
common directory, even when called from a linked worktree. Its default root is
`<main Framework>/builds/qa-runtime`. Do not override it per run or worktree.
`-RuntimeRoot` exists for a deliberate machine-wide relocation and helper tests.

Slots are `server`, `client-a`, `client-b`, and `tests`. They are shared across
worktrees. Wait when another run owns a slot; do not create another executable
directory to bypass contention. Use a unique run ID, retain its lease through
testing, and keep evidence outside the slots. Stage all clients before starting
the game: staging/release conservatively refuses while any KingdomCome process
exists because game children can retain DLLs after their launcher has exited.

## One-time firewall setup

Run an audit first (no administrator required):

```powershell
& scripts/kcdc-qa-firewall.ps1 -Action Audit
```

Review the returned JSON. Cleanup selects only missing KCDCServer,
KCDCLauncher, KCDCTests and FrameworkTests executables under development roots.
By default that is the main Framework checkout's parent, including sibling
worktrees. Existing executables, installed games, unrelated programs and slot
rules are preserved. Supply explicit `-DevelopmentRoots` to include a former
development directory; never use an entire drive as a convenience wildcard.

Run the same helper with `-Action Apply` in an elevated PowerShell. It exports
the complete firewall policy to `before.wfw` before mutation, creates ten rules
(TCP/UDP for five executable paths), then removes the obsolete rules. The JSON
records successful mutations even if a later operation fails. Apply is repeatable.
Rules allow inbound traffic on Private profiles from LocalSubnet and IPv4 loopback.
They do not change profiles, default firewall policy or notification settings.
Remote QA across routed networks needs a separately reviewed address scope.

`before.wfw` is an emergency **whole-policy** backup; `netsh advfirewall import`
replaces current policy, including unrelated subsequent changes. Do not import
it casually. Preserve the report and backup when diagnosing or reverting setup.
Audit/Apply are explicit administrative maintenance, never an automatic launch
side effect. Existing block rules for live paths are reported by launch preflight
and must be reviewed; the helper does not silently delete them.

## Every QA run

Finish the canonical build with explicit `KCDC_AGENT_TESTING=ON` as appropriate.
For native test executables, build `KCDCTests`/`FrameworkTests`, stage the test
bundle, and launch it from `tests`; targets that execute directly from the build
tree bypass these slots.
Prepare complete client/server/test bundles with matching DLLs, configuration and
resources in an ignored build directory. Preparation does not launch them.

```powershell
$run = 'door-sync-20261014-01'
& scripts/kcdc-qa-slots.ps1 -Action Stage -Slot server -RunId $run -Source <server-bundle>
& scripts/kcdc-qa-slots.ps1 -Action Stage -Slot client-a -RunId $run -Source <client-a-bundle>
& scripts/kcdc-qa-slots.ps1 -Action Stage -Slot client-b -RunId $run -Source <client-b-bundle>
& scripts/kcdc-qa-slots.ps1 -Action Launch -Slot server -RunId $run -Executable KCDCServer.exe
& scripts/kcdc-qa-slots.ps1 -Action Launch -Slot client-a -RunId $run -Executable KCDCLauncher.exe -Arguments '<verified launcher arguments>'
```

Stage replaces the slot contents, rejects junctions/symlinks, records hashes and
reserves the slot for the run. Launch checks those hashes, ownership and firewall
rules. Slot files are disposable; copy logs, receipts and saves to the run's
evidence directory before restaging. Pass launch arguments in the syntax expected
by that executable; this helper does not compose game-specific connection flags.

Register the returned PID immediately using the runtime-QA skill's `runtime.ps1`.
Register game children only after confirming origin. That registry remains the
owner of process cleanup and port-release checks; a slot lease is not a process
registry. Stop owned runtimes and verify ports before releasing each lease:

```powershell
& scripts/kcdc-qa-slots.ps1 -Action Status -Slot server
& scripts/kcdc-qa-slots.ps1 -Action Release -Slot server -RunId $run
```

Release preserves files and evidence. A crashed agent's lease remains reserved;
inspect its run and processes before explicitly releasing it with that run ID.
Restore `KCDC_AGENT_TESTING=OFF` before supplying normal client builds.
