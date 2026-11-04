# direct-retry

[↩ Back to firmware readme](../../README.md)

**MeshCore:** direct traffic follows one fixed path, so one missed hop loses the packet.

**Direct-Retry:** repeaters resend a direct packet when the next hop misses it.

```text
set direct.retry on
get direct.retry.stats
```

## What it does

- Off by default.
- **Resends:** after sending a direct packet, a repeater listens for the next hop to repeat it, and resends up to twice if it hears nothing. A last hop gets one resend, because a stock client cannot answer.
- **Hop replies:** a repeater that receives a direct packet addressed to it sends one short zero-hop reply, which stops the last relay's resend.

## CLI Commands

Detailed syntax and responses are in the [direct-retry CLI reference](docs/cli-additions.md).

[↩ Back to firmware readme](../../README.md)
