# Cockpit layout correction — revision 4

2026-09-18, following review of the first Godot preview film.

- Close the forward void with a tapered, sloping avionics cover joining the
  glare shield, side window sills and forward canopy coaming. Under-deck bearers
  tie it to the instrument housing and forward structure.
- Add three removable access covers with captive screws and low demister
  housings at the windshield base. These are geometry, not simulated ventilation.
- Remove the empty HUD footing, stanchions and rail. They were an unfinished
  visual placeholder, not a working display. A future HUD needs a deliberate
  eye-box, optical placement and UI design.
- Preserve the seat, camera, primary instruments and control positions. This
  chooses the larger-dashboard option; it does not certify the existing reach
  ergonomics or reconcile the independently authored cabin/exterior packaging.
- Rebuild all four cockpit GLB tiers and the editable Blender master. The far
  proxy also retains the closed dashboard. Ship and station are unchanged.

[Forward 4K PNG](media/freedom-cockpit-dashboard-v4.png) ·
[Leftward 4K PNG](media/freedom-cockpit-dashboard-left-v4.png)

These are actual Godot captures with the film's material/lighting setup, fixed
seated eye and terrain. Instruments remain static art. The original preview
film and its stills are deliberately unchanged revision-three history.

Validation: 27 mounting-envelope families, 12 floor rays, 12 forward-deck rays,
three unobstructed level seated sightlines, detached-mount negative control and
absence of HUD placeholder objects. All 12 asset GLBs pass finite geometry,
buffer/index bounds and LOD-budget checks. Cockpit triangle counts are 225,768 /
149,250 / 34,825 / 320 (hero / near / mid / far). Godot snapshot and streaming
contracts pass under both GCC and Clang. Three native 4K inspection views were
captured; the forward and leftward views were visually reviewed.

Provenance: `visual/hero-cockpit` and `visual/cockpit-dashboard-review-04` in
`assets/provenance.json`, BSD-3-Clause. Previous master/exports were backed up
locally before replacement; historical review images remain available.
