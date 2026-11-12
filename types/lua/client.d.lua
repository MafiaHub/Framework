---@meta

-- MafiaHub Framework: the Lua API of a client resource, for the Lua language server (LuaLS).
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
-- A function marked ---@async waits: the coroutine that calls it is suspended until the result is known,
-- then the call returns it, or raises. Every chunk, handler and timer callback runs in a coroutine.
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
---@field length number Read-only. Read-only Euclidean magnitude of this vector.
---@field lengthSquared number Read-only. Read-only squared magnitude, avoiding a square-root calculation.
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
---@field length number Read-only. Read-only Euclidean magnitude of this vector.
---@field lengthSquared number Read-only. Read-only squared magnitude, avoiding a square-root calculation.
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
---@field length number Read-only. Read-only Euclidean magnitude of this vector.
---@field lengthSquared number Read-only. Read-only squared magnitude, avoiding a square-root calculation.
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
---@field length number Read-only. Read-only magnitude (norm) of this quaternion; 1 for a unit rotation.
---@field lengthSquared number Read-only. Read-only squared magnitude, avoiding a square-root calculation.
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

---Sends a named event from this client to the server's isolated onClient handlers.
---@param eventName string Server-side client-event name.
---@param payload? any Optional payload. A string is sent as is and must already be JSON text: the server parses it and drops an event that does not parse. Other values are JSON-serialized.
function Events.emitServer(eventName, payload) end

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
---@field isClient boolean Read-only. True in the sandboxed client scripting runtime.
---@field isServer boolean Read-only. True in the authoritative server scripting runtime.
---@field frameworkVersion string Read-only. Local Framework release version.
---@field modVersion string Read-only. Local mod version from InstanceOptions.modVersion; empty when unset.
ExecutionEnvironment = {}

---Arbitrary key/value state attached to one replicated entity, reached as `entity.state`. Keys set on the server replicate to every client that can currently see the entity.
---
---Not a global: a script is given the objects of this class and cannot create one.
---@class StateBag
local StateBag = {}

---Reads one key from this entity's state.
---@param key string Key to read.
---@return any # The stored value, or undefined when the key is not set.
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
---@param key string? Only report this key, or null to report every key of this entity.
---@param handler fun(key: string, value: any, previous: any) Called as (key: string, value: any, previous: any) with the changed key, its new value and the value before it. `value` is undefined when the key was removed and `previous` is undefined when it held nothing.
---@return fun() # A zero-argument function that cancels this subscription.
function StateBag:onChange(key, handler) end

---Base handle for a live replicated network entity.
---@class Entity
---@field id integer Read-only. Immutable network entity identifier.
---@field virtualWorld integer Read-only. Current virtual-world identifier.
---@field position Vector3 Authoritative world-space position; assignment forces replicated state.
---@field rotation Quaternion|Vector3 Authoritative rotation; reads return a quaternion and assignments accept a quaternion or Euler angles in degrees.
---@field state StateBag Read-only. Arbitrary key/value state carried by this entity. The server writes it and every client that can see the entity receives it; see StateBag.
---@overload fun(id: integer): Entity
Entity = {}

---Creates a script wrapper for an existing entity with this ID; it does not spawn an entity.
---@param id integer Network entity identifier.
---@return Entity
function Entity.new(id) end

---Formats this entity handle for logging and debugging.
---@return string # Text containing the network entity ID.
function Entity:toString() end

---Framework-owned base handle for a connected player, extended by each game or mod.
---@class Player: Entity
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

---Server-created replicated world-space text label.
---@class TextLabel: Entity
---@field text string Read-only. Current replicated label text.
---@field color integer Read-only. Current packed ARGB color.
---@field fontSize number Read-only. Current font size.
---@field drawDistance number Read-only. Current maximum rendering distance in world units.
---@field fadeDistance number Read-only. Distance in world units where fading starts.
---@field style integer Read-only. The preset nearest the current look: 0 none, 1 shadow, 2 outline, 3 box.
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

