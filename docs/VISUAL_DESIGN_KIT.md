# Apsis Drift visual design kit

This document describes revision 01. The current, substantially rebuilt masters,
render review, and actual geometry reduction tiers are documented in
[Hero asset studies / revision 02](HERO_ASSET_REVIEW.md).

The Wayfinder industrial design family gives the cockpit, starter ship, and
Origin Station one shared visual grammar: charcoal load paths, warm ceramic
pressure shells, exposed copper thermal hardware, safety-orange interfaces,
cyan navigation light, amber status light, and green docking confirmation.
Every design is authored at metre scale in Blender and is intentionally more
detailed than the current renderer needs.

## Design masters

The editable masters and portable exchange files live in `assets/visual/`:

| Design | Blender master | GLB exchange | Preview |
| --- | --- | --- | --- |
| Cockpit | `wayfinder-cockpit.blend` | `wayfinder-cockpit.glb` | `wayfinder-cockpit-preview.png` |
| Courier | `wayfinder-courier.blend` | `wayfinder-courier.glb` | `wayfinder-courier-preview.png` |
| Origin Station | `origin-apse-station.blend` | `origin-apse-station.glb` | `origin-apse-station-preview.png` |

All geometry, materials, lights, object names, and scene metadata are produced
deterministically by `tools/build_visual_designs.py`. Rebuild with Blender 5.2
or newer:

```sh
blender --background --python tools/build_visual_designs.py
```

## Cockpit: Wayfinder pilot cell

The 7.8 by 8.6 metre pressure cell is a single-seat cockpit built around the
game's existing presentation hierarchy. A wide split canopy is the physical
counterpart of the dynamic exterior viewport. Three replaceable MFDs sit below
the sightline, while side consoles provide the dense instrument rails used by
the terminal UI. Twin sticks map cleanly to flight and translation controls.

The flight cell retains critical physical controls: a backup horizon, guarded
abort and dock controls, pedals, overhead breakers, oxygen bottles, visible
service conduits, a rear iris hatch, and a five-point acceleration couch. The
orange A-pillars, central mullion, and cyan/amber display blocks remain legible
when reduced to a 320x240 exterior reference or an ANSI half-block study.

## Ship: Wayfinder orbital courier

Wayfinder 01 is a 28 metre reusable courier for station launch, orbital
rendezvous, atmospheric entry, low-level signal work, and ascent. The broad
blunt belly and lifting chines explain atmospheric survival; twin aft engine
nacelles and distributed RCS pods explain vacuum authority. Three landing legs
give the vehicle a stable planetary stance.

The dorsal spine carries service bays, FTL heat fins, antennae, and an
androgynous collar sized to the station ports. The canopy and pilot cell share
the cockpit's proportions. Orange wing edges and twin cyan engine throats make
the forward/aft orientation unmistakable at low resolution.

## Station: Origin Apse Ring

Origin Station is an orbital home rather than a generic wheel. Its 84 metre
inhabited ring encloses a zero-g operations hub, while four docking piers mark
the cardinal approach corridors. The canonical positive-X pier matches the
five-kilometre launch and docking corridor in the simulation contract.

The inhabited ring, axial hub, solar wings, radiator wings, propellant farm,
communications crown, and navigation beacons form distinct functional zones.
The 190 metre solar span creates a strong distant silhouette, and the repeated
orange collars make docking affordances readable before individual modules
resolve.

## Reduction ladder

The masters are deliberately overbuilt. Reduce them by preserving information
in this order:

1. **Hero/reference:** keep all assemblies, labels, conduits, panel buses,
   landing braces, lights, and material distinctions.
2. **Near flight:** remove text, fasteners, individual keys, solar bus lines,
   small cables, and hidden interior faces; retain docking collars, RCS pods,
   canopy frames, engine throats, major trusses, and emissive landmarks.
3. **Mid-distance:** collapse each functional zone into one mesh and use flat
   material IDs; retain the ship's chines and twin engines and the station's
   ring, four piers, mast, and paired wing families.
4. **Terminal silhouette:** use only large dark/light masses plus cyan, amber,
   and green beacons. The ship becomes canopy + lifting body + twin engines;
   the station becomes ring + cross + mast + solar bar.

The GLBs are exchange artifacts, not a commitment to a runtime asset system.
Application-owned rendering may instead translate the same shapes into
procedural primitives or hand-tuned meshes once the desired LOD is known.
