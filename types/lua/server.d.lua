---@meta

-- MafiaHub Framework: the Lua API of a server resource, for the Lua language server (LuaLS).
-- Generated from the definitions of the builtins. Do not edit.
--
-- A resource runs in a sandbox. Of the standard library it has base, coroutine, table, string, utf8 and
-- math, and of os only time, clock, date and difftime. There is no io, no debug and no package.
--   removed   dofile, loadfile, string.dump
--   changed   load            takes a string chunk only, always compiled as text
--             collectgarbage  answers "count" only
--             print           writes a line to the log of the resource
--             require         reads the files of the resource and nothing else
--             setmetatable    refuses a metatable with __gc
--             coroutine.resume, coroutine.close
--                             refuse the coroutine the host runs the script in
--
-- A field marked [read-only] cannot be assigned. A function marked ---@async waits: the coroutine that
-- calls it is suspended until the result is known, then the call returns it, or raises. Every chunk,
-- handler and timer callback runs in a coroutine.
--
-- What this file cannot say:
--   - the events the framework raises and what their handlers receive (Events.on);
--   - the fields of an option table where a parameter is typed `table`;
--   - the comparisons of a class (==, <, <=) and an operator whose left operand is not the class, which
--     have no annotation: the description of the class names them;
--   - what a class adds for Lua outside its declared members, and what a game adds to all of this.

---Mutable two-dimensional vector.
---
---Also has the operators `number * Vector2`, `Vector2 == Vector2`.
---@class Vector2
---@field x number Mutable X component.
---@field y number Mutable Y component.
---@field length number [read-only] Euclidean magnitude of this vector.
---@field lengthSquared number [read-only] Squared magnitude, avoiding a square-root calculation.
---@operator add(Vector2): Vector2
---@operator sub(Vector2): Vector2
---@operator mul(number): Vector2
---@operator div(number): Vector2
---@operator unm: Vector2
---@overload fun(x: number, y: number): Vector2
Vector2 = {}

---Creates a vector from two numeric components.
---@param x number Initial X component.
---@param y number Initial Y component.
---@return Vector2
function Vector2.new(x, y) end

---Adds another vector to this vector in place.
---@param other Vector2 Vector to add component-wise.
---@return self # This mutated vector for chaining.
function Vector2:add(other) end

---Subtracts another vector from this vector in place.
---@param other Vector2 Vector to subtract component-wise.
---@return self # This mutated vector for chaining.
function Vector2:sub(other) end

---Multiplies this vector by a scalar in place.
---@param scalar number Multiplier applied to both components.
---@return self # This mutated vector for chaining.
function Vector2:mul(scalar) end

---Divides this vector by a scalar in place.
---@param scalar number Divisor applied to both components; must be non-zero.
---@return self # This mutated vector for chaining.
function Vector2:div(scalar) end

---Computes the dot product without changing either vector.
---@param other Vector2 Vector used for the dot product.
---@return number # Scalar dot product.
function Vector2:dot(other) end

---Normalizes this vector in place; a zero vector remains unchanged.
---@return self # This mutated vector for chaining.
function Vector2:normalize() end

---Linearly interpolates this vector toward a target in place.
---@param target Vector2 Destination vector.
---@param t number Interpolation factor; 0 keeps the current value and 1 reaches target.
---@return self # This mutated vector for chaining.
function Vector2:lerp(target, t) end

---Replaces both components in place.
---@param x number Replacement X component.
---@param y number Replacement Y component.
---@return self # This mutated vector for chaining.
function Vector2:set(x, y) end

---Computes Euclidean distance to another vector.
---@param other Vector2 Vector to measure from this vector.
---@return number # Distance between the two vectors.
function Vector2:distance(other) end

---Creates an independent copy of this vector.
---@return Vector2 # New vector with the same components.
function Vector2:clone() end

---Formats this vector for logging and debugging.
---@return string # Text in Vector2(x, y) form.
function Vector2:toString() end

---Creates a vector whose components are zero.
---@return Vector2 # New Vector2(0, 0).
function Vector2.zero() end

---Creates a vector whose components are one.
---@return Vector2 # New Vector2(1, 1).
function Vector2.one() end

---Mutable three-dimensional vector for positions, directions, and Euler angles.
---
---Also has the operators `number * Vector3`, `Vector3 == Vector3`.
---@class Vector3
---@field x number Mutable X component.
---@field y number Mutable Y component.
---@field z number Mutable Z component.
---@field length number [read-only] Euclidean magnitude of this vector.
---@field lengthSquared number [read-only] Squared magnitude, avoiding a square-root calculation.
---@operator add(Vector3): Vector3
---@operator sub(Vector3): Vector3
---@operator mul(number): Vector3
---@operator div(number): Vector3
---@operator unm: Vector3
---@overload fun(x: number, y: number, z: number): Vector3
Vector3 = {}

---Creates a vector from three numeric components.
---@param x number Initial X component.
---@param y number Initial Y component.
---@param z number Initial Z component.
---@return Vector3
function Vector3.new(x, y, z) end

---Adds another vector to this vector in place.
---@param other Vector3 Vector to add component-wise.
---@return self # This mutated vector for chaining.
function Vector3:add(other) end

---Subtracts another vector from this vector in place.
---@param other Vector3 Vector to subtract component-wise.
---@return self # This mutated vector for chaining.
function Vector3:sub(other) end

