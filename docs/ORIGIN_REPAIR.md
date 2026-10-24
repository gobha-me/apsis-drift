# Repair at Origin

With experimental terrain impacts selected, a surviving Wayfarer can return to
Origin for repair. Open paused **Controls → Origin craft repair** while seated
and attached, or while walking in the station. Review the quote and confirm.
Both damaged regions are repaired together, immediately. Reading the quote or
backing out changes nothing. A remote craft, boarding transition, pending loss
or catastrophic impact cannot use the service.

Technical journeys receive free service. Existing paid-operation enrollment
uses a provisional tariff of **10 credits per severity unit**, at most **60
credits** for the two regions. The quote includes due berth charges and cash
remaining after repair. Repair requires cash after berth charges; operating
credit does not finance it. An unaffordable or stale confirmation changes
neither damage nor the account. Service does not enroll a technical journey.

Repair retains the individual ship, impact history, contact gate, fuel, cargo
and jump-drive state. It clears current forebody and landing-structure severity;
the cockpit cue then identifies the previous impact as repaired history. A new
impact can damage the same ship again. Lost ships still use explicit standard
recovery, which preserves their retired history and supplies a new ship.

## Ownership and persistence

C++ owns eligibility, quotes, cash debit and condition. Confirmation rechecks the
reviewed token, stages the complete transaction, then validates and encodes the
whole save before commit. Godot presents the quote and requests confirmation.
There is no chart, mission or upgrade requirement.

Unrepaired condition keeps its original version-one encoding. A repaired craft
uses condition version two with one bounded repair receipt: the actual Origin
attachment and service-pose checksum, pre-service damage/contact state, source
projection digest and payment anchors. The receipt preserves the existing
impact serial rather than inventing a contact. Later impacts and further
repairs retain the same ownership and event rules. The format-34 envelope,
consequence selection and older nested save formats remain unchanged.

Paid receipts bind to their source account's enrollment tick and post-payment
spending watermark. Retired receipts validate against the retired source
knowledge, rather than a replacement ship's account. These consistency checks
are not authentication against deliberately rewritten save histories.

## Scope and validation

[#707](https://github.com/gobha-me/apsis-drift/issues/707) is engineering
follow-through to the existing condition and tiered-service designs. The tariff
and immediate service are provisional gameplay rules, not a selected final
economy balance. Workers, materials, timed shipyard jobs, field repair and
cosmetic refurbishment remain separate work.

The existing impact scenario supplies genuine swept-contact damage for native
repair checks. Its subsequent Origin attachment and aggregate paid-account
boundaries are explicitly initialized; they do not demonstrate a flown damaged
return, an earned financial history or a profitable route. Checks cover cash
and free service, refusal without mutation, Save/Continue and retained loss
history, alongside the ordinary keyboard, mouse and selected-controller modal
paths. Automated headless checks do not establish hardware feel or rendered
appearance.
