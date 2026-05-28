# Drift Boundary

Pointerverse should remain a lawful transformation kernel before it becomes an
inner-world or symbolic simulator.

Core code may define symbolic relation roles, but it must not assign mythic or
narrative behavior to them. A symbolic layer can exist later only by producing
deltas, passing verification, and writing trace events like every other layer.

Red flags:

- lore terms replacing laws or measurements
- direct world mutation outside delta commit
- observers reading privileged internal state instead of projections
- generated stories presented as model behavior
- pressure, region, or mythic continuity code inside `include/pv/core`
