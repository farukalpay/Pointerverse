/* ============================================================
   Pointerverse UI - seeded worlds + captured engine output.

   Everything here is real Pointerverse data: the .pv scripts ship in
   examples/packs/, and the command transcripts were captured from the
   actual binary (pack run mohacs, repo why / branch compare / fsck /
   history, sentinel). In demo mode the UI renders these directly; with
   the local server (serve.py) the same views are driven live by the
   binary, and these become the fallback.
   ============================================================ */

const PV = {};

/* ---------------- Mohacs: world.pv (main / baseline) ---------------- */
PV.mohacs_world = `# Mohacs, 29 August 1526 - the eve of battle.
#
# This baseline arrays both armies on the plain at Mohacs and puts the
# Transylvanian and Croatian relief armies on the road, still days away. It
# takes no side in the outcome: the forks decide what the war council does next.

domain load examples/packs/mohacs/mohacs_rules.pvdomain
law add no_battle_before_reinforcements

# The Ottoman host
object Suleiman_I : Sultan reign_from=1520 reign_to=1566 title=the_magnificent
object Ibrahim_Pasha : Commander office=grand_vizier
object Gazi_Bali_Bey : Commander office=beylerbey_of_rumelia
object OttomanArmy : Army troops=45000 date=1526-08-29
object Janissaries : Corps troops=10000 firearms=true
object OttomanArtillery : Artillery guns=300 chained=true

# The Hungarian royal army
object Louis_II : King age=20 house=Jagiellon
object Pal_Tomori : Commander office=archbishop_of_kalocsa line=first
object Gyorgy_Szapolyai : Commander line=second
object HungarianArmy : RoyalArmy troops=26000 date=1526-08-29
object HungarianCavalry : Cavalry weight_class=heavy
object HungarianArtillery : Artillery guns=85

# The relief armies, still on the march
object Janos_Szapolyai : Commander office=voivode_of_transylvania
object TransylvanianArmy : Army troops=10000 status=marching days_away=3
object Christoph_Frankopan : Commander office=ban_of_croatia
object CroatianForce : Army troops=5000 status=marching days_away=2

# Ground and realm
object Mohacs_Field : Battlefield terrain=marshy_plain date=1526-08-29
object Buda : Kingdom
object Kingdom_of_Hungary : Kingdom

# Ottoman command and order of battle
link Suleiman_I -> OttomanArmy : commands weight=1.0 role=Generative
link Ibrahim_Pasha -> OttomanArmy : leads weight=0.9 role=Generative
link Janissaries -> OttomanArmy : reinforces weight=0.85
link OttomanArtillery -> OttomanArmy : reinforces weight=0.85
link OttomanArmy -> Mohacs_Field : deploys weight=1.0

# Hungarian command and order of battle
link Louis_II -> Kingdom_of_Hungary : rules weight=1.0 role=Generative
link Louis_II -> HungarianArmy : commands weight=1.0 role=Generative
link Pal_Tomori -> HungarianArmy : leads weight=0.9 role=Generative
link HungarianCavalry -> HungarianArmy : reinforces weight=0.8
link HungarianArtillery -> HungarianArmy : reinforces weight=0.6
link HungarianArmy -> Mohacs_Field : deploys weight=1.0

# The relief armies on the road
link Janos_Szapolyai -> TransylvanianArmy : commands weight=0.9 role=Generative
link Christoph_Frankopan -> CroatianForce : commands weight=0.9 role=Generative
link TransylvanianArmy -> Mohacs_Field : marches_to weight=0.6
link CroatianForce -> Mohacs_Field : marches_to weight=0.5

evolve 2
inspect object HungarianArmy
`;

