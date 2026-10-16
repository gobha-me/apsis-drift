extends SceneTree
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
const DistantContacts = preload("res://scripts/native/native_distant_contacts.gd")
const PlanetOptics = preload("res://scripts/native/native_planet_optics.gd")
const SurfaceAppearance = preload("res://scripts/native/native_surface_appearance.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const WalkView = preload("res://scripts/native/native_walk_view.gd")
const Shell = preload("res://scripts/native/native_start_shell.gd")
var failures := 0
func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)
func _initialize() -> void:
	# Closed packet, finite geometry and bounded buffers precede native staging.
	check_distant_contacts()
	call_deferred("run")
func run() -> void:
	if failures > 0: quit(1); return
	var args := OS.get_cmdline_user_args()
	if args.size() not in [5, 7]: quit(1); return
	for path in args:
		if not path.is_absolute_path(): quit(1); return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	check(ClassDB.class_exists("FreedomBridge"), "Bridge load refused")
	if not ClassDB.class_exists("FreedomBridge"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if owner == null: quit(1); return
	check(owner.stage_freedom_continue(args[0]), "Existing native save refused: " + str(owner.get_last_error()))
	if failures > 0: owner = null; quit(1); return
	var pending: Dictionary = owner.get_pending_freedom_start()
	check(not pending.is_empty() and owner.commit_pending_freedom_start(pending.candidate_id), "Read-only terrain fixture refused")
	if failures > 0: owner = null; quit(1); return
	var state: Dictionary = owner.get_freedom_flight_state()
	check(NativeEnvironment.valid(state) and FlightView.valid_state(state), "Actual authoritative environment rejected")
	if failures > 0: owner = null; quit(1); return
	check(owner.save_freedom_as(args[1]), "Before world bytes unavailable")
	if failures > 0: owner = null; quit(1); return
	var before := FileAccess.get_sha256(args[1])
	check_surface_appearance(state)
	check_planet_optics(state, args[1] + ".optics.json")
	for key in ["scale_height", "sea_density", "atmosphere_edge", "star_angular_radius"]:
		for value in [NAN, INF, -INF, -1.0, "7", true, null, Vector3.ONE]:
			var malformed := state.duplicate(true)
			malformed.lighting[key] = value
			check(not NativeEnvironment.valid(malformed) and not FlightView.valid_state(malformed), "Malformed coefficient accepted: " + key)
	for key in ["planet_radius", "altitude"]:
		for value in [NAN, INF, "7", true, null]:
			var malformed := state.duplicate(true)
			malformed[key] = value
			check(not NativeEnvironment.valid(malformed), "Malformed body dimensions accepted")
	for key in state.lighting:
		var malformed := state.duplicate(true)
		malformed.lighting.erase(key)
		check(not NativeEnvironment.valid(malformed), "Missing environment field accepted: " + key)
	var environment := Environment.new()
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	var material := NativeEnvironment.install(environment)
	var light := DirectionalLight3D.new()
	light.light_energy = 2.25
	var local_light := DirectionalLight3D.new()
	check(NativeEnvironment.apply(state, environment, light, material, local_light), "Actual environment binding refused")
	check(environment.background_mode == Environment.BG_SKY and material.get_shader_parameter("sun_direction") == state.star_direction and light.light_color == state.lighting.star_color, "Actual star transfer mismatch")
	for pair in [["radius", state.planet_radius], ["altitude", state.altitude], ["sea_density", state.lighting.sea_density], ["scale_height", state.lighting.scale_height], ["atmosphere_edge", state.atmosphere_edge]]:
		check(material.get_shader_parameter(pair[0]) == pair[1], "Coefficient transfer mismatch: " + pair[0])
	var old_energy := environment.ambient_light_energy
	var old_local_energy := local_light.light_energy
	var old_light_color := light.light_color
	var old_direction: Variant = material.get_shader_parameter("sun_direction")
	var invalid := state.duplicate(true)
	invalid.lighting.tick = "18446744073709551615"
	check(NativeEnvironment.sun_visibility(invalid) == -1.0, "Invalid shadow query accepted")
	check(not NativeEnvironment.apply(invalid, environment, light, material, local_light) and environment.ambient_light_energy == old_energy and material.get_shader_parameter("sun_direction") == old_direction and local_light.light_energy == old_local_energy and light.light_energy == 2.25 and light.light_color == old_light_color, "Rejected environment changed material or sunlight")
	check(not NativeEnvironment.apply_sky(invalid, material) and material.get_shader_parameter("sun_direction") == old_direction, "Rejected isolated sky transfer changed accepted sky")
	check(NativeEnvironment.apply_sky(state, material) and environment.ambient_light_energy == old_energy and local_light.light_energy == old_local_energy and light.light_energy == 2.25, "Sky-only transfer changed ambient or sunlight")
	# Synthetic direction fixtures qualify consumer behavior, not C++ day/night.
	var energies: Array[float] = []
	for direction in [Vector3.UP, Vector3.RIGHT, Vector3.DOWN]:
		var sample := state.duplicate(true)
		sample.altitude = 0.0
		sample.star_direction = direction
		sample.lighting.direction = direction
		check(NativeEnvironment.apply(sample, environment, light, material, local_light), "Consumer phase fixture refused")
		energies.append(environment.ambient_light_energy)
		check(is_equal_approx(local_light.light_energy, 1.0 if direction == Vector3.UP else (0.5 if direction == Vector3.RIGHT else 0.0)) and light.light_energy == 2.25, "Local shadow incorrectly changed terrain sunlight")
	check(energies[0] > energies[1] and energies[1] > energies[2] and is_equal_approx(energies[2], 0.06), "Ambient phase response is incoherent")
	# Angular-limb regressions: height changes the visible horizon, and a
	# large stellar disk at the anti-solar pole can still be wholly occulted.
	for height_ratio in [0.0, 0.04, 1.5, 10.0]:
		var horizon := -acos(1.0 / (1.0 + height_ratio))
		for offset in [-0.02, 0.0, 0.02]:
			var sample := state.duplicate(true)
			sample.altitude = sample.planet_radius * height_ratio
			sample.star_direction = Vector3(cos(horizon + offset), sin(horizon + offset), 0.0)
			sample.lighting.direction = sample.star_direction
			sample.lighting.star_angular_radius = 0.01
			check(absf(NativeEnvironment.sun_visibility(sample) - (0.0 if offset < 0 else (1.0 if offset > 0 else 0.5))) < 0.0001, "Angular limb coverage changes with height")
	var anti_solar := state.duplicate(true)
	anti_solar.altitude = anti_solar.planet_radius * 1.5
	anti_solar.star_direction = Vector3.DOWN
	anti_solar.lighting.direction = Vector3.DOWN
	anti_solar.lighting.star_angular_radius = 0.183547532694299
	check(NativeEnvironment.sun_visibility(anti_solar) == 0.0, "High-altitude anti-solar sunlight leaked through planet")
	var low_sun := state.duplicate(true)
	low_sun.lighting.star_angular_radius = 0.00465
	low_sun.star_direction = Vector3(1.0, -0.1, 0.0).normalized()
	low_sun.lighting.direction = low_sun.star_direction
	low_sun.altitude = 0.0
	check(NativeEnvironment.sun_visibility(low_sun) == 0.0, "Surface below-horizon star visible")
	low_sun.altitude = 250000.0
	check(NativeEnvironment.sun_visibility(low_sun) == 1.0, "Elevated below-horizontal star wrongly hidden")
	var airless := state.duplicate(true)
	airless.atmosphere_edge = 0.0
	airless.lighting.atmosphere_edge = 0.0
	airless.lighting.scale_height = 0.0
	airless.lighting.sea_density = 0.0
	check(NativeEnvironment.apply(airless, environment, light, material, local_light) and material.get_shader_parameter("sea_density") == 0.0, "Airless fixture refused")
	check(owner.get_freedom_flight_state() == state and owner.save_freedom_as(args[1]) and FileAccess.get_sha256(args[1]) == before, "Presentation changed authoritative world bytes")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual next-tick command refused")
	var advanced: Dictionary = owner.get_freedom_flight_state()
	check(advanced.tick != state.tick and NativeEnvironment.apply(advanced, environment, light, material, local_light) and material.get_shader_parameter("sun_direction") == advanced.star_direction, "Environment retained stale tick or direction")
	var updated := args[1] + ".updated.json"
	check(owner.save_freedom_as(updated), "Updated authoritative save refused")
	var continued: Variant = ClassDB.instantiate("FreedomBridge")
	var loaded: bool = continued.stage_freedom_continue(updated)
	check(loaded, "Updated native Continue refused: " + str(continued.get_last_error()))
	if loaded:
		var next_pending: Dictionary = continued.get_pending_freedom_start()
		check(continued.commit_pending_freedom_start(next_pending.candidate_id), "Updated pending world refused")
		var restored: Dictionary = continued.get_freedom_flight_state()
		check(restored.get("lighting") == advanced.lighting and restored.get("body_basis") == advanced.body_basis, "Continue changed canonical illumination or orientation")
	continued = null
	# Independently generated, actual C++ saved port configurations. Their
	# clocks/catalog1 geometry are real; these are not continuously flown states.
	for index in range(2, 5):
		var configured: Variant = ClassDB.instantiate("FreedomBridge")
		var staged: bool = configured.stage_freedom_continue(args[index])
		check(staged, "Saved shadow configuration refused")
		if staged:
			var candidate: Dictionary = configured.get_pending_freedom_start()
			check(configured.commit_pending_freedom_start(candidate.candidate_id), "Saved shadow configuration did not commit")
			var observed: Dictionary = configured.get_freedom_flight_state()
			check(NativeEnvironment.apply(observed, environment, light, material, local_light), "C++ saved shadow illumination refused")
			var expected := 1.0 if index == 2 else 0.0
			check((is_equal_approx(local_light.light_energy, expected) if index < 4 else local_light.light_energy > 0.0 and local_light.light_energy < 1.0) and light.light_energy == 2.25 and is_equal_approx(material.get_shader_parameter("sun_visibility"), local_light.light_energy), "Local/terrain/sky disagreement at actual saved shadow phase")
			var preserved := args[1] + ".shadow-%d.json" % index
			check(configured.save_freedom_as(preserved) and FileAccess.get_sha256(preserved) == FileAccess.get_sha256(args[index]) and configured.get_freedom_flight_state() == observed, "Shadow presentation changed complete world bytes")
		configured = null
	# Real catalog2 fixed-anchor phases; only the adjacent terminator pair
	# is continuously stepped, using the ordinary authoritative 120Hz clock.
	if args.size() == 7:
		daynight_phases(args[5], args[6], args[1], environment, light, material, local_light)
	light.free()
	local_light.free()
	owner = null
	await process_frame
	print("Native environment consumer: %d failures; synthetic angular fixtures and actual saved C++ port phases, actual catalog2 anchored phases when supplied, no raster/hardware claim" % failures)
	quit(0 if failures == 0 else 1)

func check_distant_contacts() -> void:
	var state := {"system_id": "21", "tick": "120", "distant_contacts": {"version": 1, "selected": true, "system_id": "21", "tick": "120", "contacts": [{"id": "11", "label": "Unidentified contact", "status": "visible", "direction": Vector3.FORWARD, "angular_radius": 0.01, "light_direction": Vector3.UP, "tint": Color(0.7, 0.6, 0.5, 1.0), "spin_radians": 0.4, "reason": "Distant optical contact"}, {"id": "12", "label": "Unidentified contact", "status": "unresolved", "direction": Vector3.RIGHT, "angular_radius": 0.000001, "light_direction": Vector3.UP, "tint": Color.WHITE, "spin_radians": -PI, "reason": "Below optical resolution"}]}}
	var original := var_to_bytes(state)
	var first := ShaderMaterial.new()
	first.shader = NativeEnvironment.SkyShader
	var second := ShaderMaterial.new()
	second.shader = NativeEnvironment.SkyShader
	var materials: Array[ShaderMaterial] = [first, second]
	check(DistantContacts.valid(state) and DistantContacts.apply(state, materials), "Valid distant contacts refused")
	for material in materials:
		check(material.get_shader_parameter("distant_contact_count") == 1 and material.get_shader_parameter("distant_contact_directions")[0] == Vector3.FORWARD and is_equal_approx(material.get_shader_parameter("distant_contact_angular_radii")[0], 0.01), "Resolved geometry lost its true angular size or unresolved contact became a silhouette")
	check(DistantContacts.selected_contact(state, "12") == state.distant_contacts.contacts[1] and DistantContacts.selected_contact(state, "99").is_empty(), "Contact selection invented a catalog entry")
	var malformed_states: Array[Dictionary] = []
	for invalid in [null, false, 1, [], "contacts"]:
		var malformed := state.duplicate(true)
		malformed.distant_contacts = invalid
		malformed_states.append(malformed)
	for key in state.distant_contacts:
		var malformed := state.duplicate(true)
		malformed.distant_contacts.erase(key)
		malformed_states.append(malformed)
	for changes in [{"version": 1.0}, {"version": 2}, {"selected": 1}, {"selected": false}, {"system_id": "22"}, {"tick": "121"}, {"contacts": null}, {"contacts": [null]}, {"extra": true}]:
		var malformed := state.duplicate(true)
		malformed.distant_contacts.merge(changes, true)
		malformed_states.append(malformed)
	for key in state.distant_contacts.contacts[0]:
		var malformed := state.duplicate(true)
		malformed.distant_contacts.contacts[0].erase(key)
		malformed_states.append(malformed)
	for changes in [{"id": "01"}, {"id": "-1"}, {"id": "18446744073709551616"}, {"id": ""}, {"id": 11}, {"label": null}, {"label": "x".repeat(65)}, {"reason": "x".repeat(161)}, {"status": null}, {"status": "landable"}, {"direction": Vector3.ZERO}, {"direction": Vector3(INF, 0, 0)}, {"direction": Vector3(NAN, 0, 0)}, {"direction": null}, {"light_direction": Vector3.ONE}, {"light_direction": null}, {"angular_radius": NAN}, {"angular_radius": INF}, {"angular_radius": -0.001}, {"angular_radius": 0.20001}, {"angular_radius": 0}, {"spin_radians": NAN}, {"spin_radians": PI + 0.0001}, {"spin_radians": -PI - 0.0001}, {"tint": null}, {"tint": Color(INF, 0, 0, 1)}, {"tint": Color(0, 0, 0, 0.5)}, {"tint": Color(-0.01, 0, 0, 1)}, {"extra": true}]:
		var malformed := state.duplicate(true)
		malformed.distant_contacts.contacts[0].merge(changes, true)
		malformed_states.append(malformed)
	var duplicate := state.duplicate(true)
	duplicate.distant_contacts.contacts[1].id = "11"
	malformed_states.append(duplicate)
	var oversized := state.duplicate(true)
	oversized.distant_contacts.contacts.append(state.distant_contacts.contacts[0].duplicate(true))
	malformed_states.append(oversized)
	var accepted := {}
	for uniform in ["distant_contact_count", "distant_contact_directions", "distant_contact_angular_radii", "distant_contact_light_directions", "distant_contact_tints", "distant_contact_spins"]:
		accepted[uniform] = first.get_shader_parameter(uniform)
	for malformed in malformed_states:
		check(not DistantContacts.valid(malformed) and not DistantContacts.apply(malformed, materials), "Malformed distant packet accepted")
		for material in materials:
			for uniform in accepted:
				check(material.get_shader_parameter(uniform) == accepted[uniform], "Rejected packet changed previously accepted sky: " + uniform)
	var wrong := ShaderMaterial.new()
	wrong.shader = preload("res://shaders/terrain.gdshader")
	check(not DistantContacts.apply({}, [first, wrong]) and not DistantContacts.apply({}, [first, null]), "Invalid later sky destination accepted")
	for uniform in accepted:
		check(first.get_shader_parameter(uniform) == accepted[uniform], "Invalid later destination changed the first accepted sky: " + uniform)
	for status in ["unresolved", "occluded", "close_unavailable"]:
		var hidden := state.duplicate(true)
		hidden.distant_contacts.contacts[0].status = status
		check(DistantContacts.apply(hidden, materials) and first.get_shader_parameter("distant_contact_count") == 0, "Unsupported or hidden contact was rendered as a physical silhouette")
	var boundaries := state.duplicate(true)
	boundaries.distant_contacts.contacts[0].angular_radius = 0.2
	boundaries.distant_contacts.contacts[0].spin_radians = PI
	boundaries.distant_contacts.contacts[1].angular_radius = 0.0
	check(DistantContacts.valid(boundaries), "Inclusive distant geometry bounds refused")
	for historical in [{}, {"system_id": "21", "tick": "120", "distant_contacts": {"version": 1, "selected": false, "system_id": "21", "tick": "120", "contacts": []}}]:
		check(DistantContacts.apply(historical, materials) and first.get_shader_parameter("distant_contact_count") == 0, "Historical absent/unselected contacts left sky rendering enabled")
	check(var_to_bytes(state) == original, "Distant contact presenter mutated authoritative input")

func check_surface_appearance(state: Dictionary) -> void:
	if not state.has("surface_appearance") or not SurfaceAppearance.valid(state):
		check(false, "Actual C++ surface appearance packet is unavailable")
		return
	var presenter := SurfaceAppearance.new()
	var first := ShaderMaterial.new()
	first.shader = preload("res://shaders/terrain.gdshader")
	var replacement := ShaderMaterial.new()
	replacement.shader = first.shader
	if not presenter.select(state) or not presenter.apply([first]):
		check(false, "Actual surface profile could not bind terrain")
		return
	check(presenter.changed and first.get_shader_parameter("surface_profile_enabled"), "First material did not select its actual appearance")
	var bindings := [["surface_albedo_scale", "albedo_scale_basis_points"],
		["surface_detail_contrast", "detail_contrast_basis_points"],
		["surface_roughness", "roughness_basis_points"],
		["surface_specular", "specular_basis_points"]]
	for pair in bindings:
		check(is_equal_approx(float(first.get_shader_parameter(pair[0])), float(state.surface_appearance[pair[1]]) / 10000.0), "Surface material differs from C++ coefficient: " + pair[0])
	# Static packets do not request another full terrain-material update. A tile
	# created later still receives the accepted profile before becoming visible.
	check(presenter.select(state) and not presenter.changed and presenter.apply([replacement]), "Repeated profile changed or replacement material lost appearance")
	for pair in bindings:
		check(first.get_shader_parameter(pair[0]) == replacement.get_shader_parameter(pair[0]), "Replacement terrain appearance differs")
	var accepted: Dictionary = presenter.selected.duplicate(true)
	var changes: Array = [{"version": 2}, {"version": true}, {"disclosure": 0},
		{"system_id": "0" if state.system_id != "0" else "1"},
		{"planet_id": "0" if state.planet_id != "0" else "1"}, {"seed": "42"}]
	for field in SurfaceAppearance.LIMITS:
		for value in [-1, SurfaceAppearance.LIMITS[field] + 1, 65535, NAN, INF, 1.0, true, null, "1"]:
			changes.append({field: value})
	for change: Dictionary in changes:
		var malformed: Dictionary = state.duplicate(true)
		malformed.surface_appearance.merge(change, true)
		check(not SurfaceAppearance.valid(malformed) and not FlightView.valid_state(malformed) and not presenter.select(malformed) and presenter.selected == accepted, "Malformed appearance changed accepted profile: " + str(change.keys()))
	for field in state.surface_appearance:
		var missing: Dictionary = state.duplicate(true)
		missing.surface_appearance.erase(field)
		check(not presenter.select(missing) and presenter.selected == accepted, "Incomplete appearance changed accepted profile")
	# A later invalid destination must not partly update earlier destinations.
	first.set_shader_parameter("surface_roughness", 0.123)
	check(not presenter.apply([first, null]) and is_equal_approx(float(first.get_shader_parameter("surface_roughness")), 0.123), "Rejected destination set partially mutated terrain")
	check(presenter.apply([first]), "Accepted material could not restore its profile")
	var legacy: Dictionary = state.duplicate(true)
	legacy.erase("surface_appearance")
	check(SurfaceAppearance.valid(legacy) and presenter.select(legacy) and presenter.changed and presenter.apply([first, replacement]) and presenter.selected.is_empty(), "Missing historical packet did not select neutral fallback")
	check(not first.get_shader_parameter("surface_profile_enabled") and not replacement.get_shader_parameter("surface_profile_enabled"), "Neutral fallback retained selected appearance")
	check(presenter.select(state) and presenter.changed and presenter.apply([replacement]) and replacement.get_shader_parameter("surface_profile_enabled"), "Actual appearance could not replace fallback")

func check_planet_optics(historical: Dictionary, output: String) -> void:
	check(not historical.has("optics") and PlanetOptics.valid(historical), "Historical save gained an optical selection")
	check(DistantContacts.valid(historical) and historical.get("distant_contacts") is Dictionary and not historical.distant_contacts.selected and historical.distant_contacts.contacts.is_empty(), "Historical save gained a secondary population selection")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	# Pure bridge packet check: use the explicit pending transaction. The
	# ordinary shell separately owns model staging before displaying a journey.
	if not owner.stage_freedom_new_game("42"):
		check(false, "Actual optical New Game could not stage: " + str(owner.get_last_error()))
		return
	var candidate: Dictionary = owner.get_pending_freedom_start()
	if not owner.commit_pending_freedom_start(candidate.candidate_id):
		check(false, "Actual optical New Game could not commit")
		return
	var state: Dictionary = owner.get_freedom_flight_state()
	if not PlanetOptics.valid(state) or not state.has("optics"):
		check(false, "Actual C++ optical packet failed validation")
		return
	check(owner.save_freedom_as(output), "Optical baseline Save As refused")
	var bytes := FileAccess.get_file_as_bytes(output)
	var environment := Environment.new()
	var sky := NativeEnvironment.install(environment)
	# Reuse this actual New Game: no second world stage or synthetic catalog.
	check_actual_distant_contacts(owner, state, sky, output, bytes)
	var terrain := ShaderMaterial.new()
	terrain.shader = preload("res://shaders/terrain.gdshader")
	var materials: Array[ShaderMaterial] = [sky, terrain]
	var optics := PlanetOptics.new()
	if not optics.apply(state, materials):
		check(false, "Actual optical atlas could not bind sky and terrain")
		return
	var texture: ImageTexture = optics.atlas_texture
	check(texture != null and texture.get_width() == 64 and texture.get_height() == 32 and texture.get_image().get_data() == state.optics.density, "Renderer atlas differs from the actual C++ density bytes")
	for repeat in 3:
		check(optics.apply(state, materials) and optics.atlas_texture == texture and owner.get_freedom_flight_state() == state, "Repeated optical reads rebuilt the atlas or changed world state")
	for material in materials:
		check(material.get_shader_parameter("optical_atlas") == texture and material.get_shader_parameter("optical_enabled") == (state.optics.opacity_cap > 0.0) and material.get_shader_parameter("optical_planet_center") == state.optics.planet_center and material.get_shader_parameter("optical_fixed_from_tangent").is_equal_approx(state.optics.fixed_to_tangent.inverse()), "Sky and terrain did not share the actual atlas and C++ owner transform")
	var short := PackedByteArray()
	short.resize(2047)
	var oversized := PackedByteArray()
	oversized.resize(2049)
	var broken_pole: PackedByteArray = state.optics.density.duplicate()
	broken_pole[1] = (int(broken_pole[0]) + 1) % 256
	# Presentation corruption controls only; no forged packet reaches C++.
	for change in [{"system_id": "1" if state.system_id == "0" else "0"},
		{"planet_id": "1" if state.planet_id == "0" else "0"},
		{"version": 2}, {"disclosure": 0}, {"width": 63}, {"height": 33},
		{"density": short}, {"density": oversized}, {"density": broken_pole},
		{"opacity_cap": NAN}, {"opacity_cap": INF}, {"opacity_cap": 0.76},
		{"lower_height_metres": NAN}, {"upper_height_metres": INF},
		{"reference_radius": state.planet_radius + 1.0},
		{"planet_center": Vector3.ZERO}, {"planet_center": Vector3(NAN, 0, 0)},
		{"fixed_to_tangent": Basis(Vector3(-1, 0, 0), Vector3.UP, Vector3.BACK)},
		{"fixed_to_tangent": Basis(Vector3(2, 0, 0), Vector3.UP, Vector3.BACK)},
		{"fixed_to_tangent": Basis(Vector3(NAN, 0, 0), Vector3.UP, Vector3.BACK)}]:
		var malformed := state.duplicate(true)
		malformed.optics.merge(change, true)
		check(not PlanetOptics.valid(malformed) and not optics.apply(malformed, materials) and optics.atlas_texture == texture, "Malformed optics packet changed accepted graphics: " + str(change.keys()))
		for material in materials:
			check(material.get_shader_parameter("optical_atlas") == texture and material.get_shader_parameter("optical_enabled") == (state.optics.opacity_cap > 0.0), "Rejected optics packet changed a bound material")
	check(optics.apply(historical, materials) and optics.atlas_texture == null and optics.atlas_bytes.is_empty() and optics.atlas_owner.is_empty(), "Historical clear state retained a selected atlas cache")
	for material in materials:
		check(not material.get_shader_parameter("optical_enabled") and material.get_shader_parameter("optical_atlas") == null, "Historical clear state left optical rendering enabled")
	check(owner.get_freedom_flight_state() == state and owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == bytes, "Optical presentation changed complete New Game save bytes or clock")

func check_actual_distant_contacts(owner: Variant, state: Dictionary, sky: ShaderMaterial, output: String, bytes: PackedByteArray) -> void:
	var packet: Variant = state.get("distant_contacts")
	if not check_actual_contact_packet(state): return
	check(NativeEnvironment.apply_sky(state, sky), "Actual New Game contact/lighting sky transfer refused")
	var visible := 0
	for contact: Dictionary in packet.contacts:
		if contact.status == "visible" and contact.angular_radius > 0.0: visible += 1
	check(sky.get_shader_parameter("distant_contact_count") == visible, "Native sky invented an eligible actual contact")
	# The New Game actor is still walking in the station. Exercise the ordinary
	# FlightView's local selection refresh over its actual flight projection,
	# without claiming a staged seated/controller interaction or adding assets.
	var view := FlightView.new()
	view.state = state
	for contact: Dictionary in packet.contacts:
		view.selected_contact_id = contact.id
		view.refresh_distant_contacts()
		check(view.selected_contact_id == contact.id and DistantContacts.selected_contact(view.state, view.selected_contact_id) == contact, "View-local selection lost an actual C++ optical contact")
		check(owner.get_freedom_flight_state() == state, "Contact selection advanced or changed actual C++ flight")
	view.selected_contact_id = ""
	view.refresh_distant_contacts()
	check(view.selected_contact_id.is_empty() and DistantContacts.selected_contact(view.state, "").is_empty(), "View-local selection clearing invented a target")
	view.free()
	check(owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == bytes and owner.get_freedom_flight_state() == state, "Actual optical reads/selection changed complete New Game save, clock or checksum")
	var continued: Variant = ClassDB.instantiate("FreedomBridge")
	if not continued.stage_freedom_continue(output):
		check(false, "Actual selected companions Continue staging refused: " + str(continued.get_last_error()))
		return
	var pending: Dictionary = continued.get_pending_freedom_start()
	if not continued.commit_pending_freedom_start(pending.candidate_id):
		check(false, "Actual selected companions Continue commit refused")
		return
	var restored: Dictionary = continued.get_freedom_flight_state()
	check(check_actual_contact_packet(restored) and restored.distant_contacts == packet and restored.tick == state.tick and restored.checksum == state.checksum, "Continue changed actual companion selection, projection or same-tick ownership")
	var restored_path := output + ".contacts-continued.json"
	check(continued.save_freedom_as(restored_path) and FileAccess.get_file_as_bytes(restored_path) == bytes, "Companion Continue changed complete canonical New Game save")
	continued = null

func check_actual_contact_packet(state: Dictionary) -> bool:
	var packet: Variant = state.get("distant_contacts")
	var valid: bool = DistantContacts.valid(state) and packet is Dictionary and packet.selected and packet.contacts.size() == 2 and packet.system_id == state.system_id and packet.tick == state.tick
	check(valid, "Actual C++ New Game lacks two closed same-tick selected optical contacts")
	if not valid: return false
	for contact: Dictionary in packet.contacts:
		check(contact.label == "Unidentified contact" and contact.size() == 9, "Actual optical packet leaked a catalog name or additional body metadata")
	return true

func daynight_visibility(elevation: float, horizon: float, width: float) -> float:
	var fraction := clampf((elevation - horizon + width) / (2.0 * width), 0.0, 1.0)
	return fraction * fraction * (3.0 - 2.0 * fraction)

func daynight_phases(directory: String, assets: String, output: String, environment: Environment, light: DirectionalLight3D, material: ShaderMaterial, local_light: DirectionalLight3D) -> void:
	var decoded: Variant = JSON.parse_string(FileAccess.get_file_as_string(directory.path_join("manifest.json")))
	check(decoded is Dictionary, "Actual day/night manifest is unreadable")
	if not decoded is Dictionary: return
	var manifest: Dictionary = decoded
	check(manifest.get("schema_version") == 1 and manifest.get("catalog_generator") == 2 and manifest.get("ephemeris_generator") == 2 and manifest.get("terrain_policy") == 2 and manifest.get("phases") is Array and manifest.phases.size() == 5, "Day/night manifest selects an unsupported owner or phase count")
	if failures > 0: return
	var labels := ["day", "terminator-before", "terminator", "terminator-after", "night"]
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	var anchored_basis: Basis
	var previous_direction := Vector3.ZERO
	var previous_tick := -1
	var crossing: Variant = null
	var crossing_shell: Control = null
	for index in range(5):
		var phase: Variant = manifest.phases[index]
		check(phase is Dictionary and phase.get("name") == labels[index] and phase.get("file") == labels[index] + ".json" and NativeEnvironment.decimal(phase.get("tick")) and phase.get("star_direction_native") is Array and phase.star_direction_native.size() == 3, "Malformed day/night phase descriptor")
		if failures > 0: return
		var source := directory.path_join(labels[index] + ".json")
		var source_hash := FileAccess.get_sha256(source)
		check(not source_hash.is_empty(), "Day/night canonical source missing")
		var configured: Variant = ClassDB.instantiate("FreedomBridge")
		var shell := Shell.new()
		shell.presentation_only = true
		shell.process_mode = Node.PROCESS_MODE_DISABLED
		root.add_child(shell)
		shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		var staged: bool = shell.select_start(configured, {"mode": "continue", "value": source, "assets": assets})
		check(staged, "Actual day/night Continue/view refused: " + shell.error)
		if not staged: shell.free(); configured = null; return
		var view: Control = shell.current_view
		check(view is FlightView and view.activated and view.paused and view.error.is_empty() and view.staged_model.valid_installed() and view.exhaust.valid_bound_skin(), "Day/night actual asset presenter is incomplete or unpaused")
		if failures > 0: shell.free(); configured = null; return
		view.set_process(false)
		var state: Dictionary = configured.get_freedom_flight_state()
		var expected_direction := Vector3(float(phase.star_direction_native[0]), float(phase.star_direction_native[1]), float(phase.star_direction_native[2]))
		check(NativeEnvironment.valid(state) and FlightView.valid_state(state) and state.tick == phase.tick and state.system_id == manifest.system_id and state.planet_id == manifest.planet_id and state.terrain_policy == 2 and state.lighting.catalog_generator == 2, "Actual day/night owner, clock or projection mismatch")
		check(state.star_direction.distance_to(expected_direction) < 0.000002 and absf(state.altitude - float(phase.altitude_metres)) < 0.0000002 and absf(state.lighting.star_angular_radius - float(phase.star_angular_radius_radians)) < 0.000000000001, "Native day/night inputs differ from independent matrix/ENU oracle")
		if index == 0:
			anchored_basis = state.body_basis
		else:
			check(state.body_basis.x.distance_to(anchored_basis.x) < 0.000002 and state.body_basis.y.distance_to(anchored_basis.y) < 0.000002 and state.body_basis.z.distance_to(anchored_basis.z) < 0.000002, "Body orientation jumped at the fixed surface anchor")
		var expected_visibility := daynight_visibility(float(phase.solar_elevation_radians), float(phase.horizon_radians), float(phase.star_angular_radius_radians))
		check(NativeEnvironment.apply(state, environment, light, material, local_light), "Actual catalog2 phase transfer refused")
		check(absf(local_light.light_energy - expected_visibility) < 0.0002 and is_equal_approx(material.get_shader_parameter("sun_visibility"), local_light.light_energy) and material.get_shader_parameter("sun_direction") == state.star_direction and light.light_energy == 2.25 and light.light_color == state.lighting.star_color, "Sky/local sunlight/terrain direction disagree at actual phase")
		daynight_view(view, state, expected_visibility)
		if index == 0: check(is_equal_approx(local_light.light_energy, 1.0), "Actual day is not illuminated")
		if index == 4: check(is_equal_approx(local_light.light_energy, 0.0), "Actual night leaks local direct sunlight")
		if index in [1, 2, 3]:
			check(absf(local_light.light_energy - 0.5) < 0.01, "Actual adjacent terminator is not partially illuminated")
			if index > 1:
				check(int(state.tick) == previous_tick + 1 and state.star_direction.distance_to(previous_direction) < 0.00001, "Actual adjacent clock/illumination crossing is discontinuous")
		previous_direction = state.star_direction
		previous_tick = int(state.tick)
		var preserved: String = output + ".daynight-" + labels[index] + ".json"
		check(configured.save_freedom_as(preserved) and FileAccess.get_sha256(preserved) == source_hash, "Day/night transfer changed complete canonical save")
		check(configured.advance_freedom_flight(60.0, neutral, true) and configured.get_freedom_flight_state() == state and configured.save_freedom_as(preserved) and FileAccess.get_sha256(preserved) == source_hash, "Paused day/night changed full clock, state or save")
		var restored: Variant = ClassDB.instantiate("FreedomBridge")
		var restored_ok: bool = restored.stage_freedom_continue(preserved)
		check(restored_ok, "Day/night saved Continue refused")
		if restored_ok:
			var restored_pending: Dictionary = restored.get_pending_freedom_start()
			check(restored.commit_pending_freedom_start(restored_pending.candidate_id) and restored.get_freedom_flight_state() == state, "Day/night Continue changed full projection or orientation")
		restored = null
		if index == 1:
			crossing = configured
			crossing_shell = shell
		if index in [2, 3]:
			check(crossing.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual landed terminator step refused")
			var transitioned: Dictionary = crossing.get_freedom_flight_state()
			check(transitioned == state and NativeEnvironment.apply(transitioned, environment, light, material, local_light), "Real adjacent terminator step differs from independently initialized canonical phase")
			crossing_shell.current_view.state = transitioned
			daynight_view(crossing_shell.current_view, transitioned, expected_visibility)
			var transition_save := output + ".daynight-crossing-%d.json" % index
			check(crossing.save_freedom_as(transition_save) and FileAccess.get_sha256(transition_save) == source_hash, "Real terminator crossing changed expected complete canonical save")
		check(FileAccess.get_sha256(source) == source_hash, "Day/night fixture source was rewritten")
		if index != 1: shell.free()
		configured = null
	if crossing_shell != null: crossing_shell.free()
	crossing = null

func daynight_view(view: Control, state: Dictionary, visibility: float) -> void:
	view.update_view(0.0)
	check(view.error.is_empty() and view.state == state and view.paused and view.staged_model.valid_current_pose(), "Day/night native view refresh changed state or lost actual model pose")
	check(view.surface_appearance.initialized and not view.surface_appearance.changed and view.surface_appearance.selected == state.surface_appearance, "Live view lost its static C++ material profile")
	# Exercise the actual view's new-tile hook without inventing geometry or
	# waiting for a different terrain cover at these fixed sunlight fixtures.
	var replacement := ShaderMaterial.new()
	replacement.shader = preload("res://shaders/terrain.gdshader")
	view.terrain.terrain_material_created.emit(replacement)
	check(view.error.is_empty() and replacement.get_shader_parameter("surface_profile_enabled") and is_equal_approx(float(replacement.get_shader_parameter("surface_albedo_scale")), float(state.surface_appearance.albedo_scale_basis_points) / 10000.0), "Live replacement material missed canonical appearance")
	check(view.sky_material.get_shader_parameter("sun_direction") == state.star_direction and absf(view.sky_material.get_shader_parameter("sun_visibility") - visibility) < 0.0002 and absf(view.local_light.light_energy - visibility) < 0.0002, "Actual native sky/close lighting differs from canonical phase")
	check(view.light.basis.z.distance_to(state.star_direction) < 0.000002 and view.local_light.basis == view.light.basis and view.light.light_energy == 1.0 and view.light.light_color == state.lighting.star_color and view.near_environment.ambient_light_energy == view.planet_environment.ambient_light_energy, "Actual terrain/close-pass lighting or ambient direction split at phase")
