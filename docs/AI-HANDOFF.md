# AI handoff: local dev environment, workflows, and the mobmesh org move

This document is for whichever AI session picks up development on this repo
next — especially if that's after this repo has moved (or is moving) from
`HeyVern/meshcore-hotspot-ota` to a `mobmesh`-owned repo. It covers three
things: how to actually build/test changes from a sandboxed dev environment
like this one, what needs editing to point at the new org, and how to get the
files there in the first place.

## 1. Local sandbox environment: how builds/tests actually get done here

This project has no upstream source of its own to compile directly — it's a
**patch-based** project. `patches/*.patch` are unified diffs applied, in
numeric order, on top of a pristine clone of `meshcore-dev/MeshCore` at a
tracked release tag. There is no forked/vendored copy of MeshCore in this
repo; the real source only exists transiently, in CI or in a scratch
directory, for the duration of a build.

### Why you can't just `pio run` locally

This sandbox's outbound network goes through a policy-filtered proxy
(`$HTTPS_PROXY`), and `api.registry.platformio.org` (PlatformIO's package
registry, needed to install the ESP32 toolchain) is blocked — attempts return
a `403` "policy denial" from the proxy. There is no local ESP32 toolchain
cache pre-installed either. **You cannot compile firmware directly in this
sandbox.** Don't waste time retrying `pio run` with different flags — it's a
network policy block, not a transient failure.

Two things work locally without that limitation:
- `git`, patch generation/application, and text editing — all fine.
- `qemu-system-xtensa` is installed at `/usr/bin/qemu-system-xtensa`, but
  it's **stock upstream QEMU** and does not support the `esp32s3` machine
  type (`-machine help` won't list it). You need a different, prebuilt QEMU
  binary for boot-testing — see §1.3.

### 1.1 The patch-editing method (used for every code change this project makes)

This is the pattern used for every single patch edit in this project's
history. Do not hand-edit `patches/*.patch` directly — regenerate them from a
real before/after tree diff, every time:

```bash
SCRATCH=<your scratchpad dir>/some-work-name
mkdir -p "$SCRATCH"

# 1. Pristine upstream clone at the tracked tag (check variants/*/platformio.ini
#    or the last build-release.yml run for which tag is currently tracked)
git clone --depth 50 https://github.com/meshcore-dev/MeshCore.git "$SCRATCH/pristine"
cd "$SCRATCH/pristine" && git checkout <tag> --quiet

# 2. Build the "before" tree: pristine + all existing patches, unmodified
cp -r "$SCRATCH/pristine" "$SCRATCH/mid1"
cd "$SCRATCH/mid1"
git apply --check /path/to/repo/patches/0001-*.patch && git apply /path/to/repo/patches/0001-*.patch
# repeat for every patch that comes before the one you're editing, in order

# 3. Make your edits directly in $SCRATCH/mid1 (Edit tool, same as any file)

# 4. Regenerate the diff against pristine
cd "$SCRATCH"
git diff --no-index --src-prefix=a/ --dst-prefix=b/ pristine mid1 > new_000N.patch
sed -i -E 's#(a|b)/(pristine|mid1)/#\1/#g' new_000N.patch   # strip scratch path prefixes

# 5. Verify: fresh clone, apply ALL patches in order (old ones + your new one),
#    confirm no conflicts and (if other patches touch the same files) that the
#    result is byte-identical to what you intended
rm -rf "$SCRATCH/apply_test" && cp -r "$SCRATCH/pristine" "$SCRATCH/apply_test"
cd "$SCRATCH/apply_test"
git apply --check new_0001.patch && git apply new_0001.patch
git apply --check /path/to/repo/patches/0002-*.patch && git apply /path/to/repo/patches/0002-*.patch
# ...
diff -rq "$SCRATCH/apply_test" "$SCRATCH/mid1"   # (or whichever tree is ground truth)

# 6. Only once verified, copy into the real repo and commit
cp new_0001.patch /path/to/repo/patches/0001-*.patch
```

**If a later patch touches the same file(s) as the one you're editing**, you
must rebuild every subsequent patch the same way (pristine + patch 1 + patch
2's *original* content re-applied by hand, diffed against the patch-1-only
tree) rather than just regenerating patch 1 in isolation — otherwise line
offsets won't line up when the patches are applied in sequence. This came up
repeatedly in this project's history (e.g. `CommonCLI.cpp` and
`docs/cli_commands.md` are touched by every patch). When in doubt, do a full
stacked re-apply verification (step 5 above) against *every* patch, not just
the one you changed, and byte-diff the result against your intended final
state.