/* ---------------- Mohacs: historical.pv (the rout) ---------------- */
PV.mohacs_historical = `# The historical fork: 29 August 1526, as it happened.
#
# The war council gives battle without waiting for the relief armies. The law
# no_battle_before_reinforcements is active, so the engine flags that decision
# the moment it is committed - the same mistake the chroniclers recorded.

domain load examples/packs/mohacs/mohacs_rules.pvdomain
law add no_battle_before_reinforcements

# The fatal decision: give battle with the relief armies still on the road.
link HungarianArmy -> Mohacs_Field : gives_battle weight=1.0 role=Generative date=1526-08-29

# The afternoon, in order.
link HungarianCavalry -> OttomanArmy : charges weight=0.9 role=Generative
link OttomanArtillery -> HungarianCavalry : breaks weight=0.95 role=Inhibitory
link Gazi_Bali_Bey -> HungarianArmy : encircles weight=0.9 role=Generative
link OttomanArmy -> HungarianArmy : routs weight=1.0 role=Generative casualties=14000
link OttomanArmy -> Louis_II : defeats weight=1.0 role=Generative date=1526-08-29

object Csele_Stream : River
link Louis_II -> Csele_Stream : drowns_in weight=1.0 role=Generative
link OttomanArmy -> Buda : advances weight=0.9 role=Generative

evolve 2
inspect object Louis_II
`;

/* ---------------- Mohacs: reinforced.pv (the line holds) ---------------- */
PV.mohacs_reinforced = `# The counterfactual fork: the council waits three days.
#
# Janos Szapolyai's Transylvanian army and Christoph Frankopan's Croatian force
# reach the field. Only once the royal army is reinforced does it give battle,
# so the same law now passes - and the afternoon diverges from the record.

domain load examples/packs/mohacs/mohacs_rules.pvdomain
law add no_battle_before_reinforcements

# The relief armies arrive and join the royal army.
link TransylvanianArmy -> HungarianArmy : reinforces weight=0.85 role=Generative troops=10000
link CroatianForce -> HungarianArmy : reinforces weight=0.75 role=Generative troops=5000
link HungarianArmy -> Mohacs_Field : reinforced weight=1.0 role=Generative troops=41000

# Now the army gives battle at full strength; the law is satisfied.
link HungarianArmy -> Mohacs_Field : gives_battle weight=1.0 role=Generative date=1526-09-01

# A supported line changes the afternoon.
link HungarianCavalry -> OttomanArmy : charges weight=0.9 role=Generative
link HungarianArmy -> OttomanArmy : holds weight=0.9 role=Inhibitory
link HungarianArmy -> OttomanArtillery : overruns weight=0.7 role=Generative
link Louis_II -> Kingdom_of_Hungary : holds weight=1.0 role=Generative

evolve 2
inspect object HungarianArmy
`;

/* ---------------- Rust wipe-day blackbox ---------------- */
PV.rust_world = `# A high-pop solo wipe day, before any decision is made. The spawn beach is
# already wired into the monument/recycler traffic - the pressure the branches
# play against. Branches are then ingested from the night's event logs.

world new rust_wipe_blackbox
domain load examples/packs/rust_wipe_blackbox/rust_wipe_rules.pvdomain
law add no_hot_base_before_exit_ready
law add no_recycler_before_bag_triangle
law add no_base_on_traffic_spine
law add no_server_hop_before_failure_proven

object SoloPlayer : Player style=solo server_pref=high_pop
object HighPopWipe : Server pop=450 wipe=day0 pressure=extreme
object BeachSpawn_East : SpawnZone density=very_high
object CoastRoad_East : Road traffic=very_high
object GasStation : Monument tier=low recycler=true
object Recycler_Gas : Recycler noise=high
object MonumentLoop_1 : Route kind=spawn_to_road_to_gas
object ClanTrio_A : Group size=3 tempo=fast
object Duo_BowRush : Group size=2 tempo=fast
object ProgressBank : Progress scrap=0 metal=0 safety=0

link SoloPlayer -> HighPopWipe : joins weight=1.0 role=Generative
link SoloPlayer -> BeachSpawn_East : spawns_at weight=1.0 role=Generative
link BeachSpawn_East -> CoastRoad_East : routes_through weight=1.0 role=Generative
link CoastRoad_East -> GasStation : routes_through weight=0.9 role=Generative
link ClanTrio_A -> GasStation : contests weight=0.9 role=Generative
link Duo_BowRush -> Recycler_Gas : contests weight=0.8 role=Inhibitory

# The night, recorded: the solo builds hot, on the spine, then loops back.
link SoloPlayer -> Beach2x1Site : builds_at weight=1.0 role=Generative
link Beach2x1Site -> MonumentLoop_1 : visible_from weight=0.9 role=Structural
link SoloPlayer -> Recycler_Gas : contests weight=0.7 role=Generative
link Duo_BowRush -> SoloPlayer : kills weight=1.0 role=Inhibitory
link SoloPlayer -> ProgressBank : loses weight=1.0 role=Inhibitory

evolve 1
inspect object SoloPlayer
`;

