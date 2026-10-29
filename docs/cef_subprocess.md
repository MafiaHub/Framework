# The CEF subprocess

Every Chromium child process a client spawns (renderer, GPU, the storage and
audio utilities) runs `cef_subprocess.exe`. It is one Framework target shared
by every project. The network service is the exception: it runs inside the
game process (see below). It ships next to the client DLL, and every launch puts a fresh copy
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

## The network service stays in the game process

`App::OnBeforeCommandLineProcessing` enables Chromium's
`NetworkServiceInProcess2` feature, so the network service (DNS, HTTP, TLS,
cookies, the disk cache) runs on threads of the browser process instead of in
a `cef_subprocess.exe` of its own. Two things single that helper out when it
is a process:

- It is the one unsandboxed child Chromium assigns to a kill-on-close job
  (`sandbox/policy/win/sandbox_win.cc`, `LaunchWithoutSandbox`, gated by
  `UtilitySandboxedProcessLauncherDelegate::ShouldUnsandboxedRunInJob`). A
  failed assignment terminates the child with exit code 1 before it runs a
  line, and nothing is logged in a release build (`DPLOG`).
- It is the one helper that opens sockets, which is what security software
  judges an unsigned, windowless exe on.

Shipped builds have shown exactly that shape: the network service exits as it
launches and Chromium restarts it every 27 ms for the whole session
(`Network service crashed or was terminated, restarting service.` thousands
of times in `cef.log`), the menu stays blank, and the renderer and GPU
helpers run fine. The trigger is environmental and was never reproduced on a
developer machine: launching the same shipped build from a terminal, from
Explorer with the mark of the web (inside Explorer's `BREAKAWAY_OK` job), or
inside a synthetic job all worked. In-process there is nothing to launch,
kill or police, so the failure mode is gone whatever triggers it. The cost is
that a crash in the network stack now takes the game with it, which for a UI
that serves local resources is the right trade.

`Manager::Init` logs, at debug level, whether the game process runs inside a
job and that job's limit flags, so a report of the blank menu shows the
host's setup.

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

## Remote mode: CEF outside the game

A 32-bit game cannot spare the address space Chromium takes, so a client built
for 32 bits (`FW_CEF_REMOTE`, on by default there) runs the browser process in
a 64-bit host instead: `cef64/cef_host.exe`, with its own 64-bit
`cef_subprocess.exe` and CEF runtime beside it. `cmake/FrameworkCefHost.cmake`
builds it inside the client's tree as a project of its own
(`code/framework/cef_host`) and stages it in `bin/cef64`. The game process
links `libcef.dll` delay-loaded and never touches it in this mode.
`FW_CEF_MODE=remote|inprocess` overrides the build's choice at run time, and a
host that cannot start (no `cef64`, a 32-bit Wine prefix) falls back to CEF in
the game process when the 32-bit runtime ships alongside.

The host shares the in-process code: `gui/cef` (handlers, Wine switches, the
settings and profile handling in `runtime.cpp`) and `gui/resources`. The
client keeps `Manager` and `View` as they are; `gui/remote/remote_view.h` is
the `View` it creates, so scripts and mods see no difference.

- **Launch.** `RemoteSession` starts the host with `--fw-pipe`, `--fw-token`
  (128 random bits naming the pipe and every frame section),
  `--fw-host-parent` and `--fw-root`, in a kill-on-close job. The pipe's DACL
  admits only the client's own user and no remote client, and the client
  refuses any peer but the process it started (`GetNamedPipeClientProcessId`).
  `Manager::Init` waits at most three seconds, for a host that fails at once;
  CEF comes up after that, and views created before it are created on the
  host when it reports ready. The host watches the client's process like the
  subprocess watches its parent, and exits when its pipe breaks.
- **Pump.** The host runs CEF with `external_message_pump`: its main thread
  sleeps until CEF asks for work (`OnScheduleMessagePumpWork`) or the client
  sends something, and the client's begin-frame message, one per on-screen
  view per game tick, sets the paint cadence.
- **Messages** (`gui/remote/protocol.h`) go over one overlapped named pipe:
  view lifecycle, URL, origin lock, focus, mute, scripts, begin frames and
  input one way; view events, `callEvent` payloads and frame surfaces the
  other. Each side treats the other as untrusted: lengths are bounds-checked,
  enums and sizes range-checked, a malformed message ends the session. What
  the host sends the 32-bit client is capped at 1 MB a message; the host drops
  a larger view event or `callEvent` payload with a warning.
- **fw://** roots stay registered in the client. The host forwards each
  request; the client answers from its own providers, virtual filesystem
  included, on three workers that take turns over every open request a chunk
  at a time. Each request runs at most a 1 MB window ahead of what the host
  has read and acknowledged, and a range request's skip is sent to the client
  as a seek rather than read through.
- **Frames** (`gui/remote/frame_buffer.h`) are a shared section per view and
  size: a header and three BGRA slots. The host refreshes a slot the client is
  not reading with what changed since that slot was written, then publishes
  it; the client copies only the union of the dirty rects since the frame its
  texture holds into the same managed D3D8 texture `ViewD3D8` uses.
- **A crashed host** is restarted up to five times in a row with backoff; one
  that ran five minutes before failing has the budget back. Views are
  created again at their last URL, size, origin lock and focus. To scripts
  this is a reload: `browserLoadingStart` and `browserDocumentReady` fire
  again and page state is gone; `browserCreated` does not repeat.

The host is built with the same MSVC toolset as the client tree (`vcvarsall
-vcvars_ver`), so the one runtime the client package ships from that
toolset's redist serves both. Its build step reruns only when a source it
compiles changes. `RunFrameworkCefRemoteTests` (unit tests, then a smoke test
against the real host) exists in a tree configured with
`-DFW_BUILD_CEF_REMOTE_TESTS=ON`, the default where `FW_BUILD_TESTS` is on.

`RegisterSchemeHandlerFactory` and `View::SetOnBeforeBrowseCallback` take CEF
objects and do nothing in remote mode; `View::GetBrowser` is null there. Serve
content through `RegisterResourceRoot`, and send input through the `View`
methods (`SendMouseClick`, `SendKeyEvent`, `ImeCommitText`, ...).

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