### 1.2 Compile-verifying a change (since you can't build locally)

Since local `pio run` is blocked, compile checks go through a **temporary,
disposable GitHub Actions workflow**, scoped to your working branch only:

1. Add a workflow file (e.g. `.github/workflows/<branch>-scratch-build.yml`)
   with `on: push: branches: [<your-branch-name>]` (not `workflow_dispatch` —
   see the note below on why) that: checks out the repo, resolves the
   upstream tag the same way `build-release.yml` does, clones upstream,
   applies `patches/*.patch` in order (`git apply --check` first, fail loudly
   on drift), installs PlatformIO (`pip install --upgrade platformio` — CI
   runners have full network access, unlike this sandbox), and runs
   `pio run -e <build_env>` for each variant you need to check.
2. Commit and push it to your branch. The push itself triggers the run.
3. Poll for the result (`mcp__github__actions_list` /
   `mcp__github__actions_get`), or use `ScheduleWakeup` to check back in a
   few minutes rather than blocking.
4. **Delete the workflow file once you're done** — commit its removal. These
   are scratch tooling, not permanent CI. The only two permanent workflows
   this repo should ever have on `main` are `build-release.yml` and
   `qemu-boot-check.yml` (see §3).

**Why `on: push` and not `workflow_dispatch`:** GitHub only lets you
dispatch a workflow manually if that workflow file already exists on the
repo's *default* branch. A brand-new scratch workflow that only exists on
your feature branch can't be `workflow_dispatch`'d — but `on: push` scoped to
that branch fires immediately on the push that adds the file, no special
permission needed. This is why every scratch workflow in this project's
history uses `on: push: branches: [<branch>]`, never `workflow_dispatch`.

### 1.3 Getting a real firmware.bin back into the sandbox

GitHub Actions build artifacts (`actions/upload-artifact`) are **not
reachable from this sandbox** — there's no API access path to download them
from here. When you need the actual compiled binary (for local QEMU boot
testing, or to hand to the user for real-hardware testing), the pattern is:
have the CI workflow push it to a disposable **orphan branch** instead:

```yaml
      - name: Push firmware.bin to a scratch branch
        run: |
          cp upstream-src/.pio/build/<env>/firmware.bin firmware.bin
          git config user.name "github-actions[bot]"
          git config user.email "github-actions[bot]@users.noreply.github.com"
          git checkout --orphan <branch>-output
          git reset --hard
          git add -f firmware.bin
          git commit -m "output ($(date -u +%FT%TZ))"
          git push -f origin <branch>-output
```

Then, back in the sandbox: `git fetch origin <branch>-output --quiet && git
show origin/<branch>-output:firmware.bin > local/path/firmware.bin`.

Two build flavors matter, don't mix them up:
- **Normal build** (`pio run -e <env>`, no extra flags): what real hardware
  needs. Native USB-CDC console.
- **QEMU-only build** (`PLATFORMIO_BUILD_FLAGS="-DARDUINO_USB_CDC_ON_BOOT=0"
  pio run -e heltec_v4_repeater`): forces the console onto UART0, because
  QEMU's ESP32-S3 fork doesn't emulate the native USB-Serial-JTAG device real
  hardware uses. **This flag must never be set on a binary intended for real
  hardware** — flashing a QEMU-flavored build onto a real board gets you a
  working device with no usable serial console (garbled/missing output),
  which was mistaken for a crash once already in this project's history.
  Only use it for the QEMU boot-loop check.