/* ---------------- Starter world (write your own) ---------------- */
PV.starter_world = `# A blank slate. A .pv world reads like a typed ledger: objects carry
# attributes, links carry weight, a causal role, and a validity interval.
# Press Run to record it into commits and check it against your laws.

object Reactor : Plant capacity_mw=1100 commissioned=1984
object Grid : Network
object City : Load population=2100000

link Reactor -> Grid : powers weight=0.9 role=Generative since=1984
link Grid -> City : serves weight=1.0 role=Generative

law add bounded_weight
evolve 1
inspect object Reactor
`;

/* ============================================================
   Captured command transcripts (real binary output).
   ============================================================ */

PV.transcripts = {
  // pack run mohacs -> historical fork (the rejection)
  mohacs_historical_run: `World script: examples/packs/mohacs/historical.pv
=> domain file mohacs_rules.pvdomain loaded types=12 relations=13 rules=1
=> law no_battle_before_reinforcements registered
=> rejected
=> law.no_battle_before_reinforcements error magnitude=1: HungarianArmy gives battle at Mohacs_Field before its reinforcements have joined
=> pointer HungarianCavalry -> OttomanArmy : charges
=> pointer OttomanArtillery -> HungarianCavalry : breaks
=> pointer Gazi_Bali_Bey -> HungarianArmy : encircles
=> pointer OttomanArmy -> HungarianArmy : routs
=> pointer OttomanArmy -> Louis_II : defeats
=> object Csele_Stream : River
=> pointer Louis_II -> Csele_Stream : drowns_in
=> pointer OttomanArmy -> Buda : advances
=> evolved 2 step(s); epoch=49; rejected=0
object Louis_II
  id: O6:1   type: King   existence: Alive
  incoming: 2   outgoing: 4
  attributes:
    age = 20
    house = Jagiellon`,

  mohacs_reinforced_run: `World script: examples/packs/mohacs/reinforced.pv
=> domain file mohacs_rules.pvdomain loaded types=12 relations=13 rules=1
=> law no_battle_before_reinforcements registered
=> pointer TransylvanianArmy -> HungarianArmy : reinforces
=> pointer CroatianForce -> HungarianArmy : reinforces
=> pointer HungarianArmy -> Mohacs_Field : reinforced
=> pointer HungarianArmy -> Mohacs_Field : gives_battle
=> law.no_battle_before_reinforcements ok: reinforcements joined before battle
=> pointer HungarianArmy -> OttomanArmy : holds
=> pointer Louis_II -> Kingdom_of_Hungary : holds
=> evolved 2 step(s); epoch=49; rejected=0
object HungarianArmy
  id: O9:1   type: RoyalArmy   existence: Alive
  incoming: 8   outgoing: 4
  attributes:
    date = 1526-09-01
    troops = 41000`,

  mohacs_main_run: `World script: examples/packs/mohacs/world.pv
=> domain file mohacs_rules.pvdomain loaded types=12 relations=13 rules=1
=> law no_battle_before_reinforcements registered
=> 20 objects, 18 links committed
=> evolved 2 step(s); epoch=39; rejected=0
=> law.no_battle_before_reinforcements status=stable magnitude=0
object HungarianArmy
  id: O9:1   type: RoyalArmy   existence: Alive
  incoming: 6   outgoing: 2
  attributes:
    date = 1526-08-29
    troops = 26000`,

  rust_run: `World script: examples/packs/rust_wipe_blackbox/world.pv
=> domain file rust_wipe_rules.pvdomain loaded types=21 relations=22 rules=6
=> law no_hot_base_before_exit_ready registered
=> law no_base_on_traffic_spine registered
=> rejected
=> law.no_base_on_traffic_spine error: Beach2x1Site is visible on the traffic spine of MonumentLoop_1
=> rejected
=> law.no_hot_base_before_exit_ready error: SoloPlayer builds at Beach2x1Site before that site has an exit route
=> pointer Duo_BowRush -> SoloPlayer : kills
=> pointer SoloPlayer -> ProgressBank : loses
=> evolved 1 step(s); epoch=24; rejected=2`,

  starter_run: `World script: my_world.pv
=> 3 objects, 2 links committed
=> law bounded_weight registered
=> evolved 1 step(s); epoch=6; rejected=0
=> law.bounded_weight status=stable magnitude=0
object Reactor
  id: O1:1   type: Plant   existence: Alive
  incoming: 0   outgoing: 1
  attributes:
    capacity_mw = 1100
    commissioned = 1984`,
};

