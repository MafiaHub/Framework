# The CEF subprocess

Every Chromium child process a client spawns (renderer, GPU, utility,
network) runs `cef_subprocess.exe`. It is one Framework target shared by every
project. It ships next to the client DLL, and every launch puts a fresh copy
of it on players' machines. Antivirus engines therefore see it more than any
other file we ship, so this page covers what it does and the rules that keep
it from being flagged.

## How it is launched

`Framework::GUI::Manager` initialises CEF with
`settings.browser_subprocess_path` pointing at `cef_subprocess.exe` in the
directory of the **owning module** (the injected client DLL), not the
process exe. The game process is the browser process; every child is a
`cef_subprocess.exe`. The sandbox is off (`no_sandbox = true`), which is what
makes a separate subprocess exe possible at all. See "Signing" for why the
sandboxed layout does not fit us.

The exe is three translation units:

| file | role |
| --- | --- |
| `gui/cef/subprocess_main.cpp` | entry point: parent watch, then `CefExecuteProcess` |
| `gui/cef/renderer_app.cpp` | `RendererApp`, the renderer side of the JS bindings (`callEvent`) |
| `gui/resources/scheme.cpp` | custom scheme registration |

Every process in a CEF instance must register the identical set of custom
schemes, which is why `scheme.cpp` is compiled into both the client and the
subprocess, and why the scheme name is framework-wide (see
`local_resource_scheme.md`). A project cannot have its own subprocess without
also having its own scheme set.

## Parent watch

If the game dies without reaching `CefShutdown` (a crash, a kill from Task
Manager), Chromium's children are not reliably torn down. CEF still has no
built-in fix for this ([chromiumembedded/cef#3614][cef-3614]). The subprocess
therefore waits on the game process and calls `ExitProcess(0)` when it ends.

The browser process names itself. `App::OnBeforeChildProcessLaunch` appends
`--fw-parent-pid=<pid>` (`kParentProcessSwitch` in `renderer_app.h`) to every
child's command line. The child reads it with `CefCommandLine`, opens the
process with `SYNCHRONIZE` only, and waits on it from a thread.

The PID is deliberately **not** read from the process's own kernel block.
The helper used to resolve `NtQueryInformationProcess` through
`GetProcAddress` and read `PROCESS_BASIC_INFORMATION::InheritedFromUniqueProcessId`.
That combination, together with an unsigned, windowless exe launched by an
injected DLL, got the release build quarantined by Windows Defender as a
trojan.

[cef-3614]: https://github.com/chromiumembedded/cef/issues/3614

## Rules for changing it

Everything added to this exe is judged by heuristic scanners on a binary with
no reputation. Keep it boring:

- **Documented Win32 only.** No `ntdll` exports, no `GetProcAddress` for
  system functions, no undocumented structures (`PEB`, `TEB`, the `Reserved*`
  fields of `winternl.h`).
- **No process or memory access beyond `SYNCHRONIZE` on the parent.** Nothing
  that opens other processes, reads or writes their memory, enumerates
  processes or modules, or adjusts token privileges.
- **No anti-debug or anti-tamper.** The client's string encryption and `/GR-`
  are for the client DLL. Do not carry them over here; an obfuscated helper
  looks like a dropper.
- **Data reaches the child on its command line.** If a child needs something
  from the browser process, add a switch in `OnBeforeChildProcessLaunch`.
  Never have the child discover it by inspecting the system.
- **No network, file or registry access of its own.** Chromium does its own
  I/O; the helper's code should do none.

## Product identity

`code/framework/CMakeLists.txt` calls `fw_set_binary_identity()` on the
target, so the exe carries a `VERSIONINFO` with company `MafiaHub`, product
`MafiaHub Framework`, description `MafiaHub Framework Web Helper`, and the
version from the Framework `VERSION` file.

It carries the Framework's identity, not a project's, because every
project in a configure shares the one target. A second
`fw_set_binary_identity(cef_subprocess ...)` from a project would add a
second version resource and fail the link. A project that needs its own
branding on the helper needs its own target, and with it its own copy of the
scheme set (see above).

The linker's default manifest (`asInvoker`) is embedded; no custom manifest
is needed.

## Signing

None of this replaces a code signature. That is the one thing that reliably
keeps Defender and SmartScreen quiet.

- **No signed prebuilt exists.** The helper runs our `RendererApp` and our
  scheme set, so nobody else's binary can stand in for it. The CEF binary
  distribution is unsigned as well: `libcef.dll`, `chrome_elf.dll`,
  `bootstrap.exe` and `bootstrapc.exe` in `vendors/cef/.../Release` all
  report `NotSigned`. Signing is the embedder's job.
- **What to sign** in a release: `cef_subprocess.exe`, `libcef.dll`,
  `chrome_elf.dll` and the project's own launcher and client DLL, all with
  the same certificate.
- **The CEF bootstrap does not apply.** Since M138 CEF ships
  `bootstrap.exe`/`bootstrapc.exe` for sandboxed apps. They load the
  application as a DLL and require `chrome_elf.dll` to be signed with the
  same certificate as the exe. Our client is injected into a game rather than
  loaded by a CEF exe, so the bootstrap layout has nowhere to go. We stay on
  the separate-subprocess layout, which rules out the sandbox.

## When it is flagged anyway

1. Get the detection name from Windows Security's protection history, or from
   the Defender operational event log (events 1116/1117). A `!ml` suffix
   (e.g. `Trojan:Win32/Wacatac.B!ml`) is a machine-learning verdict on the
   binary's shape. A named signature is a match on specific bytes.
2. Submit the exact released file as a false positive at
   <https://www.microsoft.com/wdsi/filesubmission>, as "software developer".
   Definitions usually clear it within one to three days. This needs doing per
   release while the binaries are unsigned, since each build is a new hash.
3. For an `!ml` verdict, check the latest changes to the helper against the
   rules above before blaming the scanner.
