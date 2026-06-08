<div align="center">
  <a href="https://github.com/MafiaHub/Framework">
    <img src="https://github.com/MafiaHub/Framework/assets/9026786/43e839f2-f207-47bf-aa59-72371e8403ba" alt="MafiaHub" />
  </a>

  <h1>MafiaNet</h1>

  <p><strong>A modern, cross-platform networking engine for multiplayer games</strong></p>

  <p>
    <a href="https://github.com/MafiaHub/MafiaNet/actions/workflows/build.yml"><img src="https://github.com/MafiaHub/MafiaNet/actions/workflows/build.yml/badge.svg" alt="Build & Test" /></a>
    <a href="https://discord.gg/eBQ4QHX"><img src="https://img.shields.io/discord/402098213114347520.svg?label=Discord&logo=discord&logoColor=white" alt="Discord" /></a>
    <a href="LICENSE.md"><img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License" /></a>
    <img src="https://img.shields.io/badge/C%2B%2B-17-blue.svg?logo=cplusplus" alt="C++17" />
    <img src="https://img.shields.io/badge/CMake-3.21+-064F8C.svg?logo=cmake" alt="CMake" />
  </p>

  <p>
    <a href="#quick-start">Quick Start</a> •
    <a href="#features">Features</a> •
    <a href="https://mafianet.mafiahub.dev">Documentation</a> •
    <a href="#community">Community</a>
  </p>
</div>

---

## About

MafiaNet is an actively maintained networking library built for game developers who need reliable, high-performance multiplayer networking. Built on the foundation of RakNet and SLikeNet, MafiaNet delivers battle-tested networking capabilities with modern C++ standards and security practices.

**Supported Platforms:** Windows, Linux, macOS (primary) | iOS, Android (limited)

## Quick Start

### Requirements

- CMake 3.21+
- C++17 compatible compiler
- OpenSSL 1.0.0+
- Internet connection (first build fetches dependencies automatically)

### Building

**Linux / macOS:**
```bash
git clone https://github.com/MafiaHub/MafiaNet.git
cd MafiaNet
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)  # Linux
cmake --build . -j$(sysctl -n hw.ncpu)  # macOS
```

**Windows (Visual Studio):**
```powershell
git clone https://github.com/MafiaHub/MafiaNet.git
cd MafiaNet

# Generate Visual Studio 2022 solution
cmake -G "Visual Studio 17 2022" -A x64 -B build

# Build from command line, or open build/MafiaNet.sln in Visual Studio
cmake --build build --config Release
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `MAFIANET_BUILD_SHARED` | ON | Build shared library (.dll/.so/.dylib) |
| `MAFIANET_BUILD_STATIC` | ON | Build static library (.lib/.a) |
| `MAFIANET_BUILD_SAMPLES` | OFF | Build 80 sample applications and extensions |
| `MAFIANET_BUILD_TESTS` | OFF | Build test suite |

To build with samples:
```bash
cmake -DMAFIANET_BUILD_SAMPLES=ON ..
```

### Basic Usage

```cpp
#include "mafianet/peerinterface.h"
#include "mafianet/MessageIdentifiers.h"

// Create a peer
MafiaNet::RakPeerInterface* peer = MafiaNet::RakPeerInterface::GetInstance();

// Start as server
MafiaNet::SocketDescriptor sd(60000, 0);
peer->Startup(32, &sd, 1);
peer->SetMaximumIncomingConnections(32);

// Or connect as client
peer->Connect("127.0.0.1", 60000, nullptr, 0);

// Process incoming packets
MafiaNet::Packet* packet;
while ((packet = peer->Receive()) != nullptr) {
    switch (packet->data[0]) {
        case ID_NEW_INCOMING_CONNECTION:
            printf("Client connected\n");
            break;
        case ID_CONNECTION_REQUEST_ACCEPTED:
            printf("Connected to server\n");
            break;
    }
    peer->DeallocatePacket(packet);
}