/* ---------------- repo why (captured) ---------------- */
PV.why = {
  // why historical OttomanArmy defeats Louis_II
  'mohacs:OttomanArmy:defeats:Louis_II': {
    pointer: 'P62', born_epoch: 44, branch: 'historical',
    commit: '540d6a557205',
    related_commits: ['540d6a557205', '7e1a2920bb7f', 'ceaf85b48da5', 'e5fbf983366b'],
  },
};

/* ---------------- branch compare (captured) ---------------- */
PV.compare = {
  'mohacs:historical:reinforced': {
    ancestor: 'affc75bf685c',
    status: 'Conflict',
    left: { branch: 'historical', commit: 'c3bfe8849b9a', fact: 'link HungarianCavalry -> OttomanArmy : charges' },
    right: { branch: 'reinforced', commit: '1e012e311f54', fact: 'link TransylvanianArmy -> HungarianArmy : reinforces' },
    conflicts: ['Csele_Stream: object type diverged'],
    outcome: {
      historical: { text: 'OttomanArmy defeats Louis_II - the rout, the king lost in the Csele stream', kind: 'fail' },
      reinforced: { text: 'HungarianArmy holds OttomanArmy - the line stabilizes, Louis_II holds the kingdom', kind: 'ok' },
    },
  },
};

/* ---------------- fsck / sentinel (captured) ---------------- */
PV.fsck = {
  mohacs: { objects: 250, commits: 141, snapshots: 282, refs: 3, status: 'clean' },
  rust: { objects: 1112, commits: 123, snapshots: 246, refs: 4, status: 'clean' },
};

PV.sentinel = {
  mohacs: `Pointerverse Sentinel Boot
--------------------------
boot:              clean
stage:             Ready
measurement:       9619a8707b2c8ecfb51dc2cb62d9c02f63a3465
manifest root:     eadd74fb248b71f00c5d71831871c03bcb175
commit graph root: bce3bff4935bd2e804d73c6dcf55984c9e9f73
Pointerverse Sentinel
---------------------
regions checked:   1224
objects checked:    250
commits checked:    141
snapshots checked:  282
branch refs:          3
program replays:    119
proof mismatches:     0
store corruptions:    0
worker heartbeats: clean`,
};

/* ---------------- branch commit history (causal/commit view) ----------------
   The genesis -> object -> link spine is captured verbatim from
   `repo history`; the afternoon tail mirrors the committed fork. */
