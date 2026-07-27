# Incident log

Real, hardware-reproduced bugs in this project's patches, and how they were
found and fixed. Kept so the same bug class doesn't get reintroduced -- see
[`docs/firmware-patch-guidelines.md`](firmware-patch-guidelines.md) for the
standing rule this log motivated.

---

## Incident 1 — `987639c`: `get ota.wifi`'s config struct on the stack

**Symptom:** Heltec V4 boot loop -- repeated USB CDC disconnects/re-enumerations,
the device never settling into a stable state.

**Cause:** Adding `get ota.wifi` introduced a 289-byte `HotspotOtaConfig` struct
as a stack-local variable inside `CommonCLI::handleGetCmd()` -- a single, large,
many-branch function (MeshCore's CLI dispatch is one big function, not split
per-command). That extra 289 bytes pushed the function's already-large combined
stack frame over the task's stack limit.

**Diagnosis:** Bisected by placing an identically-sized no-op stub struct in the
same spot (fine -- ruled out the struct's size/contents), then testing the same
struct as a stack-local inside a much shorter function, `HotspotOTA::wifiConnect()`
(also fine) -- isolating the cause to `handleGetCmd()`'s frame size specifically,
not the struct itself.

**Fix:** Made the config variable `static` instead of an automatic stack local --
moves it to BSS (fixed memory), off the stack entirely. No behavior change:
MeshCore's CLI is single-threaded and processes one command at a time, so
there's no reentrancy risk from a static there.

---

## Incident 2 — `26aeb56` / `a377067`: `get ota.active`'s version write in `setup()`

**Symptom:** Same signature as Incident 1 -- Heltec V4 didn't boot and exposed
no USB serial console at all, after flashing firmware built from `26aeb56`.
`git log b3f57ba..HEAD -- patches/` confirmed `26aeb56` was the only commit
between the last known-good build (`b3f57ba`) and the broken one that touched
`patches/`, isolating it as the cause.

**Cause:** `get ota.active`'s new version-reporting feature called
`SPIFFS.open()`/`write()`/`close()` synchronously inside
`RollbackGuard::begin()`, which runs from deep inside `setup()`'s own already-
heavy initialization call chain (after `radio_init()`, itself already several
frames deep). No single variable added here was anywhere near Incident 1's
289 bytes -- but the *added call depth* from the SPIFFS/File library's own
internal frames, stacked on top of `setup()`'s existing depth, reproduced the
same class of stack-driven instability by a different mechanism.

**Diagnosis:** `git log b3f57ba..HEAD -- patches/` isolated the change to a
single commit. Static review of that commit's diff found no oversized local
variable (ruling out a literal repeat of Incident 1), pointing at *call depth*
rather than *frame size* -- distinct mechanism, same underlying bug class.

**Fix:** Deferred the actual SPIFFS write from `begin()` (`setup()`-time, deep
call chain) to the first `poll()` call (`loop()`-time, shallow call chain).
`begin()` now only copies the version string into a small static buffer;
`poll()` performs the actual write once, on its first call after `begin()`.
Also made two new stack-local buffers in `status()` `static`, defensively,
even though `status()` isn't on the boot path.

Verified fixed on real hardware: firmware built from `a377067` booted and
exposed its serial console normally, matching `b3f57ba`'s working behavior.