---Client-only, resource-owned CEF web-view API exposed as the global Web.
---@class Web
Web = {}

---Creates an origin-locked web view owned by the calling resource.
---@param url string Resource-relative URL or allowed absolute URL loaded into the view.
---@param options? { width?: number, height?: number, x?: number, y?: number, zIndex?: number, visible?: boolean, focus?: boolean } Optional initial pixel geometry, stacking, visibility, and focus settings.
---@return number # Numeric view ID used by the remaining Web methods.
function Web.createView(url, options) end

---Destroys an owned view and removes all of its script event handlers.
---@param viewId number Owned view identifier.
---@return boolean # True when a view was destroyed.
function Web.destroyView(viewId) end

---Makes an owned view visible.
---@param viewId number Owned view identifier.
---@return boolean # True when the view exists and was updated.
function Web.showView(viewId) end

---Hides an owned view.
---@param viewId number Owned view identifier.
---@return boolean # True when the view exists and was updated.
function Web.hideView(viewId) end

---Changes input focus for an owned view.
---@param viewId number Owned view identifier.
---@param focused? boolean Whether the view captures keyboard and mouse input; defaults to true.
---@return boolean # True when the view exists and focus was updated.
function Web.focusView(viewId, focused) end

---Checks whether an owned view is currently visible.
---@param viewId number Owned view identifier.
---@return boolean # False for missing or unowned views.
function Web.isViewVisible(viewId) end

---Keeps a hidden view painting so something else can sample it, such as a render target drawing the page onto world geometry. A view that is merely hidden stops painting and the sampled picture freezes.
---@param viewId number Owned view identifier.
---@param offscreen? boolean Whether the page keeps painting without being drawn on screen; defaults to true.
---@return boolean # True when the view exists and was updated.
function Web.setViewOffscreen(viewId, offscreen) end

---Checks whether an owned view keeps painting while hidden.
---@param viewId number Owned view identifier.
---@return boolean # False for missing or unowned views.
function Web.isViewOffscreen(viewId) end

---Navigates a view and replaces its allowed origin with the new URL's origin.
---@param viewId number Owned view identifier.
---@param url string New resource-relative or allowed absolute URL.
---@return boolean # True when navigation was requested.
function Web.loadURL(viewId, url) end

---Resizes an owned view's viewport.
---@param viewId number Owned view identifier.
---@param width number New viewport width in pixels.
---@param height number New viewport height in pixels.
---@return boolean # True when the view exists and was resized.
function Web.resizeView(viewId, width, height) end

---Moves an owned view on screen.
---@param viewId number Owned view identifier.
---@param x number New horizontal screen position in pixels.
---@param y number New vertical screen position in pixels.
---@return boolean # True when the view exists and was moved.
function Web.setViewPosition(viewId, x, y) end

---Registers a handler for an event emitted by an owned same-origin page.
---@param viewId number Owned view identifier.
---@param eventName string Page-to-script event name.
---@param handler fun(payload: any) Resource-owned callback invoked by the view's callEvent bridge.
function Web.on(viewId, eventName, handler) end

---Removes page-event handlers from an owned view.
---@param viewId number Owned view identifier.
---@param eventName string Page-to-script event name.
---@param handler? fun(payload: any) Optional exact callback; omitting it removes every matching handler owned by the resource.
---@return boolean # True when at least one handler was removed.
function Web.off(viewId, eventName, handler) end

---Dispatches a CustomEvent into an owned view.
---@param viewId number Owned view identifier.
---@param eventName string CustomEvent name dispatched in the page.
---@param payload? any Optional JSON-serializable event detail.
---@return boolean # True when the dispatch script was queued.
function Web.emit(viewId, eventName, payload) end

---Returns the current client viewport size.
---@return { width: integer, height: integer } # Width and height in physical pixels.
function Web.getScreenSize() end

