# Synchronous event approval

Native callers can ask reserved-event handlers to approve an operation before
committing it. Use `Events::EmitReservedSync` with the explicit failure policy
when an exception or asynchronous answer must deny the operation:

```cpp
const bool accepted = events.EmitReservedSync(isolate, context, "craftingCompleting", args, Framework::Scripting::Builtins::Events::SynchronousFailurePolicy::Veto);
```

This is a C++ dispatch option, not a new JavaScript global. The integrating
mod registers its event and exposes the proposal argument. A resource handler
returns literal `false` to refuse:

```js
Events.on('craftingCompleting', (player, proposal) => {
    if (proposal.kind === 'alchemy' && proposal.recipe === 'restricted') {
        return false;
    }
});
```

Every registered global handler runs. Any literal `false` refuses the
operation. With `SynchronousFailurePolicy::Veto`, a thrown exception, missing
call result or returned Promise also refuses it; later handlers cannot undo
that refusal. No handlers, an omitted return or another non-false value allow
it. Client-event handlers registered through `onClient` are separate.

Handlers must finish synchronously. Returning a Promise does not delay the
operation, and refusing it does not cancel work the handler already scheduled.
Resolve external policy before this callback. Exceptions and rejected async
handler usage are logged.

The default `Continue` policy retains existing behavior: exceptions are
logged and Promises ignored. Existing callers retain their prior semantics.

This dispatcher does not roll back script side effects or validate the
caller's state. Pass a detached proposal, keep authoritative writes private,
revalidate identities and revisions after handlers return, and emit committed
notifications separately. KCDC's alchemy adapter implements that boundary.

Run `builds\build.bat RunFrameworkTests 64` to check both policies, explicit
veto, handler exceptions, async handlers, continued dispatch after refusal,
empty handler sets, once handlers and isolation from client events.