// Cleanup
MafiaNet::RakPeerInterface::DestroyInstance(peer);
```

## Features

<table>
<tr>
<td width="50%" valign="top">

### Core Networking
- Reliable UDP with automatic retransmission
- Packet ordering and sequencing
- Automatic packet splitting for large messages
- IPv4 and IPv6 dual-stack support
- SSL/TLS encryption via OpenSSL
- Connection statistics and monitoring

</td>
<td width="50%" valign="top">

### Multiplayer Systems
- Peer-to-peer and client-server architectures
- NAT punchthrough and traversal
- Host migration
- Room and lobby management
- Object replication (ReplicaManager3)
- Remote procedure calls (RPC4)

</td>
</tr>
<tr>
<td width="50%" valign="top">

### Development Tools
- Packet logger with multiple outputs
- Network simulator (latency, packet loss)
- Bandwidth monitoring and limiting
- Message filtering
- Comprehensive debug statistics

</td>
<td width="50%" valign="top">

### Extensions
- **RakVoice** - Voice chat with Opus codec and RNNoise
- **Autopatcher** - Delta patching for game updates
- **FileListTransfer** - Reliable file transfer
- **Database** - MySQL, PostgreSQL, SQLite integration
- **UPnP** - Automatic port forwarding

</td>
</tr>
</table>

## Plugins

MafiaNet provides a modular plugin system for extending functionality:

| Plugin | Description |
|--------|-------------|
| **ReplicaManager3** | Automatic object replication and state synchronization |
| **RPC4** | Remote procedure calls with parameter serialization |
| **FullyConnectedMesh2** | P2P mesh networking with host migration |
| **NatPunchthrough** | NAT traversal for peer-to-peer connections |
| **FileListTransfer** | Reliable file transfer with progress callbacks |
| **DirectoryDeltaTransfer** | Incremental directory synchronization |
| **Autopatcher** | Delta patching system for game updates |
| **RakVoice** | Voice chat with Opus codec and noise suppression |
| **ReadyEvent** | Player ready state synchronization |
| **TeamManager** | Team assignment and balancing |
| **Router2** | Message routing through intermediate peers |
| **MessageFilter** | Security filtering for incoming messages |
| **PacketLogger** | Network traffic logging and debugging |
| **TwoWayAuthentication** | Mutual authentication between peers |
| **CloudComputing** | Distributed data storage and retrieval |
| **Lobby2** | Matchmaking and lobby system |

See the [Plugin Documentation](https://mafianet.mafiahub.dev/plugins/overview.html) for detailed usage.

## Documentation

Full documentation is available at **[mafianet.mafiahub.dev](https://mafianet.mafiahub.dev)**

Documentation includes:
- [Installation Guide](https://mafianet.mafiahub.dev/getting-started/installation.html)
- [Quick Start Tutorial](https://mafianet.mafiahub.dev/getting-started/quickstart.html)
- [Architecture Guide](https://mafianet.mafiahub.dev/guide/concepts.html)
- [Plugin Reference](https://mafianet.mafiahub.dev/plugins/overview.html)
- [API Reference](https://mafianet.mafiahub.dev/api/core.html)
- [FAQ](https://mafianet.mafiahub.dev/support/faq.html)

### Building Documentation Locally

```bash
# Install dependencies
brew install doxygen        # macOS
sudo apt install doxygen    # Ubuntu/Debian
pip install -r docs/requirements.txt

