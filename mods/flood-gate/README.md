# flood-gate

[↩ Back to firmware readme](../../README.md)

Gates what a repeater repeats. Its `sinkhole` command stops forwarding channel floods that
entered the mesh through a listed repeater near their origin, or that are narrower than a path hash
width. Each entry is a timeout, 24 hours by default.

```text
sinkhole watch a1b2 chan public
sinkhole add a1b2 24 chan public
sinkhole add * 168 minbytes 2
sinkhole list
```

- A flood is dropped when the listed repeater is one of its first three hops (`depth` changes this); further down the path it forwards normally.
- `watch` counts matches without dropping, to check an entry before enforcing it.
- Only channel messages and datagrams are matched; adverts, direct messages, requests and logins always pass.
- Scoped and unscoped floods are treated alike. Direct-routed traffic is untouched.
- Packets are still received and processed locally; only forwarding stops.
- Time left counts uptime and survives a reboot.

A repeater's own packets leave with an empty path, so an entry catches what that repeater
relays, not what it sends.

## CLI Commands

Detailed syntax and responses are in the [flood-gate CLI reference](docs/cli-additions.md).

[↩ Back to firmware readme](../../README.md)