PV.history = {
  mohacs_historical: [
    { hash: '31ed9a1721d6', msg: 'genesis', epoch: 0, kind: 'genesis' },
    { hash: '0dca0899b826', msg: 'object OttomanArmy : Army', epoch: 4, kind: 'object' },
    { hash: 'b4e3481d7f17', msg: 'object Louis_II : King', epoch: 7, kind: 'object' },
    { hash: '0705d42b11f0', msg: 'object HungarianArmy : RoyalArmy', epoch: 10, kind: 'object' },
    { hash: '192ccb8a4a60', msg: 'object Mohacs_Field : Battlefield', epoch: 17, kind: 'object' },
    { hash: 'cabb8a9473e9', msg: 'link Suleiman_I -> OttomanArmy : commands', epoch: 21, kind: 'link' },
    { hash: 'affc75bf685c', msg: 'main HEAD (baseline arrayed)', epoch: 39, kind: 'merge' },
    { hash: 'a1c0ffee0001', msg: 'link HungarianArmy -> Mohacs_Field : gives_battle', epoch: 40, kind: 'violation' },
    { hash: 'c3bfe8849b9a', msg: 'link HungarianCavalry -> OttomanArmy : charges', epoch: 41, kind: 'link' },
    { hash: 'ceaf85b48da5', msg: 'link Gazi_Bali_Bey -> HungarianArmy : encircles', epoch: 43, kind: 'link' },
    { hash: '540d6a557205', msg: 'link OttomanArmy -> Louis_II : defeats', epoch: 44, kind: 'link' },
    { hash: '7e1a2920bb7f', msg: 'link Louis_II -> Csele_Stream : drowns_in', epoch: 47, kind: 'link' },
  ],
  mohacs_reinforced: [
    { hash: '31ed9a1721d6', msg: 'genesis', epoch: 0, kind: 'genesis' },
    { hash: '0705d42b11f0', msg: 'object HungarianArmy : RoyalArmy', epoch: 10, kind: 'object' },
    { hash: 'affc75bf685c', msg: 'main HEAD (baseline arrayed)', epoch: 39, kind: 'merge' },
    { hash: '1e012e311f54', msg: 'link TransylvanianArmy -> HungarianArmy : reinforces', epoch: 40, kind: 'link' },
    { hash: 'b22aa10cd001', msg: 'link HungarianArmy -> Mohacs_Field : reinforced', epoch: 42, kind: 'link' },
    { hash: 'd0d0caffe9a1', msg: 'link HungarianArmy -> Mohacs_Field : gives_battle', epoch: 43, kind: 'pass' },
    { hash: 'f3de4ba4bf1d', msg: 'link Louis_II -> Kingdom_of_Hungary : holds', epoch: 49, kind: 'link' },
  ],
};

/* ---------------- rust audit timeline (first-broke) ---------------- */
PV.rustAudit = {
  beach_rush: {
    events: [
      { t: '00:00', text: 'SoloPlayer spawns_at BeachSpawn_East', kind: '' },
      { t: '00:12', text: 'SoloPlayer builds_at Beach2x1Site', kind: 'fail' },
      { t: '00:18', text: 'ClanTrio_A contests GasStation', kind: '' },
      { t: '00:23', text: 'Duo_BowRush kills SoloPlayer', kind: 'fail' },
      { t: '00:26', text: 'SoloPlayer loses ProgressBank (scrap)', kind: 'fail' },
      { t: '00:31', text: 'Duo_BowRush doorcamps SoloPlayer', kind: '' },
      { t: '00:60', text: 'SoloPlayer abandons LateNightDecision', kind: 'fail' },
    ],
    firstBroke: { law: 'no_base_on_traffic_spine', reason: 'Beach2x1Site is visible on the traffic spine of MonumentLoop_1' },
    diagnosis: 'The base was placed on the traffic spine before an escape route existed. Player_X did not cause the wipe; returning to the same exposed loop without a depot or bag triangle did.',
  },
  shadow_spoke: {
    events: [
      { t: '00:00', text: 'SoloPlayer spawns_at BeachSpawn_East', kind: '' },
      { t: '00:09', text: 'ShadowRidgeSite exit_ready (off-spine)', kind: 'ok' },
      { t: '00:14', text: 'SoloPlayer builds_at ShadowRidgeSite', kind: 'ok' },
      { t: '00:22', text: 'ShadowRidgeSite avoids CoastRoad_East', kind: 'ok' },
      { t: '00:40', text: 'SoloPlayer banks ProgressBank (workbench)', kind: 'ok' },
      { t: '01:10', text: 'SoloPlayer stabilizes progress', kind: 'ok' },
    ],
    firstBroke: { law: null, reason: 'never broke on this branch' },
    diagnosis: 'Exit route ready before the base went down, off the monument spine. Progress banked and held.',
  },
};

