# Rust wipe-day blackbox

A solo, high-pop Rust wipe day, recorded as a verifiable decision graph and
checked against a handful of laws about the order of early decisions.

This is not a Rust plugin and it does not read the game. You hand it an event
log of what you did - built here, contested there, banked or died - and the
engine records each event as a commit, checks it against your declared laws, and
keeps the whole history replayable. It does not simulate the game or predict
outcomes; it tells you, byte-for-byte and after the fact, where a recorded wipe
first broke its own rules.

## The laws

Five rules encode preconditions that keep early progress bankable. Each fires on
a decision and requires a precondition edge between the same two objects:

- `no_hot_base_before_exit_ready` - do not commit a base site before it has an exit route.
- `no_monument_before_depot_ready` - do not contest a monument before a depot path exists.
- `no_recycler_before_bag_triangle` - do not contest a recycler before a bag triangle exists.
- `no_server_hop_before_failure_proven` - do not abandon the server before the loss is proven.
- `no_base_on_traffic_spine` - a base must not sit on a route it is visible from.

## The branches

Each branch ingests one wipe from `logs/*.jsonl` in observe mode, so laws annotate
every commit instead of blocking it:

- `beach_rush` - fast and exposed: base on the spawn-to-recycler spine, monument and
  recycler contested with no depot or bags, then a late-night server hop. Five violations.
- `shadow_spoke` - the same area, off the traffic spine: exit route first, then depot,
  then bags, then short pulses that get banked. Zero violations.
- `panic_hop` - leaving for a fresh server before the loss is proven. One violation.

## Run

```sh
pointerverse pack run rust_wipe_blackbox
```

`run.sh` builds the baseline, ingests the three logs, then reads the wipe back out
of the store:

- `audit first-broke beach_rush <law>` names the first commit that broke each law -
  the base on the spine, then the hot base, then the monument and recycler contests,
  then the server hop. The first break is the base decision, not the death.
- `audit timeline beach_rush SoloPlayer` replays the run as an ordered timeline.
- `branch compare beach_rush shadow_spoke` names the first commit where the two wipes
  diverge: the base-site decision, from which everything else follows.
- `repo explain shadow_spoke commit <hash>` shows the commit where progress stabilized
  and that every law passed there.
- `sentinel boot` and `repo fsck` confirm the recorded history is intact and replayable.