Clean up: remove the temporary artifact workflow and delete the orphan
output branch when done (branch deletion via `git push --delete` may be
blocked by this sandbox's git proxy — if so, just leave it, it's harmless,
or ask the user to delete it from GitHub's UI).

### 1.4 QEMU boot-testing locally

`/usr/bin/qemu-system-xtensa` doesn't support `esp32s3`. Use the same
Espressif-fork prebuilt binary and ROM that `qemu-boot-check.yml` uses:

```bash
git clone --depth 1 --branch prebuilt-binaries https://github.com/HeyVern/Qemu.git qemu-prebuilt
chmod +x qemu-prebuilt/xtensa-softmmu/linux-x86_64/qemu-system-xtensa

git clone --depth 1 --branch esp-develop https://github.com/heyvern/qemu.git qemu-src --filter=blob:none --sparse
git -C qemu-src sparse-checkout set pc-bios
mkdir -p rom && cp qemu-src/pc-bios/esp32s3_rev0_rom.bin rom/
```

Then build a merged flash image (bootloader + partitions + boot_app0 from
`docs/flasher/heltec_v4/`, app0 = your `firmware.bin`, per the offsets in
`docs/flasher/boards.json`) and run `.github/scripts/qemu_boot_check.py`
directly — it does the merging itself if given `--docs-flasher-dir` pointing
at a directory shaped like `docs/flasher/` with your test `firmware.bin`
substituted in at `<board>/<variant>/firmware.bin`:

```bash
python3 .github/scripts/qemu_boot_check.py \
  --board-id heltec_v4 --variant repeater \
  --boards-json <flasher_dir>/boards.json \
  --docs-flasher-dir <flasher_dir> \
  --qemu-bin qemu-prebuilt/xtensa-softmmu/linux-x86_64/qemu-system-xtensa \
  --rom-dir rom \
  --workdir <workdir>
```

**Important limitation to remember:** QEMU has no real WiFi/TLS stack and no
real hardware watchdog timing. It's good for boot-loop/crash sanity checks
(does the firmware come up cleanly, no panic loop) but it **cannot validate
anything about real network behavior, OTA download timing, or the class of
bug this project hit once already** (a watchdog panic-reset that only showed
up under real WiFi + real HTTPS teardown timing). Real-hardware testing is
the only way to verify those.

## 2. What needs to change to point at `mobmesh`

Only three places in this repo hardcode the `HeyVern`/`heyvern` owner name.
Everything else in the CI workflows is already self-referencing
(`github.repository`, `context.repo.owner`/`context.repo.repo`) and needs
**no changes** when the repo moves — `build-release.yml` in particular will
keep working correctly under any owner without editing.

1. **`README.md:51`** — the "Open the flasher" link:
   `https://heyvern.github.io/meshcore-hotspot-ota/flasher/` → update to the
   new Pages URL (see §4).
2. **`README.md:91`** — the `ota.fw.url` short-update example:
   `https://github.com/HeyVern/meshcore-hotspot-ota/raw/refs/heads/main/docs/flasher/heltec_v4/repeater/firmware.bin`
   → update the owner segment.
3. **`docs/flasher/flasher.js:1`** — `const REPO = "HeyVern/meshcore-hotspot-ota";`
   **This one is functional, not cosmetic** — it's used in a live GitHub API
   call (`fetch(\`https://api.github.com/repos/${REPO}/commits?...\`)`) to
   show the flasher page's "last updated" footer. Leaving this stale means
   that API call queries the *old* repo instead of the new one. Must be
   updated for the flasher page to work correctly post-move.

**Do not blindly find-and-replace every "HeyVern" in the repo** — two more
references exist in `.github/workflows/qemu-boot-check.yml` (lines with
`HeyVern/Qemu` and `heyvern/qemu`), but those point at the *separate* prebuilt
QEMU binary/ROM repos (see §1.4), not at this project. Only update those two
lines if `HeyVern/Qemu` and `heyvern/qemu` are *also* being moved to
`mobmesh` (see §3) — otherwise leave them pointed at `HeyVern`, they're just
external static-asset sources this project's CI happens to depend on.

## 3. Getting the files from `HeyVern` to `mobmesh`

I (the AI session that wrote this doc) do **not** have write access to
`mobmesh` — GitHub App/session scoping in this environment ties a session to
one owner's repos at a time, and this session was scoped to `HeyVern`. A
future session working in `mobmesh` will have the mirror problem (scoped to
`mobmesh`, no access back to `HeyVern`). So this move needs to happen either
by the human, or by an AI session with push access to *both* at once (e.g. a
session authenticated with a personal token that has access to both orgs,
rather than the per-owner GitHub App scoping used here).

**Repos to move** (see the earlier conversation in this project's history for
the reasoning): `meshcore-hotspot-ota` (the main repo — required),
`Qemu`/`prebuilt-binaries` branch and `qemu`/`esp-develop` branch (only if you
want CI fully self-contained under the new org — otherwise `HeyVern`'s
copies can stay put and continue serving `qemu-boot-check.yml`
indefinitely, they're just static binary sources). `meshcore-dev/MeshCore`
should **not** be moved or forked — it's external upstream, always fetched
fresh at build time.

**Two ways to do the actual move, pick one:**

- **GitHub repo transfer** (recommended if `mobmesh` should become the sole
  home): repo Settings → "Transfer ownership" → target `mobmesh`. Preserves
  full git history, issues, stars, and — importantly — GitHub automatically
  sets up a redirect from the old `HeyVern/...` URL to the new one (including
  for git clone/push, not just the web UI). This is the cleanest option and
  avoids stale-link breakage elsewhere (bookmarks, the `ota.fw.url` a device
  in the field might already have configured, etc.).
- **Manual mirror + push** (if you want both copies to keep existing
  independently, e.g. `HeyVern` stays as a personal fork): `git clone
  --mirror` the source repo, add the new repo as a remote, `git push --mirror`
  to it. No automatic redirect; loses issues/releases (those need separate
  handling via the GitHub API if you want them carried over too).

Either way, after the move: update the three files in §2, reconfigure GitHub
Pages on the new repo (§4), and re-run `build-release.yml` with
`force_rebuild: true` once to get a fresh, correctly-linked publish.

## 4. GitHub Pages setup

This repo's Pages site is **not** deployed via a GitHub Actions workflow —
there's no `pages-deploy`-style workflow file and no `_config.yml`/`CNAME`
in the repo. It's the classic "Deploy from a branch" mode, configured purely
in repo Settings (not in any file, so it doesn't travel automatically with a
transfer or mirror — you have to redo it on the new repo):

1. New repo's Settings → **Pages**.
2. Source: **Deploy from a branch**.
3. Branch: **`main`**, folder: **`/docs`**.
4. Save.

That's it — `docs/flasher/index.html` (or whatever the flasher's entry point
is) becomes reachable at `https://<owner>.github.io/<repo>/flasher/`, since
GitHub Pages serves the chosen folder as site root. No custom domain is
configured (no `CNAME` file), so the URL will be the default
`mobmesh.github.io/<repo-name>/...` pattern unless you deliberately add one.

## 5. Workflow inventory (what's permanent, what's scratch-only)

Only two workflow files should ever be permanently committed to `main`:

- **`build-release.yml`** (`sync-build-release`) — the actual release
  pipeline. Polls upstream MeshCore daily for new tags, applies
  `patches/*.patch`, builds both variants, publishes GitHub Releases, vendors
  `firmware.bin`/`firmware.bin.sha256` into `docs/flasher/heltec_v4/`, and
  regenerates `docs/cli-additions.md` from the patches' own doc diff (never
  hand-edit that file — it's overwritten on every run). Supports
  `workflow_dispatch` with `upstream_ref`, `variant`, and `force_rebuild`
  inputs for manual/forced runs.
- **`qemu-boot-check.yml`** — chained via `workflow_run` to fire
  automatically after `sync-build-release` succeeds. Boots the *exact*
  just-published release binary (not a special build) in QEMU and opens an
  issue if it crashes or loop-resets. Also `workflow_dispatch`-able manually.

Any other workflow file you see in this repo's history (`*-scratch-build.yml`,
`*-qemu-artifact.yml`, `*-hw-artifact.yml`, etc.) was **temporary**, added on
a feature branch for compile/hardware verification during that specific
change, and removed again before merging — per the pattern in §1.2–1.3. If
you ever find one of these still present on `main`, it's leftover cruft;
remove it.

