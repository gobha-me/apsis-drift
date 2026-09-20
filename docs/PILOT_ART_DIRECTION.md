# Pilot art direction after build 20

2026-09-19. Related issues: #267 (characters), #254 (sound).
Status: human-approved visual direction (concept v1), not an approved production
blueprint or runtime asset. Existing pilots remain placeholders, not a completed
character deliverable. The concept's human anatomy, clearly feminine female
character, tailored suit construction and material quality set the target.
Helmet/connector identity, consistent restraints and actual cockpit fit still
require authored design work; concept approval does not waive those checks.

## What failed

The fit-first mannequin construction was carried too far into visible art.
The current generator reuses identical joint anchors, limb radii, gloves,
boots and helmet for both variants. Small torso-radius changes do not change
the outer shoulder silhouette established by identical shoulder spheres.
Round limb sections, exposed ball-shaped joints and repeated radial folds
read as toy construction. Equal-length fingers and hard knuckle beads weaken
the impression of gloved hands.

The imported pilot materials retain their authored values; ship-material tuning
is not stripping their detail. They contain constant color/roughness factors,
not cloth weave, normal detail or differentiated wear. Cool fill lighting can
further flatten them, but changing the lights alone cannot repair the asset.

## Research: learn construction, do not copy costumes

- [FBFX's The Martian production account](https://www.fbfx.co.uk/film-the-martian)
  separates fabric construction from helmet, wrist, boot and other hardware,
  including mating helmet parts and air routing. Our lesson is functional
  garment/interface construction, not the film's recognizable suit layout.
- [FBFX's fabrication practice](https://www.fbfx.co.uk/fabric-fx) describes
  pattern cutting, tailored silhouettes, stretch behavior and surface design.
  Our proposed implementation is anatomy-first tailoring with folds that
  follow seams, compression and tension rather than repeated noisy rings.
- [Legacy Effects' The Expanse portfolio](https://www.legacyefx.com/expanse)
  documents fabricated suits and exoskeleton armor. Study believable assembly
  and body interaction without turning a civilian shuttle suit into copied
  military armor. [Paul Tobin's Weta designs](https://paul_tobin.artstation.com/projects/G8PRo3)
  are explicitly early concepts, not the final television costume.
- [Bethesda's art-direction account, via Microsoft](https://news.microsoft.com/source/features/work-life/behind-the-scenes-of-starfield-where-you-can-explore-a-thousand-new-worlds-among-the-stars/)
  describes grounded, relatable future technology. Use comprehensible
  manufacture and maintenance, not its insignia or signature color blocking.

These are reference links, not licensed source assets. No film/game media is
imported into the project or used as texture inputs.

## Proposed Apsis Drift suit language

A compact working pilot's pressure garment, not a bulky EVA backpack suit or
superhero exoskeleton. Woven petrol/slate-grey panels, graphite articulation
fabric and an offset sand-colored pressure-zip placket provide material and
construction identity. Keep amber service/identity markings sparse. A recessed
keyed service manifold and protected seat-side umbilical make the suit belong
to a craft; no loose hoses across controls. A low-profile squared-oval visor,
compact cheek seals and visible locking interfaces replace the egg helmet.
The general visual direction is approved; these particular interface/helmet
details still require refinement and do not establish new gameplay fiction.

Both characters need authored human anatomy under the garment. The female
variant must read clearly feminine in face, shoulder-to-hip relationship,
waist/chest tailoring and limb proportions, without relying on labels or color.
This is one selected female character design, not a universal body template.
Both receive equally capable, practical equipment. Do not substitute exaggerated
armor cups, corsetry or heels for character design. Gloves require differentiated
finger lengths and convincing grip contact; boots need believable flexible lasts.

Preserve external cockpit eye, grip and pedal constraints. Internal joint
locations and limb shapes may differ and must be refitted, not held identical
merely because the old fixture used one skeleton. Separate engineering fit from
art approval. Seat adjustment remains a future player feature, not implemented
by this direction sheet.

## Asset plan 3a / 3b: appearance and interaction

User-approved extension, 2026-09-20:

- **3a — pilot assets:** high-quality male/female anatomy, tailored equipment,
  cockpit fit, materials and exported appearance remain in
  [#267](https://github.com/gobha-me/apsis-drift/issues/267).
- **3b — animated interaction:** corresponding arms/hands and authored controls
  move when flight actions occur, tracked separately in
  [#277](https://github.com/gobha-me/apsis-drift/issues/277).

Prepare rigs, grip anchors and movable control pivots during 3a. Start motion
once seated proportions and cockpit fit are stable, before final material
polish. First prove continuous hand/control tracking and believable limb/suit
deformation; follow with a bounded discrete reach/press/return interaction.
Pedals move only for actions assigned to them in the authored cockpit layout.

Drive presentation from semantic demands/actions and actual system state, not
specific controller buttons. Equivalent remapped or keyboard inputs must give
equivalent motion. An attempted/refused action must not falsely indicate an
accepted system change. Animation cannot delay flight response or mutate the
authoritative simulation. Preserve grip contact, neutral/pause/focus safety,
pose continuity across camera changes and unobstructed first-person head-look.

Static art and in-engine motion have separate review gates for both pilots.
This does not add walking/EVA locomotion, autopilot, NPC behavior or a generic
animation engine, and no animation completion is claimed by this plan.

## Isolated seated rig proof — 2026-09-20

One female anatomy has now been seated against the existing eye, grip and pedal
datums without rescaling the cockpit or stretching limbs. An 11.36-degree torso
recline resolves the seated pose. A 49-frame, three-second Blender study moves
the grips through a provisional +/-8-degree pitch/roll sweep; this is a reach
and deformation experiment, not an accepted authored control range or a live
semantic action mapping. Independent checks of the saved moving grip meshes
and wrists agree throughout the sweep; maximum arm reach is 87% of arm length.

The borrowed CC0 bib coverall and shoes are fit-test clothing, not the original
flight suit. Known fit limits include about 7.5 mm of cushion overlap and a
13.25 mm heel gap. Restraints, helmet, hair, canopy collision, first-person
visibility and Godot export/motion acceptance are unproved. Male fit remains
pending. Static and animated Blender masters, PNGs, the short clip, input
licenses and verified provenance stay in isolated ignored authoring output;
no new character or third-party tooling is promoted into the runtime here.

## Acceptance sequence

1. Approve distinct male/female silhouettes and original suit identity in
   neutral standing and seated concept views. A generated concept is only a
   proposal: check matching interfaces and restraint routing before modeling.
2. Build anatomy and connected garment forms before small surface detail.
   Review grey-clay geometry, bends/compression, restraint contact and grip fit.
3. Author fabric, elastomer, visor and hardware materials. Wear must follow
   contact/service areas. Keep high-detail sources; derive reusable textures,
   packed maps and LODs after quality is demonstrated, not before.
4. Inspect the actual exported mesh in Godot at gameplay distance and close up,
   in daylight, low light and orbital conditions, at 1080p and 4K. Include
   cockpit/downward head-look and exterior visibility plus a motion clip for
   shimmer, specular response and aliasing. Do not use a studio/concept image
   as evidence that the runtime matches it.
5. Record source/license/provenance, fit results, draw calls, memory and packaged
   size independently of visual acceptance. Then obtain human approval.

## Sound feedback alongside the art review

The build 20 engine was heard as a fan/air filter. The authored voice has a
54–68 Hz fundamental and a substantial broadband layer. An offline matching
85%-thrust fixture has its added atmosphere about 14 dB below the engine,
so atmospheric gain alone does not explain that fixture's character. Filtering
out low frequencies can expose the noisy engine layer; this is a diagnostic
hypothesis, not a measurement of the reviewer's speakers or flight conditions.

The next listening comparison isolates engine-only vacuum sound: audible
low-mid mechanical weight and irregular structure-borne resonance, with much
less persistent noise. Compare alternatives at matched levels before touching
the live mix. Airflow remains a separate pressure-dependent atmosphere layer;
ventilation may exist quietly as cabin machinery but must not define thrust.
No new engine fiction, spatial-audio completion or human approval is implied.

The follow-up audition selected direction 02 (pulsed structural resonance) as
the better starting point, but it still sounded like hovering. That is a
preference, not final sound acceptance. The next offline comparison retains its
resonant body while reducing regular flutter and adding load-dependent texture
and restrained spool/settling behavior. It does not replace the running game's
audio or add an audible association between thrust and absolute flight speed.

Subsequent human review preferred direction 22 / 02 and requested a denser
third component blended with both 22 variants for a comfortable continuous
engine range. Further audio direction is recorded in
[Healthy ship sound](SHIP_SOUND_DIRECTION.md).
