# Status and reports

[↩ Back to sync-settings readme](../README.md)

Region, policy and radio share two monitoring commands. Both show **this repeater's own
view**: what it is doing as a publisher, a subscriber, or neither.

| Command | Shows |
|---|---|
| `sync.<type> publish.status` | Current activity, progress, route and alerts. |
| `sync.<type> publish.report [page]` | How the most recent campaign ended. |

Replace `<type>` with `region`, `policy` or `radio`. Time has a status but no report; see
[Time status](#time-status).

## Region and policy status

Status shows the first applicable step:

| Status | Role |
|---|---|
| `step:aborting notes:n/2` | publishing |
| `step:publishing rnd:n/m` | publishing |
| `step:receiving chnk:n/m` or `step:staged` | receiving |
| `step:recovering` (policy only) | receiving |
| `step:quiet` — the guard after publishing ends | publishing |
| `step:idle` | neither |

For example, a region publisher that has completed one of seven rounds may show:

```text
sync.region publish.status
  -> sync:on layr:on step:publishing rnd:1/7
gen:42 tout:- lock:-
regn:* chnl:release
rpts:0 ERR:0
```

A policy subscriber partway through an update may show:

```text
sync.policy publish.status
  -> sync:on step:receiving chnk:1/3
gen:42 tout:11m lock:-
regn:* chnl:release
rpts:0 ERR:0
```

`gen` is the campaign generation; `tout` is the receiving timeout, `lock` is the
post-publication guard, and `rpts` counts available reports or warnings. `ERR` counts
current errors.

## Region and policy reports

Reports hold the latest outcome from either role; a newer event
replaces an older one. An outstanding abort warning appears as an extra first page.

- Publishing: `publication signing failed`, or an abort warning.
- Receiving: `applied`, `compressed payload could not be decoded`,
  `application failed; prior policy restored` or `application failed; campaign released`.

### Hop tally

On region and policy receiving outcomes only, the report adds how many
hops each frame took. The publisher never learns how far its frames travelled.

```text
sync.region publish.report 1
  -> 1/1 gen 42 applied; replay settled hops 0x1,1x3
```

- Each term is `<hops>x<frames>`: here the announcement arrived direct and three chunks
  came through one relay.
- Hop counts absent from the line had no frames; the token is omitted when nothing arrived.
- Duplicate copies are dropped before the mod sees them, so the tally shows the shortest
  route that arrived first.
- A campaign that never lands leaves nothing to tally: this diagnoses a working path, not
  a broken one.

## Radio status and reports

Radio keeps one migration record, so status and report read the same on the publishing
and receiving repeater. The report shows `committed`, `identical`, `cr-only`, `fallback`,
`aborted` or `fault`.

The status names the phase, R1 or R2, channel and cutover time. See [radio campaigns](radio.md) for the migration sequence.

## Time status

`sync.time publish.status` shows subscription state, schedule activity, the next and final sample times, and the last received sample.

Status words while a schedule is active:

| Word | Meaning |
|---|---|
| `waiting-clock` | After a restart the clock is not yet believable (before this firmware was built, or before the schedule began); nothing is sent until it is set. |
| `verifying` | The hotspot preflight is refreshing the publisher's clock. |
| `sending` | A sample has been handed to the radio. |
| `retry` | The last attempt did not complete; it retries after a minute. |

The last received sample appears as `last corrected <N>s` or `last within <N>s`, where `N` is
this repeater's clock minus the publisher's, or as `last replay` or `last storage`. Time has
no `publish.report`.

[↩ Back to sync-settings readme](../README.md)