---Multiplies this vector by a scalar in place.
---@param scalar number Multiplier applied to every component.
---@return self # This mutated vector for chaining.
function Vector3:mul(scalar) end

---Divides this vector by a scalar in place.
---@param scalar number Divisor applied to every component; must be non-zero.
---@return self # This mutated vector for chaining.
function Vector3:div(scalar) end

---Computes the dot product without changing either vector.
---@param other Vector3 Vector used for the dot product.
---@return number # Scalar dot product.
function Vector3:dot(other) end

---Replaces this vector with its cross product against another vector.
---@param other Vector3 Second vector in the cross product.
---@return self # This mutated perpendicular vector for chaining.
function Vector3:cross(other) end

---Normalizes this vector in place; a zero vector remains unchanged.
---@return self # This mutated vector for chaining.
function Vector3:normalize() end

---Linearly interpolates this vector toward a target in place.
---@param target Vector3 Destination vector.
---@param t number Interpolation factor; 0 keeps the current value and 1 reaches target.
---@return self # This mutated vector for chaining.
function Vector3:lerp(target, t) end

---Replaces all components in place.
---@param x number Replacement X component.
---@param y number Replacement Y component.
---@param z number Replacement Z component.
---@return self # This mutated vector for chaining.
function Vector3:set(x, y, z) end

---Computes Euclidean distance to another vector.
---@param other Vector3 Vector to measure from this vector.
---@return number # Distance between the two vectors.
function Vector3:distance(other) end

---Creates an independent copy of this vector.
---@return Vector3 # New vector with the same components.
function Vector3:clone() end

---Formats this vector for logging and debugging.
---@return string # Text in Vector3(x, y, z) form.
function Vector3:toString() end

---Creates a zero vector.
---@return Vector3 # New Vector3(0, 0, 0).
function Vector3.zero() end

---Creates a vector whose components are one.
---@return Vector3 # New Vector3(1, 1, 1).
function Vector3.one() end

---Creates the framework's positive-Y unit direction.
---@return Vector3 # New Vector3(0, 1, 0).
function Vector3.up() end

---Creates the framework's positive-Z unit direction.
---@return Vector3 # New Vector3(0, 0, 1).
function Vector3.forward() end

---Creates the framework's positive-X unit direction.
---@return Vector3 # New Vector3(1, 0, 0).
function Vector3.right() end

---Mutable four-dimensional vector.
---
---Also has the operators `number * Vector4`, `Vector4 == Vector4`.
---@class Vector4
---@field x number Mutable X component.
---@field y number Mutable Y component.
---@field z number Mutable Z component.
---@field w number Mutable W component.
---@field length number [read-only] Euclidean magnitude of this vector.
---@field lengthSquared number [read-only] Squared magnitude, avoiding a square-root calculation.
---@operator add(Vector4): Vector4
---@operator sub(Vector4): Vector4
---@operator mul(number): Vector4
---@operator div(number): Vector4
---@operator unm: Vector4
---@overload fun(x: number, y: number, z: number, w: number): Vector4
Vector4 = {}

---Creates a vector from four numeric components.
---@param x number Initial X component.
---@param y number Initial Y component.
---@param z number Initial Z component.
---@param w number Initial W component.
---@return Vector4
function Vector4.new(x, y, z, w) end

---Adds another vector to this vector in place.
---@param other Vector4 Vector to add component-wise.
---@return self # This mutated vector for chaining.
function Vector4:add(other) end

---Subtracts another vector from this vector in place.
---@param other Vector4 Vector to subtract component-wise.
---@return self # This mutated vector for chaining.
function Vector4:sub(other) end

---Multiplies this vector by a scalar in place.
---@param scalar number Multiplier applied to every component.
---@return self # This mutated vector for chaining.
function Vector4:mul(scalar) end

---Divides this vector by a scalar in place.
---@param scalar number Divisor applied to every component; must be non-zero.
---@return self # This mutated vector for chaining.
function Vector4:div(scalar) end

---Computes the dot product without changing either vector.
---@param other Vector4 Vector used for the dot product.
---@return number # Scalar dot product.
function Vector4:dot(other) end

---Computes Euclidean distance to another vector.
---@param other Vector4 Vector to measure from this vector.
---@return number # Distance between the two vectors.
function Vector4:distance(other) end

---Normalizes this vector in place; a zero vector remains unchanged.
---@return self # This mutated vector for chaining.
function Vector4:normalize() end

---Linearly interpolates this vector toward a target in place.
---@param target Vector4 Destination vector.
---@param t number Interpolation factor; 0 keeps the current value and 1 reaches target.
---@return self # This mutated vector for chaining.
function Vector4:lerp(target, t) end

---Replaces all components in place.
---@param x number Replacement X component.
---@param y number Replacement Y component.
---@param z number Replacement Z component.
---@param w number Replacement W component.
---@return self # This mutated vector for chaining.
function Vector4:set(x, y, z, w) end

---Creates an independent copy of this vector.
---@return Vector4 # New vector with the same components.
function Vector4:clone() end

---Formats this vector for logging and debugging.
---@return string # Text in Vector4(x, y, z, w) form.
function Vector4:toString() end

---Creates a zero vector.
---@return Vector4 # New Vector4(0, 0, 0, 0).
function Vector4.zero() end

