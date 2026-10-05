# Runtime versions

Server and client scripts can read the local Framework and multiplayer mod
versions from `ExecutionEnvironment`:

```js
console.log(`Framework: ${ExecutionEnvironment.frameworkVersion}`);
console.log(`Multiplayer mod: ${ExecutionEnvironment.modVersion}`);
```

Both properties are read-only strings:

| Property | Value |
| --- | --- |
| `frameworkVersion` | The Framework release version compiled into this process. |
| `modVersion` | The multiplayer mod version supplied by the application in `InstanceOptions.modVersion`. Empty if none was supplied. |

On the client, these describe the installed client. On the server, they
describe the running server. They do not query the other peer or report the
underlying game's version. Version strings are returned unchanged, including
any prerelease suffix; they are not compatibility checks.

Applications using the Framework client or server `Instance` pass their mod
version to scripting automatically. An application that initializes a scripting
module directly can supply it as the second argument to `Init(sdkCallback,
modVersion)`.

The binding also records these properties in the Framework scripting metadata
for both sides. Mods that publish generated declarations should regenerate
their scripting contract after adopting this Framework revision.