---Client-only, resource-owned physical key bindings exposed as the global Key.
---@class Key
Key = {}

---Returns the current layout's label for a physical key, even while UI owns input. Query again when refreshing prompts. Throws for unknown keys or non-string arguments.
---@param key string Case-insensitive physical key name, using the same names as bind.
---@return string # Uppercase printable text, or an English name such as Enter or Mouse 1. Falls back to the uppercase canonical key name when translation is unavailable.
function Key.getLabel(key) end

---Binds a resource-owned handler that fires while the game has foreground input and no UI is capturing it. Each wheelup/wheeldown notch fires down, then up.
---@param key string Case-insensitive supported keyboard or mouse key name.
---@param stateOrHandler "down" | "up" | "both" | fun(key: string, state: string) Trigger state, or the handler itself to use the default down state.
---@param handler? fun(key: string, state: string) Handler required when an explicit trigger state is provided.
---@return boolean # True after the binding is installed; invalid keys or states throw.
function Key.bind(key, stateOrHandler, handler) end

---Removes matching bindings owned by the calling resource; omitting filters removes every binding for the key.
---@param key string Case-insensitive supported key name.
---@param state? "down" | "up" | "both" Optional trigger-state filter.
---@param handler? fun(key: string, state: string) Optional exact handler filter.
---@return boolean # True when at least one binding was removed.
function Key.unbind(key, state, handler) end

---Queries live physical key state using the same foreground and UI-input gate as binding dispatch.
---@param key string Case-insensitive supported key name.
---@return boolean # False when the key is up, the game is backgrounded, UI owns input, or the key name is invalid. Always false for the wheel, which is never held.
function Key.isDown(key) end

---Client chat transport and native chat-box controls.
---@class Chat
Chat = {}

---Sends a chat line to the server, bypassing the chatSend event; incoming lines arrive through the reserved chatMessage event.
---@param text string Player-authored chat text sent to the server.
function Chat.send(text) end

---Changes visibility of the native chat overlay without opening its input field. Hidden, the overlay also stops opening on its key. The choice holds for the rest of the session and resets to visible on disconnect, so a resource that replaces the chat calls this once at startup.
---@param visible boolean Whether the chat overlay is rendered.
function Chat.setUIVisible(visible) end

---Checks whether the native chat overlay is visible.
---@return boolean # True when the overlay is currently rendered.
function Chat.isUIVisible() end

---Opens and focuses the native chat input field.
function Chat.open() end

---Closes the native chat input field without submitting its contents.
function Chat.close() end

---Checks whether the native chat input field is active.
---@return boolean # True while chat is capturing keyboard input.
function Chat.isOpen() end

---Client-only Discord rich-presence composer exposed as the global Discord; setters stage values until update or setPresence publishes them.
---@class Discord
Discord = {}

---Stages the Discord activity verb or numeric enum value.
---@param type 'playing'|'streaming'|'listening'|'watching'|integer the Discord activity verb or numeric enum value.
function Discord.setType(type) end

---Stages the activity name.
---@param name string the activity name.
function Discord.setName(name) end

---Stages the top presence line.
---@param details string the top presence line.
function Discord.setDetails(details) end

---Stages the bottom presence line.
---@param state string the bottom presence line.
function Discord.setState(state) end

---Stages the elapsed-time Unix timestamp in seconds; zero clears it.
---@param seconds number the elapsed-time Unix timestamp in seconds; zero clears it.
function Discord.setStartTimestamp(seconds) end

---Stages the countdown Unix timestamp in seconds; zero clears it.
---@param seconds number the countdown Unix timestamp in seconds; zero clears it.
function Discord.setEndTimestamp(seconds) end

---Stages the supported-platform bitmask: Desktop 1, Android 2, and iOS 4.
---@param flags number the supported-platform bitmask: Desktop 1, Android 2, and iOS 4.
function Discord.setSupportedPlatforms(flags) end

