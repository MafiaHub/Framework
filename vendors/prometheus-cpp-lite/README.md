# prometheus-cpp-lite

[![Build examples](https://github.com/biaks/prometheus-cpp-lite/actions/workflows/cmake.yml/badge.svg)](https://github.com/biaks/prometheus-cpp-lite/actions/workflows/cmake.yml)
[![Build multi-platform](https://github.com/biaks/prometheus-cpp-lite/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/biaks/prometheus-cpp-lite/actions/workflows/cmake-multi-platform.yml)


**Crossplatform header-only C++ library for quickly adding metrics (and profiling) functionality to C++ projects — simple, fast, dependency-free.**

**Simplest usage — global registry, no boilerplate:**

```cpp
#include <prometheus/prometheus.h>

int main() {
  prometheus::gauge_metric_t   conn ("connections",   "Open connections");
  prometheus::counter_metric_t get  ("http_requests", "HTTP requests", {{"method", "GET"}});
  prometheus::counter_metric_t post ("http_requests", "HTTP requests", {{"method", "POST"}});
  prometheus::http_server.start ("127.0.0.1:9100"); // default all metrics are exposed at /metrics

  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    conn  = 10 + std::rand() % 50;
    get  += 1;
    post += 2;
  }
}
```

**Full control — registry, families, labels, custom value types, multi endpoints and more (see below):**

```cpp
#include <prometheus/prometheus.h>
using namespace prometheus;

int main() {
  registry_t          registry;
  gauge_metric_t      conn   (registry, "connections",   "Open connections", {{"host", "web01"}});
  family_t            reqs   (registry, "http_requests", "HTTP requests",    {{"host", "web01"}});
  counter_t<double&>  get    (reqs,     {{"method", "GET"},  {"status", "200"}});
  counter_t<double&>  post   (reqs,     {{"method", "POST"}, {"status", "201"}});
  http_server_t       server (registry, "127.0.0.1:9100", "/metrics/app", log_e::info);

  registry_t          registry2;
  server.add_endpoint(registry2, "/metrics/sys");

  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    conn  = 10 + std::rand() % 50;
    get  += 1.5;
    post += 0.75;
  }
}
```

---



## Why another Prometheus library for C++?

The main C++ Prometheus library - [jupp0r/prometheus-cpp](https://github.com/jupp0r/prometheus-cpp) - is mature and battle-tested, but it was designed with a Java mindset:

|                     | **prometheus-cpp**                 | **prometheus-cpp-lite**                                           |
|---------------------|------------------------------------|-------------------------------------------------------------------|
| Architecture        | Split into 3 libraries (`core`, `pull`, `push`) with separate `.h`/`.cpp` pairs per class | Single header-only library - just copy the headers |
| Dependencies        | zlib, libcurl, civetweb (or beast) | **None**                                                          |
| Build system        | Bazel (primary) + CMake            | CMake or **just copy files**                                      |
| Minimum boilerplate | ~20 lines to create one counter    | **2 lines** to create a counter and expose it (with global registry, see below) |
| Value types         | `double` only                      | `uint64_t` (default), `double`, `int64_t`, or any arithmetic type |
| Metric types        | `counter`, `gauge`, `histogram`, `summary`, `info` | [`counter`](examples/test_counter.cpp), [`gauge`](examples/test_gauge.cpp), [`histogram`](examples/test_histogram.cpp), [`summary`](examples/test_summary.cpp), `info`, **[`benchmark`](examples/test_benchmark.cpp)** - or [make your own](examples/add_custom_metric_class.cpp) |
| C++ standard        | C++11                              | C++11                                                             |
| Thread-safe         | Yes                                | Yes                                                               |

**prometheus-cpp-lite is not a fork.** It is a ground-up rewrite focused on C++ idioms:
operators instead of `.Increment()`, RAII instead of manual registration, zero-copy
reference handles instead of raw pointers.

**Full prometheus-cpp compatibility.** The library supports the same API style
used by prometheus-cpp (`BuildCounter()`, `Family`, `Registry`, `Add()`, raw
references). If you are migrating from prometheus-cpp, your existing patterns
will work.

---



## Features

- **Header-only**                       - no libraries to build, no linker flags, no package manager required
- **Cross-platform**                    - works on Linux and Windows with any C++11 and higher compiler
- **Zero dependencies**                 - pure C++ standard library (networking uses a bundled copy of [ip-sockets-cpp-lite](https://github.com/biaks/ip-sockets-cpp-lite))
- **Low entry barrier**                 - a working counter with HTTP export is 2 lines of code (with global registry, see below)
- **Gradual complexity**                - start simple, add families/labels/registries/custom types when needed
- **Multiple value types**              - `uint64_t` (fast integer), `double` (Prometheus-compatible), `int64_t`, or custom
- **Six metric types**                  - [`counter`](examples/test_counter.cpp), [`gauge`](examples/test_gauge.cpp), [`histogram`](examples/test_histogram.cpp), [`summary`](examples/test_summary.cpp), `info`, [`benchmark`](examples/test_benchmark.cpp)
- **Three export modes**                - HTTP pull server, HTTP push (Pushgateway), file (node_exporter textfile)
- **Simplest way with global_registry** - add metrics anywhere in your code without passing registry references
- **prometheus-cpp compatible**         - supports the same Builder/Family/Registry API style
- **Extensible**                        - each metric type is self-contained in one header; **you can [add your own](examples/add_custom_metric_class.cpp) metric by following the same pattern**
- **Detailed examples**                 - see the [examples](examples) folder for usage patterns

---



## Quick start

### Installation

**Option A - Copy headers (simplest):**

Copy the `include/prometheus` directory into your project and add it to
include paths. If you need HTTP pull or push support, also copy the headers
from `3rdparty/ip-sockets-cpp-lite/include/`.

**Option B - CMake subdirectory:**

```cmake
add_subdirectory(prometheus-cpp-lite)

# Header-only (you define global_registry yourself if you need it):
target_link_libraries(your_target prometheus-cpp-lite)

# Or with pre-defined global objects (global_registry, file_saver, http_pusher, http_server):
target_link_libraries(your_target prometheus-cpp-lite-full)
```

**Option C - CMake FetchContent:**

```cmake
include(FetchContent)
FetchContent_Declare(
  prometheus-cpp-lite
  GIT_REPOSITORY https://github.com/biaks/prometheus-cpp-lite.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(prometheus-cpp-lite)
target_link_libraries(your_target prometheus-cpp-lite)
```

### All metric types at a glance

```cpp
#include <prometheus/prometheus.h>

using namespace prometheus;

int main() {
  registry_t         registry;

  counter_metric_t   requests (registry, "http_requests_total",  "Total requests");
  gauge_metric_t     active   (registry, "active_connections",   "Open connections");
  histogram_metric_t latency  (registry, "request_duration_sec", "Request latency", {}, {0.01, 0.05, 0.1, 0.5, 1.0});
  summary_metric_t   response (registry, "response_time_sec",    "Response time",   {}, {{0.5,0.05},{0.9,0.01},{0.99,0.001}});
  benchmark_metric_t uptime   (registry, "uptime_sec",           "Process uptime");
  info_metric_t      info     (registry, "build_info",           "Build information", {{"version", "1.0.0"}, {"commit", "abc123"}});

  http_server_t      server   (registry, {{127,0,0,1}, 9100});

  // curl http://localhost:9100/metrics

  uptime.start();
  for (int i = 0; i < 60; ++i) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    requests++;
    active.Set      (10    +  std::rand() % 50);
    latency.Observe (0.001 * (std::rand() % 1000));
    response.Observe(0.001 * (std::rand() % 500));
  }
  uptime.stop();
}
```

---



## Three ways to export metrics

### 1. HTTP pull (recommended for long-running services)

The application runs an HTTP server. Prometheus scrapes it periodically ([example](examples/provide_via_http_pull_simple.cpp)).

```cpp
#include <prometheus/prometheus.h>

prometheus::registry_t       registry;
prometheus::counter_metric_t metric (registry, "my_counter", "Example counter");
prometheus::http_server_t    server (registry, "127.0.0.1:9100");
// → http://localhost:9100/metrics
```

**Multiple endpoints** for different metric domains ([example](examples/provide_via_http_pull_advanced.cpp)):

```cpp
using namespace prometheus;
auto app_registry = std::make_shared<registry_t>();
auto sys_registry = std::make_shared<registry_t>();

http_server_t server;
server.add_endpoint(app_registry, "/metrics/app");
server.add_endpoint(sys_registry, "/metrics/sys");
server.start({{127,0,0,1}, 9200});
// → http://localhost:9200/metrics/app
// → http://localhost:9200/metrics/sys
```

### 2. HTTP push (for short-lived jobs, edge devices)

Metrics are POSTed to a Pushgateway or VictoriaMetrics at a fixed interval ([example](examples/provide_via_http_push_simple.cpp)).

```cpp
#include <prometheus/prometheus.h>

prometheus::registry_t       registry;
prometheus::counter_metric_t metric (registry, "my_counter", "Example counter");
prometheus::http_pusher_t    pusher (registry, std::chrono::seconds(5),
                                               "http://localhost:9091/metrics/job/myapp");
```

### 3. File export (for node_exporter textfile collector)

Metrics are written to a `.prom` file at a fixed interval ([example](examples/provide_via_textfile.cpp)).

```cpp
#include <prometheus/prometheus.h>

prometheus::registry_t       registry;
prometheus::counter_metric_t metric (registry, "my_counter", "Example counter");
prometheus::file_saver_t     saver  (registry, std::chrono::seconds(5), "./metrics.prom");
```

---

## Labels (dimensional data)

### Directly on a metric:

```cpp
registry_t       registry;
counter_metric_t get_count  (registry, "requests", "Requests by", {{"host", "dev"}, {"method",  "GET"}});
counter_metric_t post_count (registry, "requests", "Requests by", {{"host", "dev"}, {"method", "POST"}});

get_count++;
post_count += 5;
```

### Using a family

```cpp
registry_t       registry;
family_t         requests   (registry, "requests", "Requests by", {{"host", "dev"}});
counter_metric_t get_count  (requests, {{"method",  "GET"}});
counter_metric_t post_count (requests, {{"method", "POST"}});

get_count++;
post_count += 5;
```

Output is same:

```
# HELP http_requests HTTP requests by method
# TYPE http_requests counter
http_requests{host="dev", method="GET"} 1
http_requests{host="dev", method="POST"} 5
```

---



## Choosing a value type

The default counter uses `uint64_t` for maximum performance.
The default gauge uses `int64_t`.
Both can be overridden with `double` or any other arithmetic type.

**Why `uint64_t` is faster than `double` for atomics:**
`std::atomic<uint64_t>` supports lock-free `fetch_add` on most platforms -
a single CPU instruction.
`std::atomic<double>` typically lacks native `fetch_add`, so every increment
requires a compare-and-swap (CAS) loop: load the current value, compute the
new value, attempt to store it, and retry if another thread modified it in
between. Under contention this can be significantly slower.

prometheus-cpp uses `double` for all metric types. prometheus-cpp-lite
defaults to integer atomics where it makes sense (counters, gauges) and
uses `double` where fractional values are required (histograms, summaries,
benchmarks).

```cpp
// Default: uint64_t - lock-free fetch_add, fastest for integer counters
counter_metric_t integer_counter (registry, "requests_total", "Total requests");
integer_counter++;

// Floating-point counter - CAS loop, use when you need fractional values
counter_t<double&> fp_counter (registry, "duration_seconds_total", "Total duration");
fp_counter += 0.42;
```

---



## Global registry — add metrics to existing code in seconds

The global registry is designed for the most common real-world scenario:
**you already have a large codebase and want to add metrics without
restructuring it.**

Normally, adding metrics to existing code is painful. You need to figure out
how to pass a registry or metric objects through your class hierarchy,
modify constructors, add member variables, and thread references through
layers of code that were never designed for observability.

With prometheus-cpp-lite's global registry approach, none of that is
necessary. The `prometheus-cpp-lite-full` CMake target provides ready-made
global objects — `global_registry`, `file_saver`, `http_pusher`, and
`http_server` — so you can create metrics anywhere in your code and start
exposing them with a single call from any place ([example](examples/check_global_objects.cpp)).
No plumbing required.

### The workflow

**1. Link against `prometheus-cpp-lite-full` in your CMakeLists.txt:**

```cmake
target_link_libraries(your_target prometheus-cpp-lite-full)
```

**2. Sprinkle metrics anywhere in your codebase — any file, any class, any function:**

```cpp
#include <prometheus/counter.h>

void handle_request() {
  prometheus::counter_metric_t requests ("http_requests_total", "Total requests");
  requests++;
}

void handle_error() {
  prometheus::counter_metric_t errors   ("http_errors_total",   "Total errors");
  prometheus::counter_metric_t requests ("http_requests_total", "Total requests");
  // ^ same metric as in handle_request() — same name, same global registry
  errors++;
  requests++;
}
```

Metrics with the same name automatically refer to the same underlying
metric in the global registry. No constructor changes, no member
variables, no passing references around.

**3. Start exposing metrics — one line, anywhere in your code:**

```cpp
#include <prometheus/prometheus.h>

// Call this once at startup, e.g. in main():
prometheus::http_server.start("127.0.0.1:9100");
// → http://localhost:9100/metrics — all metrics from all files are here
```

That's it. Every metric you created in step 2 is automatically available
at the HTTP endpoint ([example](examples/use_metrics_in_class_simple.cpp)).
You can also use the other pre-defined global
exporters:

```cpp
#include <prometheus/prometheus.h>

// Write metrics to a .prom file every 5 seconds:
prometheus::file_saver.start(std::chrono::seconds(5), "./metrics.prom");

// Push metrics to Pushgateway every 10 seconds:
prometheus::http_pusher.start(std::chrono::seconds(10), "http://pushgateway:9091/metrics/job/myapp");
```

### If you prefer pure header-only (no extra .cpp files)

If you don't want any pre-compiled translation units in your project, link
against the header-only target instead:

```cmake
target_link_libraries(your_target prometheus-cpp-lite)
```

In this case, define the global objects yourself — once, in any `.cpp` file:

```cpp
#include <prometheus/prometheus.h>

// Define once, in a single .cpp file:
namespace prometheus {
  registry_t    global_registry;
  http_server_t http_server (global_registry);
}

int main() {
  prometheus::http_server.start("127.0.0.1:9100");
  // ... rest of your application ...
}
```

After that, metrics created anywhere via the two-argument constructor
(`counter_metric_t m ("name", "help")`) automatically use `global_registry`,
and the behavior is identical to `prometheus-cpp-lite-full`.

---

### Quick profiling with benchmark metrics

The `benchmark_t` metric is especially powerful with the global registry.
You can wrap any function call or code block with `start()`/`stop()` to
measure its wall-clock time — without touching the function's signature,
class hierarchy, or build dependencies. Just add two lines around the code
you want to profile:

```cpp
#include <prometheus/benchmark.h>

void process_order(const Order& order) {
  prometheus::benchmark_metric_t timer ("process_order_seconds", "Time spent in process_order");
  timer.start();

  // ... existing code, unchanged ...

  timer.stop();
}
```

The accumulated elapsed time is immediately available at your `/metrics`
endpoint — no separate profiling tool, no recompilation with special flags,
no post-hoc analysis. You get real production latency data from live traffic
([example](examples/use_benchmark_in_class_simple.cpp)).

**Best practice for multithreaded code:** the `start()`/`stop()` state
machine is local to each metric instance and is intentionally not
thread-safe — this avoids lock overhead in hot paths. When multiple threads
execute the same function, give each thread its own metric instance by
adding a distinguishing label (e.g. thread index or thread name):

```cpp
#include <prometheus/benchmark.h>

// Shared family — defined once (e.g. at file scope or in a class).
prometheus::family_t worker_time ("worker_duration_seconds", "Per-thread processing time");

void worker_thread_func(int thread_id) {
  // Each thread owns its own metric — no start/stop contention.
  prometheus::benchmark_metric_t my_timer (worker_time, {{"thread", std::to_string(thread_id)}});
  for (;;) {
    my_timer.start();
    // ... do work ...
    my_timer.stop();
  }
}
```

This produces separate time series per thread, which you can aggregate in
PromQL (`sum`, `avg`, `max`) or inspect individually in Grafana:

```
# HELP worker_duration_seconds Per-thread processing time
# TYPE worker_duration_seconds counter
worker_duration_seconds{thread="0"} 12.345
worker_duration_seconds{thread="1"} 11.892
worker_duration_seconds{thread="2"} 13.017
```

This is invaluable when instrumenting legacy code — from personal
experience, it turns a multi-day refactoring task into a few minutes of
work.

---

## Info metric - expose build metadata

The `info_t` metric is a gauge permanently set to `1` whose labels carry
metadata such as version, commit hash, or build date. This is a standard
Prometheus pattern for exposing build information that can be joined with
other metrics in PromQL or Grafana.

```cpp
#include <prometheus/info.h>
prometheus::info_metric_t build_info (registry, "build_info", "Build information",
                          {{"version", "1.0.0"}, {"commit", "abc123"}, {"branch", "main"}});
```

Output:

```
# HELP build_info Build information
# TYPE build_info gauge
build_info{branch="main",commit="abc123",version="1.0.0"} 1
```

---



## Drop-in compatibility with `prometheus-cpp`

prometheus-cpp-lite ships shim headers and class aliases that make it
**a transparent replacement** for [jupp0r/prometheus-cpp](https://github.com/jupp0r/prometheus-cpp).
Code written for prometheus-cpp compiles and works **without any changes** —
not even the `#include` lines need to be modified.

| prometheus-cpp                                  | prometheus-cpp-lite       | Shim header                               |
|-------------------------------------------------|---------------------------|-------------------------------------------|
| `#include <prometheus/exposer.h>`               | ✔ provided                | redirects to `<prometheus/http_puller.h>` |
| `#include <prometheus/gateway.h>`               | ✔ provided                | redirects to `<prometheus/http_pusher.h>` |
| `#include <prometheus/family.h>`                | ✔ provided                | redirects to `<prometheus/core.h>`        |
| `#include <prometheus/registry.h>`              | ✔ provided                | redirects to `<prometheus/core.h>`        |
| `#include <prometheus/text_serializer.h>`       | ✔ provided                | redirects to `<prometheus/core.h>`        |
| `#include <prometheus/client_metric.h>`         | ✔ provided                | redirects to `<prometheus/core.h>`        |
| `prometheus::Exposer`                           | alias for `http_server_t` | defined in `<prometheus/http_puller.h>`   |
| `prometheus::Gateway`                           | alias for `http_pusher_t` | defined in `<prometheus/http_pusher.h>`   |
| `prometheus::Registry`                          | same class                | defined in `<prometheus/core.h>`          |
| `prometheus::Family`                            | same class                | defined in `<prometheus/core.h>`          |
| `prometheus::BuildCounter()`                    | same function             | defined in `<prometheus/counter.h>`       |
| `prometheus::BuildGauge()`                      | same function             | defined in `<prometheus/gauge.h>`         |
| `prometheus::BuildHistogram()`                  | same function             | defined in `<prometheus/histogram.h>`     |
| `prometheus::BuildSummary()`                    | same function             | defined in `<prometheus/summary.h>`       |
| `Add()`, `Increment()`, `RegisterCollectable()` | same API                  |                                           |

### Example 1: Exposer (HTTP pull)

The code below is **[valid prometheus-cpp code](examples/legacy_prometheus_cpp.cpp)**. It compiles and runs with
prometheus-cpp-lite without touching a single line — just swap the library
in your build system:

```cpp
#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>

int main() {
  prometheus::Exposer exposer{"127.0.0.1:8080"};
  auto registry = std::make_shared<prometheus::Registry>();

  auto& family = prometheus::BuildCounter()
                   .Name("http_reqs")
                   .Help("HTTP requests")
                   .Register(*registry);

  auto& counter = family.Add({{"method", "GET"}});
  exposer.RegisterCollectable(registry);

  counter.Increment();
}
```

**The same thing in prometheus-cpp-lite native API — 6 lines instead of 13:**

```cpp
#include <prometheus/prometheus.h>

int main() {
  prometheus::registry_t       registry;
  prometheus::http_server_t    server  (registry, "127.0.0.1:8080");
  prometheus::counter_metric_t counter (registry, "http_reqs", "HTTP requests", {{"method","GET"}});

  counter++;
}
```

### Example 2: Gateway (HTTP push)

Again — **unmodified prometheus-cpp code**, works as-is:

```cpp
#include <prometheus/counter.h>
#include <prometheus/gateway.h>
#include <prometheus/registry.h>

int main() {
  prometheus::Gateway gateway{"localhost", "9091", "my_job", {{"instance", "host1"}}};
  auto registry = std::make_shared<prometheus::Registry>();

  auto& family = prometheus::BuildGauge()
                   .Name("cpu_usage_percent")
                   .Help("CPU usage")
                   .Register(*registry);

  auto& cpu_usage = family.Add({{"core", "0"}});
  gateway.RegisterCollectable(registry);

  cpu_usage.Set(0.54);
  cpu_usage.Increment(0.09);
  gateway.Push();
}
```

**The same thing in prometheus-cpp-lite native API:**

```cpp
#include <prometheus/prometheus.h>

int main() {
  prometheus::registry_t       registry;
  prometheus::http_pusher_t    pusher    (registry, std::chrono::seconds(5), "localhost", "9091", "my_job", {{"instance", "host1"}});
  prometheus::gauge_t<double&> cpu_usage (registry, "cpu_usage_percent", "CPU usage", {{"core", "0"}});

  cpu_usage  = 0.54;
  cpu_usage += 0.09;
}
```

Both styles can coexist in the same project — all metrics end up in the same
registry and are serialized identically.

---



## API levels

prometheus-cpp-lite supports several API styles. Start with the simplest and
move to more explicit forms only when you need fine-grained control:

| Level                              | Description                                         | Example                                                            |
|------------------------------------|-----------------------------------------------------|--------------------------------------------------------------------|
| **Simple** with `global_registry`  | Global registry, implicit family, operator syntax   | `counter_metric_t m ("name", "help", {{"k","v"}});`<br>`m++;` |
| **Simple** with explicit `registry`| Explicit registry, implicit family, operator syntax | `registry_t r;`<br>`counter_metric_t m (r, "name", "help", {{"k","v"}});`<br>`m++;` |
| **Family** with `global_registry`  | Global registry, explicit family with family labels | `family_t f ("name", "help", {{"k","v"}});`<br>`counter_metric_t m (f, {{"k","v"}});`<br>`m++;` |
| **Family** with explicit `registry`| Explicit registry and family with family labels     | `registry_t r;`<br>`family_t f (r, "name", "help", {{"k","v"}});`<br>`counter_metric_t m (f, {{"k","v"}});`<br>`m++;` |
| **Custom family**                  | Compile-time type safety for families               | `counter_family_t f ("name", "help", {{"k","v"}});`<br>`counter_metric_t m (f, {{"k","v"}});`<br>`m++;` |
| **Custom types of metric values**  | Customisation values types of metrics               | `counter_t<double&> m ("name", "help", {{"k","v"}});`<br>`m++;` |
| **`prometheus-cpp` compatible**    | Builder pattern, raw references                     | `auto  r = std::make_shared<prometheus::Registry>();`<br>`auto& f = BuildCounter().Name("nm").Help("hlp").Labels({{"k","v"}}).Register(r)`<br>`auto& m = f.Add({{"k","v"}});`<br>`m.Increment();` |

All levels are interoperable - metrics created with any style end up in the
same registry and are serialized together.

See the [`examples/`](examples/) directory for complete working programs
demonstrating every API level (`test_*` files).

---




## Migrating from prometheus-cpp-lite v1.0

prometheus-cpp-lite v2.0 is a major update that simplifies the project
structure, unifies the API, and adds new features — while preserving full
backward compatibility. **Your existing v1.0 code will compile and work
without changes.** You will only see deprecation warnings guiding you
toward the updated API.

### Structural changes

|                           | v1.0                                                          | v2.0                                                   |
|---------------------------|---------------------------------------------------------------|--------------------------------------------------------|
| Directory layout          | `core/include/`, `simpleapi/`, `3rdpatry/`                    | `include/`, `src/`, `3rdparty/`                        |
| CMake targets             | `prometheus-cpp-lite-core` (header-only)                      | `prometheus-cpp-lite` (header-only, simpleapi)         |
|                           | `prometheus-cpp-simpleapi` (simpleapi, static, with globals)  | `prometheus-cpp-lite-full` (static, with globals)      |
| Core headers              | Split: `registry.h`, `family.h`, `metric.h`, `builder.h`, …   | Unified: `core.h` (one header for all core types)      |
| Metric headers            | Same                                                          | Same (`counter.h`, `gauge.h`, etc.)                    |
| Umbrella header           | `simpleapi.h` (includes + global objects + simpleapi aliases) | `prometheus.h` (includes + global object declarations) |
| Networking                | separate `Gateway` and `PushToServer`,<br> no HTTP pull server    | rewritten `http_pusher_t` (POST/PUT/DELETE,<br>sync + async, periodic + on-demand, `Gateway`-compatible)<br>+ new `http_server_t` (HTTP pull, multi-endpoint, index page, `Exposer`-compatible) |
| Networking library        | `http-client-lite` (bundled)                                  | `ip-sockets-cpp-lite` (bundled)                        |
| Networking headers        | `save_to_file.h`, `push_to_server.h`, `gateway.h`             | `file_saver.h`, `http_pusher.h`, `http_puller.h`       |
| Collect values            | standalone classes for storing and serializing metric values  | each metric collects and serializes values itself      |
| Metric ownership model    | separate classes for owning metrics and referencing them      | single template: `metric_t<T>` owns the value, `metric_t<T&>`<br> is a zero-copy reference — same class, same API |
| Add custom metric classes | not possible                                                  | support users custom metric classes — follow the existing metric pattern  |

### API changes

|                            | v1.0                                                 | v2.0                                                                      |
|----------------------------|------------------------------------------------------|---------------------------------------------------------------------------|
| simpleAPI primary namespace| `prometheus::simpleapi::`                            | `prometheus::`                                                            |
| Creating a family          | `simpleapi::counter_family_t f {"name", "help", {{"k","v"}}};` | `family_t f {"name", "help", {{"k","v"}}};`<br>`family_t f {registry, "name", "help", {{"k","v"}}};`<br>`counter_family_t f {"name", "help", {{"k","v"}}};`<br>`counter_family_t f {registry, "name", "help", {{"k","v"}}};` |
| Creating a metric (simple) | `simpleapi::counter_metric_t m {"name", "help", {{"k","v"}}};` | `counter_metric_t m {"name", "help", {{"k","v"}}};`<br>`counter_metric_t m {registry, "name", "help", {{"k","v"}}};` |
| Creating a metric in family| `simpleapi::counter_metric_t m {family.Add(l)};`     | `counter_metric_t m {family, {{"k","v"}}};`<br>`counter_metric_t m {family.Add({{"k","v"}})};` |
| Networking classes         | `SaveToFile`, `PushToServer`                         | `file_saver_t`, `http_pusher_t`                                           |
| HTTP pull server           | not available                                        | `http_server_t` (new)                                                     |
| `prometheus-cpp` compat    | partial                                              | full (added `Exposer` and `Gateway` aliases)                              |
| New metric types           | —                                                    | `info_t`                                                                  |

### What stayed the same (backward compatibility)

Everything below continues should to work in v2.0 — no code changes required:

- **Old CMake target names.** `prometheus-cpp-lite-core` and
  `prometheus-cpp-simpleapi` are aliases for the new targets.
- **Old header names.** Shim headers (`registry.h`, `family.h`, `metric.h`,
  `builder.h`, `hash.h`, `collectable.h`, `client_metric.h`,
  `metric_family.h`, `text_serializer.h`, `save_to_file.h`,
  `push_to_server.h`, `gateway.h`) redirect to the new headers with a
  one-time `#pragma message` deprecation notice.
- **Old class names.** `SaveToFile` → `file_saver_t`, `PushToServer` →
  `http_pusher_t`, `Gateway` → `http_pusher_t` — all available as
  `[[deprecated]]` type aliases.
- **`prometheus::simpleapi::` namespace.** All type aliases (`counter_metric_t`,
  `counter_family_t`, `gauge_metric_t`, etc.) are preserved with
  `[[deprecated]]` attributes pointing to the `prometheus::` equivalents.
- **`#include <prometheus/simpleapi.h>`** still works. It includes the new
  umbrella header `<prometheus/prometheus.h>` (which pulls in all metric
  headers, networking headers, and global object declarations) and provides
  the deprecated `prometheus::simpleapi::` aliases. A `#pragma message`
  notice suggests switching to `<prometheus/prometheus.h>`.

### Step-by-step migration guide

All steps are optional. Your code compiles without them — these changes
only silence deprecation warnings and modernize your codebase.

#### Step 1 — Update CMakeLists.txt

Replace:

```cmake
# v1.0
add_subdirectory("prometheus-cpp-lite")

# If you need SimpleAPI and pre-defined global objects (global_registry, file_saver, etc.):
target_link_libraries(your_target prometheus-cpp-simpleapi)

# If you use only ComplexAPI or Java liked legacy API from prometheus-cpp and use local registries:
target_link_libraries(your_target prometheus-cpp-lite-core)
```

With:

```cmake
# v2.0
add_subdirectory("prometheus-cpp-lite")

# If you need pre-defined global objects (global_registry, file_saver, http_pusher, http_server):
target_link_libraries(your_target prometheus-cpp-lite-full)

# If you need any type of API and you define global_registry yourself or use local registries only:
target_link_libraries(your_target prometheus-cpp-lite)
```

#### Step 2 — Update `#include` directives

| Old (v1.0)                       | New (v2.0)                   | Notes                                                                  |
|----------------------------------|------------------------------|------------------------------------------------------------------------|
| `<prometheus/simpleapi.h>`       | `<prometheus/prometheus.h>`  | Umbrella header: all metrics + networking + global object declarations |
| `<prometheus/registry.h>`        | `<prometheus/core.h>`        | Or just include a metric header — it pulls in `core.h` automatically   |
| `<prometheus/family.h>`          | `<prometheus/core.h>`        |                                                                        |
| `<prometheus/metric.h>`          | `<prometheus/core.h>`        |                                                                        |
| `<prometheus/builder.h>`         | `<prometheus/core.h>`        |                                                                        |
| `<prometheus/hash.h>`            | `<prometheus/core.h>`        |                                                                        |
| `<prometheus/collectable.h>`     | `<prometheus/core.h>`        |                                                                        |
| `<prometheus/client_metric.h>`   | `<prometheus/core.h>`        |                                                                        |
| `<prometheus/metric_family.h>`   | `<prometheus/core.h>`        |                                                                        |
| `<prometheus/text_serializer.h>` | `<prometheus/core.h>`        |                                                                        |
| `<prometheus/save_to_file.h>`    | `<prometheus/file_saver.h>`  |                                                                        |
| `<prometheus/push_to_server.h>`  | `<prometheus/http_pusher.h>` |                                                                        |
| `<prometheus/gateway.h>`         | `<prometheus/http_pusher.h>` |                                                                        |

> **Tip:** You don't need to include `<prometheus/core.h>` explicitly —
> every metric header (e.g. `<prometheus/counter.h>`) already includes it.
> If you want everything at once, use `<prometheus/prometheus.h>`.

#### Step 3 — Update namespace and class names

```cpp
// v1.0 SimpleAPI style:
prometheus::simpleapi::counter_family_t family  { "name", "help" };
prometheus::simpleapi::counter_metric_t metric1 { family.Add(labels) };
prometheus::simpleapi::counter_metric_t metric2 { "standalone", "help", labels };

// v2.0 (with global_registry)
prometheus::family_t         family  ("name", "help");
prometheus::counter_metric_t metric1 (family, labels);
prometheus::counter_metric_t metric2 ("standalone", "help", labels);

// v2.0 (with explicit registry)
prometheus::registry_t       registry;
prometheus::family_t         family  (registry, "name", "help");
prometheus::counter_metric_t metric1 (family, labels);
prometheus::counter_metric_t metric2 (registry, "standalone", "help", labels);
```

### Quick reference: v1.0 → v2.0 name mapping

| v1.0                                      | v2.0                                 | Notes                                        |
|-------------------------------------------|--------------------------------------|----------------------------------------------|
| `#include <prometheus/simpleapi.h>`       | `#include <prometheus/prometheus.h>` | Umbrella header                              |
| `prometheus::simpleapi::counter_metric_t` | `prometheus::counter_metric_t`       | All metrics moved to main namespace          |
| `prometheus::simpleapi::counter_metric_t` | `prometheus::counter_t<uint64_t&>`   | Fast template to use custom type             |
| `prometheus::simpleapi::counter_family_t` | `prometheus::counter_family_t`       | All metrics families moved to main namespace |
| `prometheus::simpleapi::counter_family_t` | `prometheus::family_t`               | New simple class with check type in runtime  |
| `prometheus::SaveToFile`                  | `prometheus::file_saver_t`           | Change class name                            |
| `prometheus::PushToServer`                | `prometheus::http_pusher_t`          | Change class name                            |
| `prometheus::Gateway`                     | `prometheus::http_pusher_t`          | `prometheus-cpp` compat alias also available |

---



## Examples

All examples are in the [`examples/`](examples/) directory. Build them with:

```
cmake -B build -G Ninja -DPROMETHEUS_BUILD_EXAMPLES=ON
cmake --build build
```


| Example | Description |
|---------|-------------|
| **Quick start** | |
| [`quick_start.cpp`](examples/quick_start.cpp) | All metric types + HTTP server in one file. The code from the README "All metric types at a glance" section. |
| **Export modes** | |
| [`provide_via_http_pull_simple.cpp`](examples/provide_via_http_pull_simple.cpp) | Shortest HTTP pull server — 3 lines of setup, `curl http://localhost:9100/metrics`. |
| [`provide_via_http_pull_advanced.cpp`](examples/provide_via_http_pull_advanced.cpp) | Single-registry and multi-path (multiple registries at different URLs) HTTP pull examples. |
| [`provide_via_http_push_simple.cpp`](examples/provide_via_http_push_simple.cpp) | Shortest periodic push to Pushgateway — 3 lines of setup. |
| [`provide_via_http_push_advanced.cpp`](examples/provide_via_http_push_advanced.cpp) | Periodic push, on-demand Push/PushAdd/Delete (Gateway API), and async push examples. |
| [`provide_via_textfile.cpp`](examples/provide_via_textfile.cpp) | Periodic save to `.prom` file for node_exporter textfile collector. |
| **Metric types — all API levels** | |
| [`test_counter.cpp`](examples/test_counter.cpp) | Counter (`uint64_t`) — every way to create: global/explicit registry, untyped/typed family, all legacy APIs. |
| [`test_gauge.cpp`](examples/test_gauge.cpp) | Gauge (`int64_t`) — same full set of API variants. |
| [`test_histogram.cpp`](examples/test_histogram.cpp) | Histogram (`double`) — buckets, custom boundaries, same full set of API variants. |
| [`test_summary.cpp`](examples/test_summary.cpp) | Summary (`double`) — quantiles, custom quantile definitions, same full set of API variants. |
| [`test_benchmark.cpp`](examples/test_benchmark.cpp) | Benchmark (`double`) — profile method execution time, same full set of API variants. |
| **Global objects** | |
| [`check_global_objects.cpp`](examples/check_global_objects.cpp) | Uses `prometheus-cpp-lite-full` cmake target with pre-defined globals: `global_registry`, `file_saver`, `http_server`, `http_pusher`. |
| **Using metrics in classes** | |
| [`use_metrics_in_class_simple.cpp`](examples/use_metrics_in_class_simple.cpp) | Simplest way to add metrics to a class — declare as members, increment in methods, expose via `http_server.start()`. |
| [`use_benchmark_in_class_simple.cpp`](examples/use_benchmark_in_class_simple.cpp) | Profile method execution time with `benchmark_metric_t` — `start()`/`stop()` around code phases. |
| [`use_metrics_in_class_advanced.cpp`](examples/use_metrics_in_class_advanced.cpp) | Dynamic per-instance metrics with labels — connection pool where each connection creates metrics at runtime via `make_metric<>()` without storing families. |
| **Compatibility** | |
| [`legacy_prometheus_cpp.cpp`](examples/legacy_prometheus_cpp.cpp) |  Unmodified prometheus-cpp code (`Exposer`, `BuildCounter`, `Registry`) — compiles as-is with prometheus-cpp-lite. |
| **Extensibility** | |
| [`add_custom_metric_class.cpp`](examples/add_custom_metric_class.cpp) | How to create your own metric type (`min_max_t` — tracks min/max of observed values). Demonstrates owning/reference forms, all family wrappers, and the Builder API. |




---

## Networking

HTTP pull and push functionality uses
[ip-sockets-cpp-lite](https://github.com/biaks/ip-sockets-cpp-lite) - a
header-only, dependency-free, cross-platform C++ sockets library by the same
author. A copy of its headers is bundled in
[`3rdparty/ip-sockets-cpp-lite/`](3rdparty/ip-sockets-cpp-lite/) so that
prometheus-cpp-lite remains self-contained with zero external dependencies.

If you only need the core metrics and serialization (e.g. you have your own
HTTP server or use file export only), the socket headers are not required.

---

## Supported exposition format

Prometheus Text Exposition Format (`text/plain; version=0.0.4`).

---

## License

MIT License. See [LICENSE](LICENSE) for details.