# Build and serve
cd docs
./build.sh serve            # http://localhost:8000
```

## Project Structure

```
MafiaNet/
├── Source/
│   ├── include/mafianet/   # Public API headers (include as "mafianet/...")
│   └── src/                # Implementation files
├── Samples/                # 80 example applications
│   ├── ChatExample/        # Simple chat application
│   ├── Ping/               # Basic UDP communication
│   ├── ReplicaManager3/    # Object replication demo
│   ├── NATCompleteClient/  # NAT traversal client
│   ├── NATCompleteServer/  # NAT traversal server
│   ├── RakVoice/           # Voice chat examples
│   ├── FileListTransfer/   # File transfer demo
│   └── Tests/              # Comprehensive test suite
├── DependentExtensions/    # Optional integrations
│   ├── Autopatcher/        # Delta patching system
│   ├── Lobby2/             # Matchmaking and lobbies
│   ├── RakVoice.cpp/h      # Voice communication
│   ├── MySQLInterface/     # MySQL connectivity
│   ├── PostgreSQLInterface/# PostgreSQL connectivity
│   ├── SQLite3Plugin/      # SQLite integration
│   └── RPC3/               # Legacy RPC system
├── docs/                   # Sphinx documentation source
└── cmake/                  # CMake modules and helpers
```

## Dependencies

MafiaNet automatically fetches required dependencies via CMake FetchContent:

| Dependency | Version | Used For |
|------------|---------|----------|
| OpenSSL | 1.0.0+ | Encryption (required, system-installed) |
| bzip2 | - | Compression (Autopatcher) |
| miniupnpc | - | UPnP port forwarding |
| Opus | 1.5.2 | Voice codec (RakVoice) |
| RNNoise | - | Noise suppression (RakVoice) |

## Running Tests

Build with samples enabled, then run the test suite:

```bash
cmake -DMAFIANET_BUILD_SAMPLES=ON ..
cmake --build .
./Samples/Tests/Tests