---Creates a vector whose components are one.
---@return Vector4 # New Vector4(1, 1, 1, 1).
function Vector4.one() end

---Mutable quaternion for three-dimensional rotations. Components are scalar-first (w, x, y, z), matching GLM — not the x, y, z, w order some libraries use.
---
---Also has the operators `Quaternion == Quaternion`.
---@class Quaternion
---@field w number Mutable scalar component.
---@field x number Mutable X imaginary component.
---@field y number Mutable Y imaginary component.
---@field z number Mutable Z imaginary component.
---@field length number [read-only] Magnitude (norm) of this quaternion; 1 for a unit rotation.
---@field lengthSquared number [read-only] Squared magnitude, avoiding a square-root calculation.
---@operator mul(Quaternion): Quaternion
---@overload fun(w: number, x: number, y: number, z: number): Quaternion
Quaternion = {}

---Creates a quaternion in scalar-first w, x, y, z component order. Watch the trap: the scalar w is the first argument, unlike the x, y, z, w order used by some other libraries.
---@param w number Initial scalar component (comes first — this is not x, y, z, w order).
---@param x number Initial X imaginary component.
---@param y number Initial Y imaginary component.
---@param z number Initial Z imaginary component.
---@return Quaternion
function Quaternion.new(w, x, y, z) end

---Composes this rotation with another quaternion in place.
---@param other Quaternion Rotation composed after this quaternion.
---@return self # This mutated quaternion for chaining.
function Quaternion:mul(other) end

---Normalizes this quaternion in place; a zero quaternion becomes the identity rotation instead of NaN.
---@return self # This unit quaternion for chaining.
function Quaternion:normalize() end

---Computes the conjugate without changing this quaternion.
---@return Quaternion # New conjugated quaternion.
function Quaternion:conjugate() end

---Computes the inverse rotation without changing this quaternion.
---@return Quaternion # New inverse quaternion.
function Quaternion:inverse() end

---Spherically interpolates this quaternion toward a target in place.
---@param target Quaternion Destination rotation.
---@param t number Interpolation factor; 0 keeps the current rotation and 1 reaches target.
---@return self # This mutated quaternion for chaining.
function Quaternion:slerp(target, t) end

---Computes the quaternion dot product.
---@param other Quaternion Quaternion used for the dot product.
---@return number # Scalar dot product.
function Quaternion:dot(other) end

---Applies this rotation to a vector without mutating either value.
---@param vector Vector3 Vector to rotate.
---@return Vector3 # New rotated vector.
function Quaternion:rotateVector(vector) end

---Converts this rotation to Euler angles in radians.
---@return Vector3 # Pitch, yaw, and roll as a Vector3.
function Quaternion:toEuler() end

---Replaces all quaternion components in place.
---@param w number Replacement scalar component.
---@param x number Replacement X imaginary component.
---@param y number Replacement Y imaginary component.
---@param z number Replacement Z imaginary component.
---@return self # This mutated quaternion for chaining.
function Quaternion:set(w, x, y, z) end

---Creates an independent copy of this quaternion.
---@return Quaternion # New quaternion with the same components.
function Quaternion:clone() end

---Formats this quaternion for logging and debugging.
---@return string # Text in Quaternion(w, x, y, z) form.
function Quaternion:toString() end

---Creates the identity rotation.
---@return Quaternion # New Quaternion(1, 0, 0, 0).
function Quaternion.identity() end

---Creates a rotation from Euler angles.
---@param pitch number Pitch angle in radians.
---@param yaw number Yaw angle in radians.
---@param roll number Roll angle in radians.
---@return Quaternion # New rotation quaternion.
function Quaternion.fromEuler(pitch, yaw, roll) end

---Creates a rotation around an axis.
---@param axis Vector3 Rotation axis; it is normalized internally.
---@param angle number Rotation angle in radians.
---@return Quaternion # New axis-angle rotation quaternion.
function Quaternion.fromAxisAngle(axis, angle) end

---Mutable RGBA color, with components stored from 0 to 1.
---
---Also has the operators `Color == Color`.
---@class Color
---@field r number Mutable normalized red component.
---@field g number Mutable normalized green component.
---@field b number Mutable normalized blue component.
---@field a number Mutable normalized alpha component.
---@overload fun(r: number, g: number, b: number, a?: number): Color
Color = {}

---Creates a color from normalized RGBA components.
---@param r number Initial red component from 0 to 1.
---@param g number Initial green component from 0 to 1.
---@param b number Initial blue component from 0 to 1.
---@param a? number Optional alpha component from 0 to 1; defaults to 1.
---@return Color
function Color.new(r, g, b, a) end

---Linearly interpolates every component toward a target in place.
---@param target Color Destination color.
---@param t number Interpolation factor; 0 keeps this color and 1 reaches target.
---@return self # This mutated color for chaining.
function Color:lerp(target, t) end

---Replaces this color's normalized components in place.
---@param r number Replacement red component from 0 to 1.
---@param g number Replacement green component from 0 to 1.
---@param b number Replacement blue component from 0 to 1.
---@param a? number Optional replacement alpha; defaults to 1 when omitted.
---@return self # This mutated color for chaining.
function Color:set(r, g, b, a) end

---Creates an independent copy of this color.
---@return Color # New color with the same components.
function Color:clone() end