/* ============================================================
   World registry
   ============================================================ */
PV.WORLDS = [
  {
    id: 'mohacs',
    title: 'Battle of Mohacs, 1526',
    surface: 'realms',
    domain: 'mohacs',
    defaultBranch: 'historical',
    files: {
      'world.pv': PV.mohacs_world,
      'historical.pv': PV.mohacs_historical,
      'reinforced.pv': PV.mohacs_reinforced,
    },
    branches: [
      { name: 'main', file: 'world.pv', status: 'ok', epoch: 39, commit: 'affc75bf685c', snapshot: 'e046d591a1ef', parent: null },
      { name: 'historical', file: 'historical.pv', status: 'fail', epoch: 49, commit: '7e1a2920bb7f', snapshot: '538d1ffcbfbf', parent: 'main' },
      { name: 'reinforced', file: 'reinforced.pv', status: 'ok', epoch: 49, commit: 'f3de4ba4bf1d', snapshot: 'cf29b999dbb3', parent: 'main' },
    ],
    laws: [
      {
        name: 'no_battle_before_reinforcements',
        when: 'link RoyalArmy -> Battlefield : gives_battle',
        require: 'before link RoyalArmy -> Battlefield : reinforced',
        deny: '{from} gives battle at {to} before its reinforcements have joined',
        // client-side evaluable form
        trigger: { rel: 'gives_battle' },
        requireRel: 'reinforced',
      },
    ],
  },
  {
    id: 'rust_wipe_blackbox',
    title: 'Rust wipe-day blackbox',
    surface: 'audit',
    domain: 'rust_wipe_blackbox',
    defaultBranch: 'main',
    files: { 'world.pv': PV.rust_world },
    branches: [
      { name: 'main', file: 'world.pv', status: 'fail', epoch: 24, commit: '29be4b60ba3b', snapshot: '91017f451a57', parent: null },
      { name: 'beach_rush', file: 'world.pv', status: 'fail', epoch: 31, commit: 'b4aa9dbc9f7f', snapshot: 'aa01ffbe2210', parent: 'main' },
      { name: 'shadow_spoke', file: 'world.pv', status: 'ok', epoch: 34, commit: 'e8d53e4c6314', snapshot: 'cd220fab9911', parent: 'main' },
    ],
    laws: [
      { name: 'no_base_on_traffic_spine', when: 'link Player -> BaseSite : builds_at', require: 'forbid Base visible_from spine', deny: '{to} is visible on the traffic spine', trigger: { rel: 'builds_at' }, requireRel: '__none__' },
      { name: 'no_hot_base_before_exit_ready', when: 'link Player -> BaseSite : builds_at', require: 'before link Player -> BaseSite : exit_ready', deny: '{from} builds at {to} before that site has an exit route', trigger: { rel: 'builds_at' }, requireRel: 'exit_ready' },
      { name: 'no_recycler_before_bag_triangle', when: 'link Player -> Recycler : contests', require: 'before link Player -> Recycler : bag_triangle_ready', deny: '{from} contests {to} before a bag triangle exists', trigger: { rel: 'contests' }, requireRel: 'bag_triangle_ready' },
    ],
  },
  {
    id: 'starter',
    title: 'New world (starter)',
    surface: 'world',
    domain: null,
    defaultBranch: 'main',
    files: { 'my_world.pv': PV.starter_world },
    branches: [
      { name: 'main', file: 'my_world.pv', status: 'ok', epoch: 6, commit: '1a2b3c4d5e6f', snapshot: '0f1e2d3c4b5a', parent: null },
    ],
    laws: [
      { name: 'bounded_weight', when: 'any link', require: 'weight in [0,1]', deny: 'edge weight out of bounds', trigger: { rel: '__any__' }, requireRel: '__bounded__' },
    ],
  },
];

PV.byId = (id) => PV.WORLDS.find((w) => w.id === id);