# Run a specific test
./Samples/Tests/Tests EightPeerTest
```

Available tests include: `EightPeerTest`, `MaximumConnectTest`, `PeerConnectDisconnectTest`, `ManyClientsOneServerBlockingTest`, `ReliableOrderedConvertedTest`, `SecurityFunctionsTest`, `SystemAddressAndGuidTest`, and more.

## Changelog

### Version 0.9.0 (Latest)
- **Strong-typed `PeerGuid`**: a new `enum class PeerGuid : uint64_t` names a peer's `RakNetGUID` value distinctly from `NetworkID` (an object id), so the two can no longer be passed interchangeably in a `uint64_t`-typed signature — removing a class of silent "passed the wrong id" bugs in ReplicaManager3 glue and `void(uint64_t)` callbacks. Convert with `ToPeerGuid()` / `ToGuid()` and compare against `UNASSIGNED_PEER_GUID`. As a trivially-copyable 8-byte scoped enum it serializes byte-identically through `BitStream` (and therefore `VariableDeltaSerializer`) to the raw `uint64_t` it replaces — fully wire-compatible, no netcode bump. Purely additive, no behavioural change

### Version 0.8.0
- **Optional disconnect reason**: `CloseConnection` gains a final optional `const BitStream *reasonData` argument whose bytes are appended right after the `ID_DISCONNECTION_NOTIFICATION` message ID, so the remote peer can learn *why* it was dropped (e.g. a kick/ban enum plus a custom string). The receiver reads it like any other body — `packet->data + 1` for `packet->length - 1` bytes. Only graceful disconnects carry a reason; locally-synthesized notifications (`ID_CONNECTION_LOST`, timeout/dead-connection) stay payload-less, so always tolerate a zero-length body. Wire-backward-compatible: peers that only inspect `data[0]` are unaffected
- **Bug fix**: `RakPeer::CloseConnection` no longer coerces an unresolved target index (`-1`) to `0` and reads `remoteSystemList[0]` — which targeted an unrelated peer's slot or crashed on an unallocated list; the close socket is now resolved without assuming a valid slot index

### Version 0.7.0
- **Virtual worlds (dimensions)**: new per-entity / per-observer `VirtualWorldId` scoping on top of ReplicaManager3 — the SA-MP `SetPlayerVirtualWorld` / routing-bucket model for instanced interiors (e.g. apartments). Players only see entities sharing their virtual world (or `VIRTUAL_WORLD_GLOBAL`), switchable at runtime with no reconnect. Derive entities from `VirtualWorldReplica3`; `Connection_RM3` gets `Get/SetVirtualWorld`; `ReplicaManager3` gets `GetConnectionsInVirtualWorld`/`GetGuidsInVirtualWorld` and `SetPlayerVirtualWorld`. The filter is authority-only, so a downloaded copy never despawns the entity at its owner. See `Samples/VirtualWorld`

### Version 0.6.1
- **ReplicaManager3**: `GetReplicaAtIndex` is now `const`, matching the other read accessors (`GetReplicaCount`, `GetConnectionCount`, `GetConnectionAtIndex`) — const methods iterating replicas no longer need a `const_cast`. The returned `Replica3*` stays non-const. Source-compatible (no break for existing non-const call sites)

### Version 0.6.0
- **RPC4 user context**: `RegisterFunction`, `RegisterSlot`, `RegisterBlockingFunction` and the `RPC4GlobalRegistration` handler constructors now take an opaque `void *context` passed back to the handler on every call — no more file-static global pointers to route an RPC to an object instance (each registration carries its own context)
- **Bug fix**: `RakPeer::CloseConnection` no longer dereferences a null `rakNetSocket` during teardown (a pre-existing crash in release builds); falls back to the primary socket
- **Testing**: added `RPC4ContextTest` (slot/nonblocking/blocking context); quarantined the flaky `ManyClientsOneServerDeallocateBlockingTest` in CI pending a teardown-race fix
- _Breaking_: RPC4 handler signatures gained a trailing `void *context` and the registration calls take a context argument; there are no compatibility overloads (pass `nullptr` when unused)

### Version 0.5.1
- **Plugins**: Added `DirectoryDeltaTransfer::AddFile(filePath, fileName)` to queue a single file for upload (complements the recursive `AddUploadsFromSubdirectory`)

### Version 0.5.0
- **Namespace cleanup**: Standardized on the `MafiaNet` namespace; removed the legacy `SLNet` macro in favor of an `MNet` shorthand alias, and dropped stale `RakNet` alias references
- **Single header set**: Collapsed the legacy redirect-stub header layers into the canonical `mafianet/...` includes
- **BitStream safety**: Catch-all serialization now `static_assert`s on trivially-copyable types (preventing `std::string` double-frees), with added length-prefixed `std::string` specializations
- _Breaking_: legacy include spellings and the `SLNet` namespace macro are removed; serializing non-trivially-copyable types via the generic `BitStream::Write`/`Read` is now a compile error

### Version 0.4.0
- **Cross-platform**: Full macOS and Linux support, merged `Socket2` definitions, removed deprecated platform back-ends
- Fixed IPv6 connectivity and initialization issues
- Upgraded dependencies: OpenSSL 3.6.0, miniupnpc 2.3.3, Opus 1.6.1; dependencies now fetched on demand via CMake
- Fixed undefined behaviour in congestion control and a null-socket crash in connection teardown
- CI now runs the full test suite on Linux, macOS and Windows, with numerous test-stability fixes

### Version 0.3.0
- **RakVoice**: Migrated from Speex to Opus codec with RNNoise noise suppression
- Added support for 8000, 16000, 24000, 48000 Hz sample rates
- Voice activity detection using Opus DTX

### Version 0.2.0
- Rebranded from SLikeNet to MafiaNet
- Updated to C++17 standard
- Modernized CMake build system
- Added Sphinx documentation with Breathe integration
- Removed pre-generated Visual Studio solution files

See [CHANGELOG](https://mafianet.mafiahub.dev/changelog.html) for full history.

## Background

MafiaNet continues the legacy of two foundational networking libraries:

- **RakNet** (2001-2014) - Industry-standard game networking library by Jenkins Software, used in countless multiplayer games. Acquired and open-sourced by Oculus VR.
- **SLikeNet** (2016-2019) - Community continuation by SLikeSoft that modernized RakNet with bug fixes, security patches, and C++11 support.

With SLikeNet no longer maintained, MafiaNet carries the torch forward—providing an actively developed, modern networking solution for the game development community.

## Community

- **Discord**: [Join our server](https://discord.gg/eBQ4QHX) for support and discussion
- **Documentation**: [mafianet.mafiahub.dev](https://mafianet.mafiahub.dev)
- **Issues**: [Report bugs](https://github.com/MafiaHub/MafiaNet/issues) or request features
- **Contributions**: Pull requests are welcome!

## License

MafiaNet is released under the [MIT License](LICENSE.md).

---

<div align="center">
  <sub>Built with passion by the <a href="https://github.com/MafiaHub">MafiaHub</a> community</sub>
</div>