---Converts normalized components to a hexadecimal CSS-style string.
---@param includeAlpha? boolean Whether to append the alpha byte; defaults to false.
---@return string # Lowercase #rrggbb or #rrggbbaa string.
function Color:toHex(includeAlpha) end

---Formats this color for logging and debugging.
---@return string # Text in Color(r, g, b, a) form.
function Color:toString() end

---Parses a hexadecimal color string.
---@param hex string Hexadecimal color in #RRGGBB or #RRGGBBAA form; the leading # is optional. Three- and four-digit shorthands are not supported.
---@return Color # Parsed color, or opaque white when the input is invalid.
function Color.fromHex(hex) end

---Creates a normalized color from byte components.
---@param r number Red byte from 0 to 255.
---@param g number Green byte from 0 to 255.
---@param b number Blue byte from 0 to 255.
---@param a? number Optional alpha byte from 0 to 255; defaults to 255.
---@return Color # New normalized color.
function Color.fromRGB(r, g, b, a) end

---Creates opaque white.
---@return Color # New Color(1, 1, 1, 1).
function Color.white() end

---Creates opaque black.
---@return Color # New Color(0, 0, 0, 1).
function Color.black() end

---Creates opaque red.
---@return Color # New Color(1, 0, 0, 1).
function Color.red() end

---Creates opaque green.
---@return Color # New Color(0, 1, 0, 1).
function Color.green() end

---Creates opaque blue.
---@return Color # New Color(0, 0, 1, 1).
function Color.blue() end

---Creates opaque yellow.
---@return Color # New Color(1, 1, 0, 1).
function Color.yellow() end

---Creates opaque cyan.
---@return Color # New Color(0, 1, 1, 1).
function Color.cyan() end

---Creates opaque magenta.
---@return Color # New Color(1, 0, 1, 1).
function Color.magenta() end

---Creates fully transparent black.
---@return Color # New Color(0, 0, 0, 0).
function Color.transparent() end

---Asynchronous resource event bus.
---@class Events
Events = {}

---Registers a persistent handler in the shared event namespace.
---@param eventName string Case-sensitive event name.
---@param handler fun(...: any): any Resource-owned callback invoked with the emitted arguments.
---@return fun() # Function that removes this exact subscription.
function Events.on(eventName, handler) end

---Registers a handler that is removed before its first invocation.
---@param eventName string Case-sensitive event name.
---@param handler fun(...: any): any Resource-owned callback invoked with the emitted arguments.
function Events.once(eventName, handler) end

---Removes a matching handler owned by the calling resource.
---@param eventName string Case-sensitive event name.
---@param handler fun(...: any): any Resource-owned callback invoked with the emitted arguments.
function Events.off(eventName, handler) end

---Invokes every shared handler and waits for all synchronous and asynchronous results.
---@async
---@param eventName string Shared event name.
---@param ... any Arguments delivered to every matching handler.
function Events.emit(eventName, ...) end

---Invokes matching handlers belonging only to one resource.
---@async
---@param resourceName string Destination running resource.
---@param eventName string Shared event name.
---@param ... any Arguments delivered to matching handlers owned by the destination.
function Events.emitTo(resourceName, eventName, ...) end

---Registers a handler in the calling resource's private local-event namespace.
---@param eventName string Case-sensitive event name.
---@param handler fun(...: any): any Resource-owned callback invoked with the emitted arguments.
function Events.onLocal(eventName, handler) end

---Emits an event only within the calling resource.
---@async
---@param eventName string Private local event name.
---@param ... any Arguments delivered only to handlers owned by the calling resource.
function Events.emitLocal(eventName, ...) end

---Counts persistent and one-shot shared handlers across resources.
---@param eventName string Shared event name to inspect.
---@return number # Number of matching handlers.
function Events.listenerCount(eventName) end

---Broadcasts a named event to every connected client's shared Events handlers.
---@param eventName string Client shared-event name.
---@param payload? any Optional payload. A string is sent as is and must already be JSON text: each client parses it and drops an event that does not parse. Other values are JSON-serialized.
function Events.emitAllClients(eventName, payload) end

---Registers a persistent server handler for events originating from clients; this namespace is isolated from native events.
---@param eventName string Case-sensitive event name.
---@param handler fun(player: Player, payload?: any): any Resource-owned callback invoked with the sending player and the parsed payload, which is absent when the client sent none. The payload is untrusted.
---@return fun() # Function that removes this exact subscription.
function Events.onClient(eventName, handler) end

---Registers a one-shot server handler for a client-originated event.
---@param eventName string Case-sensitive event name.
---@param handler fun(player: Player, payload?: any): any Resource-owned callback invoked with the sending player and the parsed payload, which is absent when the client sent none. The payload is untrusted.
function Events.onceClient(eventName, handler) end

---Removes a matching client-originated event handler owned by the calling resource.
---@param eventName string Case-sensitive event name.
---@param handler fun(player: Player, payload?: any): any Resource-owned callback invoked with the sending player and the parsed payload, which is absent when the client sent none. The payload is untrusted.
function Events.offClient(eventName, handler) end

---Typed request and notification channel between local resources through the global Messages object.
---@class Messages
Messages = {}

---Registers or replaces a message handler owned by the calling resource.
---@param messageType string Message type unique within the receiving resource.
---@param handler fun(payload: any, reply: fun(response?: any)) Handler invoked with the payload and a reply callback; the reply is ignored for notifications.
function Messages.handle(messageType, handler) end

