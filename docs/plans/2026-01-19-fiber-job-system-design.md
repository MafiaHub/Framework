# Fiber-Based Job System Design

## Overview

Add an opt-in fiber-based job system to the MafiaHub Framework using [Fiber Tasking Library (FTL)](https://github.com/RichieSams/FiberTaskingLib).

**Primary use cases:**
- Background I/O (asset loading, file operations) without blocking the main loop
- General-purpose parallelism for CPU-bound work

**Platforms:** Windows, Linux, macOS

## Library

**Fiber Tasking Library v2.1.0**

- Download: https://github.com/RichieSams/FiberTaskingLib/archive/refs/tags/v2.1.0.zip
- License: Apache 2.0
- Vendoring: Direct copy to `vendors/ftl/`, pinned to v2.1.0

FTL provides:
- Fiber-based task scheduler with work stealing
- Atomic counters for task dependencies
- Cross-platform fiber primitives (Windows native fibers, ucontext/boost.context on Unix)

## File Structure

```
code/framework/src/jobs/
├── job_system.h          # Main JobSystem class, public API
├── job_system.cpp        # Implementation
├── task_builder.h        # Fluent API for task graphs
├── io_tasks.h            # High-level helpers for common I/O patterns
└── io_tasks.cpp

vendors/ftl/
├── include/
├── source/
├── CMakeLists.txt
└── VERSION               # Contains "2.1.0"
```

## Integration

The job system is **opt-in** - not bound to CoreModules or auto-initialized.

Projects create and manage their own instance:

```cpp
#include <framework/jobs/job_system.h>

class MyGameServer : public Framework::Integrations::Server::Instance {
    Framework::Jobs::JobSystem* _jobSystem = nullptr;

    void PostInit() override {
        JobSystemConfig config;
        _jobSystem = new Framework::Jobs::JobSystem(config);
    }

    void PostUpdate() override {
        _jobSystem->ProcessCompletedCallbacks();
    }

    void PostShutdown() override {
        delete _jobSystem;
    }
};
```

## API Design

### Configuration

```cpp
struct JobSystemConfig {
    uint32_t workerThreadCount = 0;       // 0 = auto-detect (num cores - 1)
    uint32_t fiberPoolSize = 128;         // Fibers per thread
    uint32_t fiberStackSize = 64 * 1024;  // 64KB per fiber stack
    bool enableProfiling = false;         // Tracy/Remotery integration
};
```

### Level 1: Simple Tasks

```cpp
auto* jobs = GetJobSystem();

// Single task
jobs->Schedule([]{
    // work happens on a fiber
});

// Named task (for profiling)
jobs->Schedule("LoadConfig", []{
    loadConfiguration();
});

// Batch of tasks (parallel)
jobs->ScheduleBatch(items, [](Item& item) {
    processItem(item);
});
```

### Level 2: Task Dependencies (Atomic Counters)

```cpp
ftl::AtomicCounter counter(scheduler);

// Schedule work that decrements counter when done
jobs->Schedule(&counter, []{ loadTextures(); });
jobs->Schedule(&counter, []{ loadModels(); });
jobs->Schedule(&counter, []{ loadSounds(); });

// This task waits for all above to complete
jobs->WaitForCounter(&counter);
processAllAssets();
```

### Level 3: Task Graphs

```cpp
auto loadConfig = jobs->CreateTask([]{ return loadConfig(); });
auto loadAssets = jobs->CreateTask([]{ return loadAssets(); });
auto initGame   = jobs->CreateTask([](Config c, Assets a){
    initializeGame(c, a);
});

initGame->DependsOn(loadConfig, loadAssets);
jobs->RunGraph(initGame);
```

### Blocking I/O in Fibers

```cpp
jobs->Schedule([]{
    // Fiber yields while waiting for I/O
    auto data = jobs->BlockingCall([]{
        return readFile("large_asset.bin");
    });
    processData(data);
});
```

## I/O Helpers

High-level utilities for common async I/O patterns:

```cpp
namespace Framework::Jobs::IO {

// Async file read - callback on completion
void ReadFileAsync(JobSystem* jobs,
                   const std::string& path,
                   std::function<void(std::vector<uint8_t>)> onComplete,
                   std::function<void(std::string)> onError = nullptr);

// Async file write
void WriteFileAsync(JobSystem* jobs,
                    const std::string& path,
                    std::vector<uint8_t> data,
                    std::function<void()> onComplete = nullptr,
                    std::function<void(std::string)> onError = nullptr);

// Batch file loading (parallel)
void ReadFilesAsync(JobSystem* jobs,
                    const std::vector<std::string>& paths,
                    std::function<void(std::vector<FileResult>)> onAllComplete);

// Blocking-style API for use inside fibers
std::vector<uint8_t> ReadFileBlocking(JobSystem* jobs, const std::string& path);
std::vector<FileResult> ReadFilesBlocking(JobSystem* jobs, const std::vector<std::string>& paths);

}
```

**Callback delivery:** Callbacks execute on the main thread during `ProcessCompletedCallbacks()`.

## Error Handling

Tasks can fail. Unhandled exceptions are caught at the scheduler level, logged via spdlog, and the fiber is recycled.

```cpp
// Manual try/catch
jobs->Schedule([]{
    try {
        riskyOperation();
    } catch (const std::exception& e) {
        spdlog::error("Task failed: {}", e.what());
    }
});

// Built-in error propagation
jobs->ScheduleWithErrorHandler(
    []{ return loadAsset("model.obj"); },
    [](Asset a) { useAsset(a); },             // onSuccess
    [](std::exception_ptr e) { logError(e); } // onError
);
```

## Profiling

When `enableProfiling = true`, the job system emits events compatible with:
- Tracy profiler (`TRACY_ENABLE`)
- Remotery
- Chrome tracing (JSON export)

Named tasks appear in the profiler timeline:

```cpp
jobs->Schedule("LoadTextures", []{ /* work */ });
jobs->Schedule("ParseConfig", []{ /* work */ });
```

## CMake Integration

```cmake
# Option 1: Use FTL's CMakeLists.txt
add_subdirectory(vendors/ftl)
target_link_libraries(Framework PRIVATE ftl)

# Option 2: Add sources directly
file(GLOB_RECURSE FTL_SOURCES "vendors/ftl/source/*.cpp")
target_sources(Framework PRIVATE ${FTL_SOURCES})
target_include_directories(Framework PRIVATE "vendors/ftl/include")
```

## Platform Notes

FTL handles platform-specific fiber implementations:
- **Windows**: Native fibers (`CreateFiber`/`SwitchToFiber`)
- **Linux**: `ucontext` or `boost.context` backend
- **macOS**: `ucontext` (deprecated but functional) or `boost.context`

For macOS, FTL can optionally use `boost.context` for better stability.
