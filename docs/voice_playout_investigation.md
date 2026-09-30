# Voice pops and clicks: buffer investigation

The Framework's local sink and KCD2's FMOD sink both use
`Framework::Voice::PlayoutBuffer`. Source inspection found reproducible
discontinuities in its recovery paths. These are plausible causes of the
reported pops; no game, microphone, or playback device was launched to establish
which event occurs during the user's sessions.

## Confirmed defects

- **Underruns cut directly to silence.** `Pull` refused an entire callback when
  only part of it was buffered. Both sinks supplied silence, leaving the short
  remainder queued and potentially stranded until re-priming or release.
- **Latency trimming joined unrelated sample positions.** Once depth exceeded
  the ceiling, old PCM was discarded and the next sample played at full level.
  Its phase and amplitude need not match the previous output sample.
- **Full-ring drops were invisible to playback.** `Push` ignored the ring's
  failure result. After the consumer resumed, retained PCM could run directly
  into new PCM across the dropped interval.
- **Large requests could stay silent indefinitely.** A callback larger than
  the ring could never satisfy the all-or-nothing pop, regardless of how much
  useful PCM had arrived.
- **Recovery copied repeatedly on the callback thread.** Trimming and release
  kept polling and copying until the queue fell far enough. Work was affected
  by concurrent arrivals rather than bounded to the initial queue snapshot.

`SpscRing` checks capacity before writing and keeps producer and consumer indices
separate. These playback defects are audio loss and discontinuity, rather than
evidence of an out-of-bounds memory write in that ring.

## Fix

The consumer now renders available PCM even when it cannot fill a complete
request. It writes the rest explicitly, fades to silence on underrun, and
re-primes before resuming. Starts and latency skips blend from the last emitted
sample over 240 samples (5 ms at 48 kHz). Fade state spans callbacks; continuous
PCM is unchanged after the transition.

A rejected producer write signals an overrun atomically. The consumer discards
the stale queue, fades out, and re-primes on fresh PCM. The producer never moves
the read index. Consumer-only skipping discards a queue snapshot without copying
or chasing a concurrently refilling producer.

KCD2's `Voice::Fill` uses the shared buffer's complete output, including its fade
tail. Its FMOD stream configuration and voice wire format are unchanged. FMOD's
[manual PCM stream documentation](https://www.fmod.com/docs/2.03/api/loading-and-playing-sounds-in-the-core-api.html#creating-a-sound-by-manually-providing-sample-data)
describes the callback that supplies this audio.

## Validation and limits

`RunFrameworkVoiceTests` exercises the ring, activation gate, jitter estimator,
mixer and recovery paths without opening devices or connecting to a server.
Regression cases include a short remainder, 60-sample callbacks, a 20,000-sample
request, opposite-polarity trimming, ring saturation, and speaker-slot reuse.
All 48 focused tests pass with the fix. Running them against the original
playout algorithm (with a discard-signature adapter) fails eight cases, including
all seven new recovery regressions.

For synthetic constant PCM, a 24,000-to-zero underrun is spread into steps of at
most 100. A +30,000-to-−30,000 trim is spread into steps of at most 250 instead
of a single 60,000-sample jump. These measurements verify the waveform joins;
they do not establish a live listening result.

Capture, denoising, codec transport, admission and both sinks were inspected.
KCD2 already counts capture overruns when the game thread falls behind its
conversion ring. Both capture pumping and speaker submission run on the game
thread, so frame stalls can still delay voice delivery. Recovery can soften a
gap but cannot recreate speech lost during a sufficiently long stall. Neither
live timing nor the game's FMOD stream-thread scheduling was measured here.