---Sends a request to another local resource and waits for its handler to call reply.
---@async
---@param resourceName string Destination running resource.
---@param messageType string Handler type registered by the destination.
---@param payload? any Optional payload delivered to the handler.
---@return any # Promise resolved with the reply value or rejected when delivery or handling fails.
function Messages.request(resourceName, messageType, payload) end

---Sends a fire-and-forget notification to another local resource.
---@param resourceName string Destination running resource.
---@param messageType string Handler type registered by the destination.
---@param payload? any Optional payload delivered to the handler.
function Messages.send(resourceName, messageType, payload) end

---Bulk access to values exported by another running resource through the global Imports object.
---@class Imports
Imports = {}

---Builds an object containing every currently registered export from another resource; undeclared dependencies produce a warning.
---@param resourceName string Name of the running resource whose exports should be read.
---@return table<string, any> # Object keyed by export name, or an empty object when the resource has no exports.
function Imports.get(resourceName) end

---Registration and lookup of cross-resource values through the global Exports object.
---@class Exports
Exports = {}

---Registers a value from the calling resource for use by dependent resources.
---@param name string Export name, preferably declared in the current resource manifest.
---@param value any JavaScript value or function retained by the current resource.
---@return boolean # True when the value was registered.
function Exports.register(name, value) end

---Reads one registered export from another running resource; undeclared dependencies produce a warning.
---@param resourceName string Name of the running resource that owns the export.
---@param exportName string Registered export name.
---@return any # The exported value.
function Exports.get(resourceName, exportName) end

---Resource-aware console that routes output through the Framework logger.
---@class console
console = {}

---Writes an informational log entry prefixed with the current resource name.
---@param ... any Values formatted and joined with spaces.
function console.log(...) end

---Alias of console.log for informational output.
---@param ... any Values formatted and joined with spaces.
function console.info(...) end

---Writes a warning log entry prefixed with the current resource name.
---@param ... any Values formatted and joined with spaces.
function console.warn(...) end

---Writes an error log entry prefixed with the current resource name.
---@param ... any Values formatted and joined with spaces.
function console.error(...) end

---Writes a debug log entry prefixed with the current resource name.
---@param ... any Values formatted and joined with spaces.
function console.debug(...) end

---Runtime-side flags exposed as the global ExecutionEnvironment.
---@class ExecutionEnvironment
---@field isClient boolean [read-only] True in the sandboxed client scripting runtime.
---@field isServer boolean [read-only] True in the authoritative server scripting runtime.
---@field frameworkVersion string [read-only] Local Framework release version.
---@field modVersion string [read-only] Local mod version from InstanceOptions.modVersion; empty when unset.
ExecutionEnvironment = {}

---Arbitrary key/value state attached to one replicated entity, reached as `entity.state`. Keys set on the server replicate to every client that can currently see the entity.
---
---Not a global: a script is given the objects of this class and cannot create one.
---@class StateBag
local StateBag = {}

---Reads one key from this entity's state.
---@param key string Key to read.
---@return any # The stored value, or nil when the key is not set.
function StateBag:get(key) end

---Checks whether this entity's state holds a key.
---@param key string Key to test.
---@return boolean # True when the key is set.
function StateBag:has(key) end

---Lists the keys this entity's state holds, sorted.
---@return string[] # Every key currently set, in ascending order.
function StateBag:keys() end

---Copies this entity's whole state into a plain object.
---@return table<string, any> # Every key and value currently set. The copy does not track later changes.
function StateBag:toObject() end

---Watches this entity's state. The filter is applied before the handler runs, so a listener watching one key of one entity is not woken by unrelated changes. The subscription is dropped when the registering resource stops.
---@param key string? Only report this key, or nil to report every key of this entity.
---@param handler fun(key: string, value: any, previous: any) Called as (key: string, value: any, previous: any) with the changed key, its new value and the value before it. `value` is nil when the key was removed and `previous` is nil when it held nothing.
---@return fun() # A zero-argument function that cancels this subscription.
function StateBag:onChange(key, handler) end

---Writes one key of this entity's state and replicates it to whoever the scope names. Throws when a limit is reached.
---@param key string Key to write; at most 64 UTF-8 bytes.
---@param value any Value to store. Booleans, numbers and strings travel as themselves; anything else is serialized as JSON. At most 4096 bytes.
---@param options? { scope?: 'broadcast'|'owner'|'server' } Who the key reaches: every client that can see the entity (the default), only its owning client, or nobody -- server-side storage that never goes on the wire.
---@return boolean # True when the value changed, false when it was already stored.
function StateBag:set(key, value, options) end

---Removes one key from this entity's state, telling every client that held it.
---@param key string Key to drop.
---@return boolean # True when the key was set and has been removed.
function StateBag:remove(key) end

---Base handle for a live replicated network entity.
---@class Entity
---@field id integer [read-only] Immutable network entity identifier.
---@field virtualWorld integer [read-only] Current virtual-world identifier.
---@field position Vector3 Authoritative world-space position; assignment forces replicated state.
---@field rotation Quaternion|Vector3 Authoritative rotation; reads return a quaternion and assignments accept a quaternion or Euler angles in degrees.
---@field state StateBag [read-only] Arbitrary key/value state carried by this entity. The server writes it and every client that can see the entity receives it; see StateBag.
---@overload fun(id: integer): Entity
Entity = {}