## 6. Other things worth knowing

- **`docs/cli-additions.md` is generated, never hand-edited.** It's
  regenerated by `build-release.yml` from the patches' own embedded doc
  diffs every time it runs. If you need to change the CLI documentation,
  edit the relevant `.patch` file's `docs/cli_commands.md` hunk instead.
- **This project's own CLI naming/behavior is documented in
  `docs/cli-additions.md`** (generated) and mirrored by hand in `README.md`'s
  command table — keep both in sync when adding/renaming commands.
- **`docs/firmware-patch-guidelines.md`** has this project's own conventions
  for writing patches — read it before making patch edits if you haven't
  already absorbed the pattern from §1.1.
- **Real-hardware testing surfaces things QEMU/CI can't.** This project hit a
  genuine watchdog panic-reset bug (mid-stream HTTPS teardown ordering
  relative to `Update.abort()`) that only manifested on real hardware under
  real WiFi/TLS timing — two theories were tried and discarded (GPIO47 power
  brownout, then a naive "drain before close" fix) before the actual root
  cause (call *order* relative to `Update.abort()`, not just draining) was
  found through a precise A/B hardware test. When debugging something
  hardware-adjacent, prefer a real hardware round-trip with the user over
  more speculation — QEMU genuinely cannot validate everything.
