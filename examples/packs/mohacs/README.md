# Mohács, 1526

On 29 August 1526 the Hungarian royal army gave battle at Mohács without waiting
for the Transylvanian and Croatian relief armies a few days' march away. The
battle was lost in under two hours, King Louis II drowned in flight, and the
Kingdom of Hungary was partitioned for a century and a half.

This pack builds that situation from the historical record - real commanders,
real numbers, the real date - as one baseline world, then forks it into the two
afternoons: the one that happened, and the one where the council waits.

```sh
pointerverse pack run mohacs
```

## What it builds

`world.pv` arrays both hosts on the plain and puts the relief armies on the
road. Numbers are entered as typed attributes, so a node reads like a ledger:

```sh
pointerverse repo --store .pack-store query main objects type Army
pointerverse repo --store .pack-store explain main object HungarianArmy
```

The baseline loads a reusable domain package, `mohacs_rules.pvdomain`, whose one
law states the decision the war council faced:

```txt
rule no_battle_before_reinforcements
when link RoyalArmy -> Battlefield : gives_battle
require before link RoyalArmy -> Battlefield : reinforced
deny reason "{from} gives battle at {to} before its reinforcements have joined"
```

## The two forks

`historical` gives battle early. With the law active, the engine rejects that
commit on the spot - the same mistake the chroniclers recorded - and the
afternoon then plays out: the charge, the artillery, Gazi Bali Bey's
encirclement, the rout, the king in the Csele stream, Buda.

`reinforced` waits. Janos Szapolyai's Transylvanian army and Christoph
Frankopan's Croatian force join first, the royal army reaches full strength, and
only then gives battle - so the same law passes and the line holds.

## Reading the divergence

```sh
pointerverse repo --store .pack-store why historical OttomanArmy defeats Louis_II
pointerverse repo --store .pack-store branch compare historical reinforced
pointerverse repo --store .pack-store explain reinforced commit <gives_battle>
pointerverse repo --store .pack-store fsck
```

`branch compare` names the first commit where the two afternoons diverge.
`explain` prints the exact delta and law status behind any commit. `fsck`
confirms the whole store is intact and replayable.

The history is real; the counterfactual is the point. Change one decision, run
the consequences, and prove every step.
