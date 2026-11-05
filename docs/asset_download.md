# Client asset download

How a client fetches a server's resource packages (`.fwpak`) and asset paks,
and what the framework does so that a slow link, a busy server or a changed
pak never costs the player the join. The code is
`integrations/client/asset_downloader.*`,
`integrations/server/asset_file_server.*`,
`utils/streamed_assets/transfer.*` and the download half of
`integrations/client/instance.cpp`.

## Flow

1. Admission and the build challenge pass; the server sends `ServerResources`
   with every resource's package hash and size, its asset paks (lane, SHA-256,
   size, deferred flag), a per-connection download token and the mirror URLs
   (`download-url`).
2. The client names each file in its cache for this server and checks what is
   already there: a file whose verification note still matches its size and
   write time is trusted without a hash; anything else is hashed, and what
   does not match is fetched.
3. Each missing file is asked of the mirrors first, then of
   `http://<host>:<port>/files/<name>` on the game port over TCP with the
   token in `X-Download-Token`. A source that serves the file's chunk list
   gets the file assembled from it (below); one that does not is read whole,
   resumable from the bytes already received. The file is verified with
   SHA-256 before it is renamed into place, and its note is written.
4. Once every blocking file is present the mod is told
   (`OnInitialAssetDownloadReady`) and resources start. Paks announced as
   deferred follow in a transfer of their own and are handed to the mod
   through `OnResourceAssetPaksChanged` as they land. A resource restart
   repeats the same transfer for the resources it changed, deferred paks
   included.

## Chunks: a one-file edit costs that file

The server writes `<name>.chunks` beside every file it publishes, one
`<length> <sha256>` line per chunk in file order, reused across restarts while
the file is unchanged. A pak is cut at its entries and at its central
directory, a long entry every 4 MiB (`Utils::StreamedAssets::kChunkSize`), so
an edit that grows or shrinks one entry moves the entries after it without
changing their chunks; a file that is not a pak (a `.fwpak`) is cut every
4 MiB. The client fetches the list, then assembles the file in a partial of
its full size:

- chunks an older pak on disk already holds are copied from it, found by
  hash wherever they lie in it: earlier versions of the same lane pak
  (`<resource>.<lane>.<otherhash>.pak`) in this server's cache, and the same
  pak in another server's cache (its name carries its hash, so the bytes are
  the same). The older paks are hashed once per file, whatever source serves
  it. A `.fwpak` has nothing to reuse: its nonce is derived from its content,
  so any change rewrites every byte of it;
- the rest are fetched by `Range`, neighbouring chunks in one request of up
  to 4 MiB, `RetryPolicy::connections` (4) requests at a time;
- a note beside the partial records which chunks are in place, by offset,
  length and hash, so a transfer cut at any point resumes with every whole
  chunk it had, in this session or a later one. A cut costs the chunk it fell
  in, not the request.

The whole file is still verified against the announced SHA-256 before it is
used. If that fails, each chunk in place is checked against the list and the
ones that are not as written (a crash between the data and the note) come
again; if every chunk is as the list says, the list is not the file's and
the file is read whole. A source that answers a range with the whole file is
taken for one without range support, and the file is read whole from it
instead.

Paks the server no longer announces are pruned only once the transfer is
through, since they are where the unchanged chunks come from.

## Why a join is never refused for load

The server has a budget of `maxplayers + max-downloading - 4` transfers, so
the webserver's other routes keep workers, and cuts it into slots of
`kMaxTransfersPerClient` (4) connections, one slot per peer: a peer that holds
a slot is never short of its connections, whatever the other peers do. A peer
past the slots gets `503`, `Retry-After: 2` and `X-Queue-Position: n`, where
`n` is how many peers asked before it and are still asking. Slots go to the
front of that line, so a join burst after a restart drains in the order
players arrived instead of racing, and a peer that stops asking leaves the
line after a few `Retry-After` periods. A peer that holds a slot keeps it for
the same grace between two files of its set, as spare capacity the line may
take. Chunk lists are answered outside the slots.

On the client a `503` is a wait, not a failed attempt: the downloader asks
again after the pause the server names, for as long as
`InstanceOptions::downloadQueueWaitMs` allows (0, the default, waits as long
as the server keeps answering). `AssetDownloadStatus::waitingForSlot` and
`queuePosition` carry the state to the mod's loading card.

## Why a flaky link still finishes

`RetryPolicy::attempts` counts requests that moved **no** bytes. A transfer
that was moving and got cut resumes with a fresh budget; the cut is counted in
`AssetDownloadStatus::cuts` and logged. The server's write timeout is raised
from httplib's 5 s to 120 s (`kAssetWriteTimeout` in the server instance), so
a receiver that stalls for a while is not cut by the sender in the first
place.

A refusal (`403`, `404`) is still not retried: it will not change by asking
again.

## When a download is given up

- The downloader's own budget runs out: `attempts` requests in a row without
  a byte, with backoff from `firstDelayMs` doubling to `maxDelayMs`, or the
  queue wait is exceeded.
- The client instance sees nothing observable for
  `InstanceOptions::downloadStallTimeoutMs` (60 s by default): no request
  started, no answer, no byte, no pause begun, no file being hashed. Waiting
  in a queue is activity. A mod that kept its own byte-based stall watch
  should drop it: a queued client moves no bytes and is not stalled.

Either way `OnAssetsDownloadFinished(false)` fires with
`AssetDownloadStatus::failure` set to a sentence for the player, then the
connection closes. A deferred transfer that fails, or stalls for the same
`downloadStallTimeoutMs`, does not: the session runs without the paks that
did not land, `GetDeferredDownloadStatus().failure` says so, and a later
refresh fetches them again.

## Deferred lanes

A mod's `Utils::StreamedAssets::Policy::LaneMayDefer(lane)` names the lanes
whose paks may arrive after the join: sound banks, clips a script asks for.
The server flags those paks `deferred`; the client's first connect holds them
back, enters the world once the blocking paks are in, and fetches them
behind the join. Anything the level load must see stays blocking. The
default is blocking for every lane, so a mod that does nothing keeps today's
behaviour.

## Downloading beside the player slots

`max-downloading` (config and command line; `InstanceOptions::maxDownloading`)
admits that many connections beyond `maxplayers` while they download and
start resources. A player takes a `maxplayers` slot at `ClientJoin`, when it
enters the world; one that finds the world full by then is turned away with
the server-full reason. 0, the default, keeps every admitted connection on a
player slot, as before.

## Mirrors

`download-url` names one or more HTTP(S) origins, separated by spaces or
commas, tried in order before the game port, with no token. The server logs
at start which files to copy there from its staging directory; copy the
`.chunks` lists with them. A mirror that fails, serves wrong bytes or cannot
serve ranges falls back per file.

## What the log says

Each file logs its size, elapsed time, rate, how many bytes came from older
files and how many cuts it resumed through; the whole transfer logs what was
fetched, reused and already cached, elapsed time and rate. A user report of a
slow join is triaged from those lines: a low rate on every file is the
uplink, cuts are the link, queue waits are the server's slot count.