---Creates a script wrapper for an existing entity with this ID; it does not spawn an entity.
---@param id integer Network entity identifier.
---@return Entity
function Entity.new(id) end

---Formats this entity handle for logging and debugging.
---@return string # Text containing the network entity ID.
function Entity:toString() end

---Moves this entity into another virtual world.
---@param world integer Virtual-world identifier used to partition replication and visibility.
function Entity:setVirtualWorld(world) end

---Restricts replication of this entity to one owning connection while preserving normal range and visibility checks.
---@param player Entity? Player-owned entity whose connection should exclusively receive this entity, or nil to clear the restriction.
function Entity:setVisibleTo(player) end

---Framework-owned base handle for a connected player, extended by each game or mod.
---@class Player: Entity
---@field steamId string [read-only] Authenticated Steam identifier, or an empty string when unavailable.
---@field epicId string [read-only] Epic Games account identifier the client reported, not verified by the server, or an empty string when unavailable.
---@field discordId string [read-only] Authenticated Discord identifier, or an empty string when unavailable.
---@field discordName string [read-only] Discord username the client reported, not verified by the server, or an empty string when unavailable.
---@field hardwareId string [read-only] Framework hardware identifier, or an empty string when unavailable.
---@field ping integer [read-only] Current round-trip latency in milliseconds, or -1 when unavailable.
---@field ip string [read-only] Current remote network address, or an empty string when unavailable.
---@overload fun(id: integer): Player
Player = {}

---Creates a wrapper for an existing connected player with this ID; it does not connect or spawn a player.
---@param id integer Network entity identifier.
---@return Player
function Player.new(id) end

---Formats this player handle for logging and debugging.
---@return string # Text containing the player's network entity ID.
function Player:toString() end

---Checks whether this player's nametag is shown to other players.
---@return boolean # True unless the nametag was hidden; false also when this game has no nametags.
function Player:isNametagVisible() end

---Checks whether the health bar under this player's nametag is shown.
---@return boolean # True unless the health bar was hidden; false also when this game has no nametags.
function Player:isNametagHealthVisible() end

---Reads this player's nametag text override.
---@return string # The override, or an empty string when the player's own name is drawn.
function Player:getNametagText() end

---Reads this player's nametag color.
---@return integer # Packed 0xAARRGGBB color; opaque white when untinted.
function Player:getNametagColor() end

---Disconnects this player from the server.
---@param reason? string Optional reason shown to the disconnected player; omitting it uses the generic kicked reason.
function Player:kick(reason) end

---Emits a named script event to this player's client connection.
---@param eventName string Client event name.
---@param payloadJson? string Optional JSON payload forwarded verbatim to the owning client.
function Player:emit(eventName, payloadJson) end

---Returns this player's current remote network address.
---@return string # Address string, or an empty string when the player or peer is unavailable.
function Player:getIP() end

---Shows or hides the name over this player's head for every other player. The health bar has its own switch, and each player can still hide all nametags locally.
---@param visible boolean True to show this player's nametag to everyone, false to hide it.
function Player:setNametagVisible(visible) end

---Shows or hides the health bar under this player's nametag, leaving the name itself alone.
---@param visible boolean True to show the health bar under this player's name, false to hide it.
function Player:setNametagHealthVisible(visible) end

---Overrides the text drawn on this player's nametag.
---@param text? string Text to show instead of the player's name, cut to 64 bytes; empty or omitted restores the name.
function Player:setNametagText(text) end

---Tints the text on this player's nametag.
---@param color integer Packed 0xAARRGGBB color.
function Player:setNametagColor(color) end

---Server-created replicated world-space text label.
---@class TextLabel: Entity
---@field text string [read-only] Current replicated label text.
---@field color integer [read-only] Current packed ARGB color.
---@field fontSize number [read-only] Current font size.
---@field drawDistance number [read-only] Current maximum rendering distance in world units.
---@field fadeDistance number [read-only] Distance in world units where fading starts.
---@field style integer [read-only] The preset nearest the current look: 0 none, 1 shadow, 2 outline, 3 box.
---@overload fun(id: integer): TextLabel
TextLabel = {}

---Creates a wrapper for an existing text label; use TextLabel.create to spawn one.
---@param id integer Existing text-label network entity identifier.
---@return TextLabel
function TextLabel.new(id) end

---Formats this label for logging and debugging.
---@return string # Text containing the label ID and current text.
function TextLabel:toString() end

---Destroys this replicated label; stale wrappers no longer resolve afterward.
function TextLabel:destroy() end

---Changes the label's displayed text.
---@param text string Replacement text replicated to clients; at most 1024 bytes.
function TextLabel:setText(text) end

---Changes the rendered font size.
---@param size number Font size in pixels, clamped from 1 to 512.
function TextLabel:setFontSize(size) end

---Changes how far away clients can render the label.
---@param distance number Maximum rendering distance in world units, clamped from 1 to 1000 metres' worth.
function TextLabel:setDrawDistance(distance) end

---Changes where the label starts fading out.
---@param distance number Distance in world units where fading starts; 0 starts it at 75% of the draw distance.
function TextLabel:setFadeDistance(distance) end

---Resets the outline, shadow and background to a preset.
---@param style integer 0 none, 1 shadow, 2 outline, 3 box; unsupported values are ignored.
function TextLabel:setStyle(style) end