---Stages the large image asset key.
---@param assetKey string the large image asset key.
function Discord.setLargeImage(assetKey) end

---Stages the large image tooltip.
---@param text string the large image tooltip.
function Discord.setLargeText(text) end

---Stages the small image asset key.
---@param assetKey string the small image asset key.
function Discord.setSmallImage(assetKey) end

---Stages the small image tooltip.
---@param text string the small image tooltip.
function Discord.setSmallText(text) end

---Stages multiple Discord image fields at once.
---@param assets { largeImage: string?, largeText: string?, smallImage: string?, smallText: string? } Asset keys and tooltips to merge into the staged activity.
function Discord.setAssets(assets) end

---Stages the party grouping identifier.
---@param id string the party grouping identifier.
function Discord.setPartyId(id) end

---Stages the party's current and maximum size.
---@param current number Current party member count.
---@param max number Maximum party capacity.
function Discord.setPartySize(current, max) end

---Stages the party join-privacy name or numeric enum value.
---@param privacy 'private'|'public'|integer the party join-privacy name or numeric enum value.
function Discord.setPartyPrivacy(privacy) end

---Stages multiple Discord party fields at once.
---@param party { id: string?, size: integer[]?, privacy: ('private'|'public'|integer)? } Party fields to merge into the staged activity.
function Discord.setParty(party) end

---Stages the opaque match secret.
---@param secret string the opaque match secret.
function Discord.setMatchSecret(secret) end

---Stages the opaque join secret that enables invitations.
---@param secret string the opaque join secret that enables invitations.
function Discord.setJoinSecret(secret) end

---Stages the opaque spectate secret.
---@param secret string the opaque spectate secret.
function Discord.setSpectateSecret(secret) end

---Stages multiple Discord activity secrets at once.
---@param secrets { match: string?, join: string?, spectate: string? } Opaque secrets to merge into the staged activity.
function Discord.setSecrets(secrets) end

---Stages whether the activity represents an instanced session.
---@param instance boolean whether the activity represents an instanced session.
function Discord.setInstance(instance) end

---Merges a complete option batch into the staged activity and publishes it immediately.
---@param options table<string, any> Batch of supported activity, timestamp, asset, party, secret, instance, and platform fields.
---@return boolean # True when the update was dispatched; false when Discord is unavailable.
function Discord.setPresence(options) end

---Publishes the currently staged activity as one rate-limited Discord update.
---@return boolean # True when dispatched; false when Discord is unavailable.
function Discord.update() end

---Clears what scripts published and resets staged state.
---@return boolean # True when dispatched; false when Discord is unavailable.
function Discord.clear() end

---Resets staged activity fields without publishing a Discord update.
function Discord.reset() end

---Returns the signed-in Discord user's snowflake.
---@return string # User ID string, or an empty string until available.
function Discord.getUserId() end

---Returns the signed-in Discord user's username.
---@return string # Username string, or an empty string until available.
function Discord.getUsername() end

---Checks whether Discord is connected and can publish presence.
---@return boolean # True when rich presence is initialized.
function Discord.isAvailable() end

---Local player's proximity voice chat settings: on/off, playback volume, hearing range and the push-to-talk binding.
---@class Voice
Voice = {}

---Turns voice chat on or off. Off closes the microphone and playback devices and tells the server to stop relaying voice to this client.
---@param enabled boolean Whether voice chat runs at all for this player.
function Voice.setEnabled(enabled) end

---Checks whether voice chat is enabled for this player.
---@return boolean # True unless the player turned voice chat off.
function Voice.isEnabled() end

---Sets the playback volume of incoming voice. A game that plays voice through its own audio engine applies the same gain there.
---@param volume number Playback gain, where 1 is unattenuated. Clamped to 0..4.
function Voice.setVolume(volume) end

---Reads the voice playback volume.
---@return number # Current gain, where 1 is unattenuated.
function Voice.getVolume() end

