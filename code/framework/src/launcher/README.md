# PE Loader

This directory contains the framework's PE (Portable Executable) loader, which allows loading a game executable into the launcher's process space without creating a new process.

## How It Works

The PE loader performs the following steps:

1. **Map the executable** - Memory-map the game's EXE file
2. **Load sections** - Copy PE sections (.text, .data, .rdata, etc.) into the launcher's address space
3. **Apply relocations** - Fix absolute addresses since the game loads at a different base than intended
4. **Resolve imports** - Hook IAT (Import Address Table) to redirect API calls as needed
5. **Setup TLS** - Initialize Thread Local Storage for the game's CRT
6. **Setup exception handling** - Register the game's exception handlers (x64)
7. **Invoke entry point** - Jump to the game's original entry point

## TLS (Thread Local Storage) Handling

The loader supports two approaches for TLS setup, configurable via `ProjectConfiguration::useDirectTlsSlot0`:

### Approach 1: Direct Slot 0 (`useDirectTlsSlot0 = true`)

Used for games that expect their CRT to use TLS slot 0 (e.g., Cyberpunk 2077).

**Requirements:**
- The launcher EXE must declare a "sacrificial" TLS buffer that claims slot 0
- This buffer gets overwritten with the game's TLS template

**How it works:**
1. Launcher EXE declares `__declspec(thread)` variable (claims slot 0)
2. Launcher's CRT uses a different slot for its own TLS (locale, etc.)
3. PE loader copies game's TLS template directly to slot 0
4. Game code expecting slot 0 finds its TLS data intact

**Example launcher code:**
```cpp
// Must be in the launcher EXE, not a DLL
__declspec(thread) uint8_t g_sacrificialTlsBuffer[32768];

// Touch early to ensure allocation
struct TlsInit {
    TlsInit() { g_sacrificialTlsBuffer[0] = 1; }
} g_tlsInit;
```

### Approach 2: Allocated Slot (`useDirectTlsSlot0 = false`, default)

Used for games that don't require slot 0 (e.g., Mafia series).

**How it works:**
1. Framework's loader DLL (`FrameworkLoaderData.dll`) has its own TLS buffer
2. `GetThreadLocalStorage` export provides the allocated slot index
3. PE loader copies game's TLS template to this allocated slot
4. Game's TLS index variable is patched to point to the allocated slot

This is the traditional approach and works for most games.

## Configuration

```cpp
Framework::Launcher::ProjectConfiguration config;
config.useDirectTlsSlot0 = true;  // Use direct slot 0 (requires sacrificial buffer)
config.useDirectTlsSlot0 = false; // Use allocated slot (default)
```

When the launcher and mapped game both use the dynamic UCRT and startup aborts in
`_register_thread_local_exe_atexit_callback`, the launcher has already claimed the
process-wide EXE TLS-destructor slot. Suppress only the mapped game's duplicate registration:

```cpp
config.suppressThreadLocalExeAtexitCallback = true;
```

Leave this disabled unless the duplicate-registration failure is confirmed; a game whose
launcher did not claim the slot should retain the normal UCRT registration. Suppression means
the mapped game's primary-thread TLS destructors are not run during process exit; thread
attach/detach callbacks and normal execution are unaffected.

## When to Use Which Approach

| Game Behavior | Approach | Config |
|--------------|----------|--------|
| Game CRT expects TLS at slot 0 | Direct Slot 0 | `useDirectTlsSlot0 = true` |
| Game works with any TLS slot | Allocated Slot | `useDirectTlsSlot0 = false` |

If you encounter crashes related to TLS (often manifesting as NULL pointer access at small offsets like 0x14, 0x24), try switching approaches.

## Files

- `project.cpp` / `project.h` - Main launcher project class and configuration
- `loaders/exe_ldr.cpp` / `exe_ldr.h` - PE executable loader implementation
- `data/tls.cpp` - TLS buffer for allocated slot approach (in FrameworkLoaderData.dll)