---Stops following an entity; the label returns to its own position.
function TextLabel:detach() end

---Changes the label's packed ARGB color from a Color or byte components.
---@param colorOrR Color|integer A Color, or the red byte (0-255) when passing components.
---@param g? integer Green byte (0-255) when passing components.
---@param b? integer Blue byte (0-255) when passing components.
---@param a? integer Alpha byte (0-255) when passing components; defaults to 255.
function TextLabel:setColor(colorOrR, g, b, a) end

---Changes any part of the label at once. A bad key throws and changes nothing.
---@param options table Only the keys given change; outline, shadow, background, showTo and hideFrom are replaced whole.
function TextLabel:update(options) end

---Follows another live entity instead of staying at the label's own position. It streams from where the target is, moves into its virtual world and is destroyed with it.
---@param target Entity|integer The entity to follow, or its network id.
---@param offset? Vector3|{ x: number, y: number, z: number } Where to draw relative to it; the game decides the reference point (a player's head, say).
function TextLabel:attachTo(target, offset) end

---The label's current look and placement, in the shape update() takes. The audience is left out: see getAudience().
---@return table
function TextLabel:getOptions() end

---Who the label is for, with players as network ids. For reading: a connected player with no avatar right now has no id and is left out, though the label stays as set for them, so passing this to update() would drop them.
---@return { showTo: integer[]?, hideFrom: integer[], hideForTarget: boolean }
function TextLabel:getAudience() end

---Shows or hides the label for one player. A hidden player's client is never sent it; hideForTarget still applies.
---@param player Player|integer A connected player, or their network id.
---@param visible boolean false adds them to hideFrom and takes them off showTo; true undoes both, adding them to showTo while it is set.
function TextLabel:setVisibleFor(player, visible) end

---Creates and replicates a world-space text label, up to the server-wide limit of 512. A bad option throws and creates nothing.
---@param x number World-space X coordinate.
---@param y number World-space Y coordinate.
---@param z number World-space Z coordinate.
---@param text string Initial displayed text.
---@param fontSizeOrOptions? number|table Look and placement, or (the older form) the font size from 1 to 512.
---@param drawDistance? number Older form only: the draw distance in world units.
---@param virtualWorld? integer Older form only: the virtual-world identifier.
---@return TextLabel # Handle for the newly created label.
function TextLabel.create(x, y, z, text, fontSizeOrOptions, drawDistance, virtualWorld) end

---Server-side delivery of structured chat messages to connected clients.
---@class Chat
Chat = {}

---Broadcasts a structured chat message to all clients.
---@param text string Message body broadcast to every connected client.
---@param options? { author: string?, color: (integer|Color)? } Optional author label and color: a Color or a packed 0xRRGGBBAA integer.
function Chat.sendToAll(text, options) end

---Sends a structured chat message to one player's owning connection.
---@param player Entity Player-owned entity identifying the destination connection.
---@param text string Message body sent to the player.
---@param options? { author: string?, color: (integer|Color)? } Optional author label and color: a Color or a packed 0xRRGGBBAA integer.
function Chat.sendToPlayer(player, text, options) end

---Server-side proximity voice rules: how far voice carries, and who may talk to or hear whom.
---@class Voice
Voice = {}

---Sets how far voice carries for talkers with no override of their own. Connected clients are told, so their playback fades out at the same distance.
---@param range number Audibility radius in world units; values <= 0 restore the default of 25.
function Voice.setRange(range) end

---Reads the server-wide proximity range.
---@return number # Radius in world units.
function Voice.getRange() end

---Overrides how far one player's voice carries, whichever voice tier they chose.
---@param player Entity Player whose voice carries the given distance.
---@param range number Audibility radius in world units; values <= 0 return them to the radius of their voice tier.
function Voice.setPlayerRange(player, range) end

---Reads how far a player's voice carries, with the server-wide default already resolved.
---@param player Entity Player to query.
---@return number # Radius in world units.
function Voice.getPlayerRange(player) end

---Sets how far a voice tier carries. Players switch tier themselves; whisper starts at 3, shout at 25, and normal carries the server-wide range. Set a tier to 0 to make it carry the server-wide range, which takes it away. Connected clients are told.
---@param tier number Voice tier: 0 whisper, 1 normal, 2 shout.
---@param range number Audibility radius in world units; values <= 0 make the tier carry the server-wide range.
function Voice.setTierRange(tier, range) end

---Reads how far a voice tier carries, with the server-wide range already resolved.
---@param tier number Voice tier: 0 whisper, 1 normal, 2 shout.
---@return number # Radius in world units.
function Voice.getTierRange(tier) end

---Puts a player on a voice tier, as if they had switched to it. Their own indicator follows, and the playerVoiceTierChange event is not raised.
---@param player Entity Player to move.
---@param tier number Voice tier: 0 whisper, 1 normal, 2 shout.
function Voice.setPlayerTier(player, tier) end

---Reads the voice tier a player is on. A player switching tier raises the playerVoiceTierChange event with the player and the new tier.
---@param player Entity Player to query.
---@return number # 0 whisper, 1 normal, 2 shout.
function Voice.getPlayerTier(player) end

---Server-wide mute: a muted player's voice reaches nobody.
---@param player Entity Player to mute or unmute.
---@param muted boolean True to stop their voice reaching anyone.
function Voice.setPlayerMuted(player, muted) end

---Checks the server-wide mute flag.
---@param player Entity Player to query.
---@return boolean # True when the player's voice reaches nobody.
function Voice.isPlayerMuted(player) end

---Server-wide deafen: a deaf player receives nobody.
---@param player Entity Player to deafen or undeafen.
---@param deaf boolean True to stop them receiving anyone's voice.
function Voice.setPlayerDeaf(player, deaf) end

---Checks the server-wide deafen flag.
---@param player Entity Player to query.
---@return boolean # True when the player receives nobody's voice.
function Voice.isPlayerDeaf(player) end

---One-way mute between two players, enforced by the server rather than the client.
---@param listener Entity Player who stops hearing the target.
---@param target Entity Player the listener stops hearing.
---@param muted boolean True to mute, false to restore.
function Voice.setLocalMute(listener, target, muted) end

---Checks a one-way mute.
---@param listener Entity Player doing the muting.
---@param target Entity Player being muted.
---@return boolean # True when the listener does not receive the target.
function Voice.isLocallyMuted(listener, target) end

---Checks whether a player left voice chat enabled in their own client settings. A preference, not a permission: use setPlayerMuted or setPlayerDeaf to enforce anything.
---@param player Entity Player to query.
---@return boolean # True unless the player turned voice chat off.
function Voice.isPlayerVoiceEnabled(player) end

---Checks whether a player is speaking right now. Tracks speech rather than the push-to-talk key: silence is dropped before it reaches the server, and the state clears shortly after the last frame. The same signal raises the playerVoiceStart and playerVoiceStop events.
---@param player Entity Player to query.
---@return boolean # True while the player's voice is reaching the server.
function Voice.isPlayerTalking(player) end

---A player asking to join, handed to `playerConnecting`. Every identifier is reported by the player's own client and is not verified by the server.
---
---Not a global: a script is given the objects of this class and cannot create one.
---@class PendingConnection
---@field nickname string [read-only] The name the player asked to join under.
---@field steamId string [read-only] Steam identifier the client reported, or an empty string when it had none.
---@field epicId string [read-only] Epic Games account identifier the client reported, or an empty string when it had none.
---@field discordId string [read-only] Discord identifier the client reported, or an empty string when it had none.
---@field discordName string [read-only] Discord username the client reported, or an empty string when it had none.
---@field hardwareId string [read-only] Framework hardware identifier the client reported, or an empty string when it had none.
---@field ticket string [read-only] The string the client was launched with, as the `ticket` of its launch link, or an empty string. The framework passes it through untouched; checking it -- against a one-time join ticket your own launcher or website issued, typically -- is the script's job.
---@field ip string [read-only] The remote address the connection comes from, without its port.
local PendingConnection = {}

---Turns the player away before they receive anything. Takes effect on the next server tick, whatever other handlers are still doing. Does nothing once the connection is decided or the player has left.
---@param reason? string Shown to the player as written, up to 512 bytes. Omitted, they read that the server refused the connection.
function PendingConnection:reject(reason) end

---Shows the waiting player a line of status and restarts the admission timeout, so a queue that keeps updating its players is never timed out. Does nothing once the connection is decided or the player has left.
---@param message string One line for the player's connecting screen, up to 256 bytes: 'Checking the whitelist', 'You are 4th in the queue'.
function PendingConnection:update(message) end

---Checks whether this connection is still waiting on a decision.
---@return boolean # False once it was let in or turned away, or the player gave up and left; a queue drops such entries.
function PendingConnection:isPending() end

---Calls a function once after a delay, in a coroutine of its own.
---@param callback fun(...: any) Function to call.
---@param milliseconds? number Delay before the call; none when it is missing or below 1.
---@param ... any Arguments passed to the callback.
---@return integer # Id to pass to clearTimeout.
function setTimeout(callback, milliseconds, ...) end

---Calls a function repeatedly, waiting the delay between calls, each time in a coroutine of its own.
---@param callback fun(...: any) Function to call.
---@param milliseconds? number Delay between calls; 1 when it is missing or below 1.
---@param ... any Arguments passed to the callback.
---@return integer # Id to pass to clearInterval.
function setInterval(callback, milliseconds, ...) end

---Cancels a pending setTimeout.
---@param id? integer Id returned when the timer was set; anything else is ignored.
function clearTimeout(id) end

---Cancels a setInterval.
---@param id? integer Id returned when the timer was set; anything else is ignored.
function clearInterval(id) end

---Suspends the calling coroutine for a time. Everything else goes on running meanwhile.
---@async
---@param milliseconds? number How long to wait; until the next tick when it is missing or below 1.
function sleep(milliseconds) end

---Runs a function in a coroutine of its own, at once and as far as its first wait. What it raises is not raised in the caller.
---@param callback fun(...: any) Function to run.
---@param ... any Arguments passed to the function.
function spawn(callback, ...) end

---Writes its arguments to the log of the resource, on one line, each as tostring spells it.
---@param ... any Values to write.
function print(...) end

---Loads a Lua file of the resource once and returns what it returned, true when it returned nothing. Nothing outside the resource can be loaded.
---@param name string Name of the module: the path of the file below the root of the resource, without `.lua` and with a dot between directories.
---@return any # What the file returned.
function require(name) end