---Narrows how far this player hears others. Can only reduce the server's range, since a talker beyond it is never relayed.
---@param range number Audibility radius in world units; 0 removes the local limit.
function Voice.setHearingRange(range) end

---Reads the local hearing-range limit.
---@return number # Radius in world units, or 0 when the server's range applies unreduced.
function Voice.getHearingRange() end

---Reads the server's proximity range for talkers with no override of their own.
---@return number # Radius in world units.
function Voice.getRange() end

---Switches how far this player's voice carries. A request: the server paces it, decides the radius, and may move the player itself.
---@param tier number Voice tier: 0 whisper, 1 normal, 2 shout.
function Voice.setTier(tier) end

---Reads the voice tier this player is on.
---@return number # 0 whisper, 1 normal, 2 shout.
function Voice.getTier() end

---Rebinds push-to-talk. Unknown key names and the wheel, which is never held, throw.
---@param key string Case-insensitive key name, using the same names as Key.bind.
function Voice.setPushToTalkKey(key) end

---Reads the push-to-talk binding.
---@return string # Canonical key name, e.g. "v".
function Voice.getPushToTalkKey() end

---Sets how long transmission continues after push-to-talk is released, so letting go slightly early does not clip the end of a word. Muting, turning voice off and starting to type into a text field still stop it at once.
---@param milliseconds number How long to keep transmitting after the key goes up. Clamped to 0..2000.
function Voice.setPushToTalkReleaseDelay(milliseconds) end

---Reads the push-to-talk release delay.
---@return number # Delay in milliseconds; 0 when transmission stops the moment the key is released.
function Voice.getPushToTalkReleaseDelay() end

---Checks whether the local player is speaking right now. The same state raises the voiceStart and voiceStop events.
---@return boolean # True while push-to-talk is open -- held, or still inside the release delay -- and the microphone is producing audio.
function Voice.isTalking() end

---Checks whether a capture device opened for this session.
---@return boolean # False when the player has no working microphone, i.e. they are listen-only.
function Voice.hasMicrophone() end

---Chooses what opens the microphone. Switching closes whatever the previous mode had open. Unknown modes throw.
---@param mode 'pushToTalk' | 'voiceActivity' pushToTalk sends while the key is held; voiceActivity sends whenever the microphone is louder than the activation threshold.
function Voice.setTransmitMode(mode) end

---Reads what opens the microphone.
---@return 'pushToTalk' | 'voiceActivity' # The current transmit mode.
function Voice.getTransmitMode() end

---Sets the voice-activation sensitivity: lower sends quieter speech, higher ignores more background noise. Clamped to 0..1.
---@param level number Microphone level, 0 to 1 on the scale getInputLevel reports, above which voice activation sends.
function Voice.setActivationThreshold(level) end

---Reads the voice-activation threshold.
---@return number # Level from 0 to 1.
function Voice.getActivationThreshold() end

---Reads how loud the microphone is while this player is sending voice. The microphone stays open for the whole session, so what it hears the rest of the time is not given to scripts.
---@return number # Smoothed level from 0 to 1 while isTalking is true; 0 otherwise.
function Voice.getInputLevel() end

---Turns noise suppression on or off. It runs before the level is measured, so it also keeps steady noise from tripping voice activation.
---@param enabled boolean Whether to filter background noise out of the microphone.
function Voice.setNoiseSuppression(enabled) end

---Checks whether noise suppression is on.
---@return boolean # True while background noise is being filtered.
function Voice.isNoiseSuppressionEnabled() end

---Lists the microphones voice can record from.
---@return string[] # Device names as the player would recognise them; empty when the game chooses the device itself.
function Voice.getInputDevices() end

---Chooses the microphone. A running microphone is reopened on the new device; a device that is no longer connected falls back to the default.
---@param name string A name from getInputDevices, or an empty string for the system default.
function Voice.setInputDevice(name) end

---Reads the chosen microphone.
---@return string # Its name, or an empty string for the system default.
function Voice.getInputDevice() end

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
