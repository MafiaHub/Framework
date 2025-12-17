# Multi-Resource Scripting System Migration Plan

## Executive Summary

The current system uses a **single Lua state** where all scripts share globals, events, and lifecycle. The migration will introduce a **Resource Manager** that treats each "resource" (a collection of related scripts with a manifest) as an isolated unit with its own environment, lifecycle, and communication channels.

---

## Phase 1: Resource Abstraction Layer

### 1.1 Define the Resource Concept

Create a new `Resource` class that encapsulates:

- **Identity**: Unique name, version, author metadata
- **Manifest**: List of scripts, dependencies, exports, permissions
- **Environment**: Isolated Lua environment (not a separate state, but a sandboxed table environment)
- **State**: Loading, Running, Stopping, Stopped, Error
- **Event Handlers**: Resource-scoped event subscriptions
- **Exports**: Functions/tables this resource exposes to others

### 1.2 Resource Manifest Schema

Extend the current `manifest.json` to support:

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Unique resource identifier |
| `version` | string | Semantic versioning (e.g., "1.0.0") |
| `author` | string | Resource author |
| `description` | string | Human-readable description |
| `dependencies` | array | Required resources with version constraints |
| `exports` | array | Named functions/modules this resource provides |
| `permissions` | array | What APIs this resource can access |
| `priority` | number | Load order weight for resources without explicit dependencies |
| `server_files` | array | Server-side Lua scripts |
| `client_files` | array | Client-side Lua scripts |

Example manifest structure:
```json
{
  "name": "my-resource",
  "version": "1.0.0",
  "author": "Developer",
  "description": "Example resource",
  "dependencies": [
    { "name": "core-utils", "version": ">=1.0.0" }
  ],
  "exports": ["myFunction", "myModule"],
  "permissions": ["entities", "commands"],
  "priority": 100,
  "server_files": ["server/main.lua"],
  "client_files": ["client/main.lua"]
}
```

### 1.3 Resource States and Transitions

Define a state machine with the following states and transitions:

```
                    ┌─────────┐
                    │ Unloaded│
                    └────┬────┘
                         │ load()
                         ▼
                    ┌─────────┐
         ┌─────────►│ Loading │
         │          └────┬────┘
         │               │ success
         │    error      ▼
         │          ┌─────────┐
         │    ┌─────│ Running │◄────────┐
         │    │     └────┬────┘         │
         │    │          │ stop()       │ start()
         │    ▼          ▼              │
    ┌─────────┐    ┌──────────┐    ┌─────────┐
    │  Error  │    │ Stopping │───►│ Stopped │
    └─────────┘    └──────────┘    └─────────┘
```

Each state transition triggers lifecycle events within the resource.

### 1.4 Resource Directory Structure

Resources are organized in a dedicated directory:

```
resources/
├── core-utils/
│   ├── manifest.json
│   ├── server/
│   │   └── main.lua
│   └── client/
│       └── main.lua
├── player-system/
│   ├── manifest.json
│   ├── server/
│   │   ├── main.lua
│   │   └── database.lua
│   └── client/
│       └── hud.lua
└── vehicle-system/
    ├── manifest.json
    └── server/
        └── main.lua
```

---

## Phase 2: Resource Manager Core

### 2.1 ResourceManager Class

Create a central `ResourceManager` class with the following responsibilities:

**Discovery**:
- Scan a configurable resources directory for valid resource manifests
- Validate manifest schema on discovery
- Report invalid or malformed resources

**Dependency Resolution**:
- Build a dependency graph from all discovered resources
- Detect circular dependencies at load time
- Determine correct load order via topological sort

**Lifecycle Management**:
- Load, start, stop, and unload individual resources
- Handle cascade operations (stopping dependents when a dependency stops)
- Coordinate hot-reload without affecting unrelated resources

**Registry**:
- Track all discovered resources
- Maintain current state of each resource
- Provide query APIs for resource information

### 2.2 Dependency Graph

Implement a DAG (Directed Acyclic Graph) for dependency management:

**Graph Construction**:
- Each resource is a node
- Dependencies create directed edges (dependent → dependency)
- Version constraints are validated during edge creation

**Circular Dependency Detection**:
- Perform cycle detection during graph construction
- Reject resources that would create cycles
- Provide clear error messages identifying the cycle

**Load Order Calculation**:
- Topological sort produces load order
- Resources with no dependencies load first
- Priority field breaks ties between independent resources

**Reverse Dependency Tracking**:
- Track which resources depend on each resource
- When stopping a resource, identify affected dependents
- Support cascade stop or warning based on configuration

### 2.3 Resource Isolation Strategy

Use **Lua environment sandboxing** instead of multiple Lua states:

**Single Lua State**:
- Keep one `sol::state` per engine (server/client)
- Simpler binding management
- Lower memory footprint
- Easier debugging

**Environment Tables**:
- Each resource gets its own `_ENV` table
- Scripts execute with this environment as their global scope
- Prevents accidental global pollution between resources

**Shared Read-Only Globals**:
- Core builtins (Console, Math types, JSON, Hash, etc.) are shared
- Exposed as read-only in each resource's environment
- Modifications don't affect other resources

**Resource-Specific Globals**:
- Each resource can define its own globals
- These are isolated to the resource's environment
- Not visible to other resources unless exported

**Event Ownership**:
- Events registered by a resource are tagged with resource ID
- Allows cleanup when resource stops
- Enables resource-scoped event delivery

---

## Phase 3: Inter-Resource Communication

### 3.1 Export/Import System

Resources expose and consume functionality through explicit contracts:

**Export Registration**:
- Resources declare exports in manifest
- At runtime, register actual values (functions, tables)
- Exports are validated against manifest declarations

**Import Resolution**:
- Resources request imports by resource name and export name
- Runtime validation ensures exporting resource is loaded and running
- Clear errors when imports cannot be resolved

**Lua API**:

```lua
-- Exporting resource
Exports.register("myFunction", function(arg)
    return arg * 2
end)

Exports.register("myModule", {
    helper = function() end,
    constant = 42
})

-- Importing resource
local myFunc = Exports.get("core-utils", "myFunction")
local result = myFunc(21)  -- returns 42

local mod = Exports.get("core-utils", "myModule")
mod.helper()

-- Discovery
local available = Exports.list("core-utils")  -- {"myFunction", "myModule"}
```

### 3.2 Cross-Resource Events

Extend the event system to support different scopes:

**Event Scopes**:

| Scope | Description | Use Case |
|-------|-------------|----------|
| Local | Within current resource only | Internal resource logic |
| Global | Broadcast to all resources | System-wide notifications |
| Targeted | Sent to specific resource | Direct communication |

**Lua API**:

```lua
-- Local events (current behavior, isolated to resource)
Event.on("playerJoined", function(player) end)
Event.emit("playerJoined", player)

-- Global events (all resources can subscribe)
Event.onGlobal("serverStarted", function() end)
Event.broadcast("serverStarted")

-- Targeted events (sent to specific resource)
Event.emitTo("player-system", "updateScore", playerId, 100)

-- Receiving targeted events
Event.onTargeted("updateScore", function(playerId, score) end)
```

### 3.3 Message Passing

For complex communication patterns, implement an async message queue:

**Features**:
- Structured message format with type and payload
- Request/response pattern with callbacks
- Decoupled communication (sender doesn't require receiver to be loaded)
- Message buffering for temporarily unavailable resources

**Lua API**:

```lua
-- Fire and forget
Message.send("analytics", "trackEvent", { event = "login", userId = 123 })

-- Request/response
Message.request("database", "getPlayer", { id = 123 }, function(response)
    if response.success then
        print("Player name: " .. response.data.name)
    end
end)

-- Handling messages
Message.handle("getPlayer", function(request, reply)
    local player = db:find(request.id)
    reply({ success = true, data = player })
end)
```

---

## Phase 4: Lifecycle Management

### 4.1 Resource Lifecycle Events

Each resource receives these events in order:

| Event | Timing | Purpose |
|-------|--------|---------|
| `onResourceLoad` | Environment created, before scripts run | Early initialization |
| `onResourceStart` | All scripts executed successfully | Resource is fully operational |
| `onResourceStop` | Stop requested, before cleanup | Save state, cleanup timers |
| `onResourceUnload` | Final cleanup, about to be removed | Release external resources |

Additional events for awareness:

| Event | Timing | Purpose |
|-------|--------|---------|
| `onResourceStarted` | Another resource started | React to new resources |
| `onResourceStopped` | Another resource stopped | Handle dependency loss |

### 4.2 Start Sequence

When starting a resource:

1. **Validate Manifest**: Parse and validate manifest.json
2. **Check Dependencies**: Verify all dependencies are loaded and running
3. **Create Environment**: Set up isolated Lua environment table
4. **Load Scripts**: Execute each script file in order within the environment
5. **Register Exports**: Make declared exports available to other resources
6. **Fire onResourceStart**: Notify the resource it's running
7. **Update State**: Set state to Running
8. **Notify Others**: Fire global `onResourceStarted` event

### 4.3 Stop Sequence

When stopping a resource:

1. **Fire onResourceStop**: Allow resource to save state and cleanup
2. **Unregister Event Handlers**: Remove all event subscriptions owned by this resource
3. **Unregister Exports**: Remove exports from the registry
4. **Notify Dependents**: Warn or stop resources that depend on this one
5. **Clear Environment**: Remove resource's environment table
6. **Fire onResourceUnload**: Final cleanup notification
7. **Update State**: Set state to Stopped
8. **Notify Others**: Fire global `onResourceStopped` event

### 4.4 Hot-Reload Per Resource

Extend file watching to support granular reloading:

**File Tracking**:
- Map each file to its owning resource
- File watcher reports changes with full path
- ResourceManager identifies affected resource

**Reload Process**:
1. Detect file change
2. Identify owning resource
3. Fire `onResourceStop` (allows state preservation)
4. Stop only that resource
5. Re-read manifest (may have changed)
6. Restart resource with fresh scripts
7. Fire `onResourceReload` event

**State Preservation** (optional):
- Resource can serialize state in `onResourceStop`
- State passed to `onResourceStart` after reload
- Enables seamless hot-reload experience

---

## Phase 5: Resource Manager API (Lua Bindings)

### 5.1 Resource Control

New `Resource` builtin for Lua:

```lua
-- Lifecycle control
Resource.start("player-system")      -- Load and start a resource
Resource.stop("player-system")       -- Stop a running resource
Resource.restart("player-system")    -- Stop then start

-- State queries
Resource.isRunning("player-system")  -- Returns boolean
Resource.getState("player-system")   -- Returns "running", "stopped", etc.

-- Discovery
Resource.list()                      -- Returns array of all resource names
Resource.getRunning()                -- Returns array of running resource names
Resource.getInfo("player-system")    -- Returns manifest metadata table

-- Self-reference
Resource.getCurrent()                -- Returns current resource's name
Resource.getPath()                   -- Returns current resource's directory path
```

### 5.2 Resource Information

The `getInfo` function returns:

```lua
local info = Resource.getInfo("player-system")
-- info = {
--     name = "player-system",
--     version = "1.0.0",
--     author = "Developer",
--     description = "Handles player logic",
--     state = "running",
--     dependencies = {"core-utils"},
--     exports = {"getPlayer", "setPlayerData"},
--     loadTime = 1234567890,  -- Unix timestamp
--     scriptCount = 3
-- }
```

### 5.3 Permissions System

Resources declare required permissions in manifest:

| Permission | Grants Access To |
|------------|------------------|
| `filesystem` | File read/write operations |
| `network` | HTTP requests, sockets |
| `commands` | Register server commands |
| `entities` | Create/modify game entities |
| `players` | Access player data and controls |
| `database` | Database operations |
| `admin` | Administrative functions |

**Runtime Enforcement**:
- ResourceManager validates permissions before allowing operations
- Unauthorized operations throw errors with clear messages
- Permissions checked at builtin registration time

**Lua API**:

```lua
-- Check if current resource has permission
if Resource.hasPermission("entities") then
    -- Safe to create entities
end

-- Get all permissions for a resource
local perms = Resource.getPermissions("player-system")
```

---

## Phase 6: Error Handling and Resilience

### 6.1 Resource Isolation on Error

When a script error occurs:

**Error Identification**:
- Capture error with full stack trace
- Identify owning resource from execution context
- Tag error logs with resource name

**Containment**:
- Error in one resource doesn't crash others
- Configurable behavior per resource:
  - `continue`: Log error, keep resource running
  - `stop`: Stop the resource, others continue
  - `restart`: Automatically restart the resource

**Logging**:
```
[ERROR] [player-system] server/main.lua:42: attempt to index nil value
    Stack trace:
        server/main.lua:42 in function 'updatePlayer'
        server/main.lua:15 in function 'onPlayerJoin'
```

### 6.2 Dependency Failure Handling

When a dependency fails to load:

**Blocking**:
- Dependent resources marked as "blocked"
- Clear error message identifies missing dependency
- Blocked resources can be retried after dependency is available

**Graceful Degradation**:
- Optional dependencies (marked in manifest) don't block
- Resource notified of missing optional dependency
- Can implement fallback behavior

**Cascade Stop**:
- When a running dependency stops unexpectedly
- Dependents receive `onDependencyLost` event
- Configurable: auto-stop or continue with degraded functionality

### 6.3 Recovery Mechanisms

**Manual Recovery**:
```lua
Resource.restart("failed-resource")
```

**Auto-Restart Policy**:
- Configurable per resource in manifest
- Maximum restart attempts within time window
- Exponential backoff between attempts

**Health Monitoring**:
- Optional health check function per resource
- ResourceManager periodically calls health checks
- Unhealthy resources can trigger alerts or restarts

---

## Phase 7: Client-Side Considerations

### 7.1 Resource Synchronization

Server controls which resources clients load:

**Connection Flow**:
1. Client connects to server
2. Server sends resource list with versions
3. Client compares with cached resources
4. Client downloads new/updated resources
5. Client starts resources in dependency order

**Dynamic Updates**:
- Server can command client to start/stop resources mid-session
- Useful for game state changes (entering new area, etc.)
- Client receives `onServerResourceCommand` event

### 7.2 Client Resource Lifecycle

Mirror server resource lifecycle with client-specific considerations:

**Same State Machine**:
- Unloaded → Loading → Running → Stopping → Stopped
- Same lifecycle events

**Server Control**:
- Server can remotely start/stop client resources
- Client resources can request start/stop (server validates)

**Paired Resources**:
- Many resources have both server and client components
- Same resource name, different file sets
- Can communicate via existing client-server RPC

### 7.3 Asset Management Integration

Extend current asset download system:

**Per-Resource Downloads**:
- Each resource is a downloadable unit
- Separate cache per resource
- Version-based cache invalidation

**Incremental Updates**:
- Only download changed resources on reconnect
- File hashing to detect changes
- Delta updates for large resources (future enhancement)

**Download Priority**:
- Core resources download first
- Optional resources can load lazily
- Progress reporting per resource

---

## Phase 8: Implementation Considerations

### 8.1 File Structure

New files to create:

```
code/framework/src/scripting/
├── resource/
│   ├── resource.h              -- Resource class definition
│   ├── resource.cpp            -- Resource implementation
│   ├── resource_manager.h      -- ResourceManager class
│   ├── resource_manager.cpp    -- ResourceManager implementation
│   ├── resource_manifest.h     -- Manifest parsing and validation
│   ├── resource_manifest.cpp
│   ├── dependency_graph.h      -- DAG implementation
│   ├── dependency_graph.cpp
│   └── environment_sandbox.h   -- Lua environment isolation
├── builtins/
│   ├── resource.h              -- Resource Lua builtin
│   ├── exports.h               -- Exports Lua builtin
│   └── message.h               -- Message Lua builtin (optional)
```

### 8.2 Integration Points

**ServerScriptingModule**:
- Replace direct engine script loading with ResourceManager
- ResourceManager owns the Engine instance
- Manifest loading delegates to ResourceManager

**ClientScriptingModule**:
- Similar integration as server
- Asset download system feeds into ResourceManager
- Server commands routed through ResourceManager

**CoreModules**:
- Add `GetResourceManager()` static accessor
- Resources can access manager for queries

### 8.3 Backward Compatibility

**Legacy Gamemode Support**:
- Detect if resources directory doesn't exist
- Fall back to current single-script behavior
- Treat entire gamemode as one "legacy" resource

**Gradual Migration**:
- Projects can mix legacy and new resource structure
- Legacy code runs in its own environment
- New resources can coexist

---

## Implementation Order

### Stage 1: Foundation
1. Resource class with state machine
2. Resource manifest schema and parser
3. ResourceManager with discovery and registry

### Stage 2: Core Functionality
4. Dependency graph with cycle detection
5. Load order calculation (topological sort)
6. Environment sandboxing implementation

### Stage 3: Lifecycle
7. Start/stop sequences with events
8. Cascade operations for dependencies
9. Integration with existing Engine classes

### Stage 4: Communication
10. Export/Import system
11. Cross-resource events (local, global, targeted)
12. Message passing (optional, can defer)

### Stage 5: Lua API
13. Resource builtin
14. Exports builtin
15. Extended Event builtin

### Stage 6: Resilience
16. Error isolation and logging
17. Dependency failure handling
18. Recovery mechanisms

### Stage 7: Client Integration
19. Resource synchronization protocol
20. Client ResourceManager
21. Asset management integration

### Stage 8: Hot-Reload
22. Per-resource file watching
23. State preservation on reload
24. Reload coordination

---

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Lua states | Single shared state | Simpler bindings, lower memory, easier debugging |
| Isolation | Environment sandboxing | Logical isolation without state duplication |
| Communication | Export/Import + Events | Explicit contracts, loose coupling |
| Dependencies | DAG with topological sort | Handles complex dependency trees correctly |
| Hot-reload | Per-resource granularity | Minimal disruption, faster iteration |
| State machine | Explicit states with transitions | Clear lifecycle, predictable behavior |
| Permissions | Manifest-declared, runtime-enforced | Security without complexity |

---

## Success Criteria

The implementation is complete when:

1. Multiple resources can be loaded independently
2. Resources are properly isolated (no global pollution)
3. Resources can communicate via exports and events
4. Individual resources can be started/stopped without affecting others
5. Hot-reload works per-resource
6. Dependencies are resolved automatically
7. Errors in one resource don't crash others
8. Client resources synchronize with server
9. Existing single-resource gamemodes continue to work
