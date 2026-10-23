# Closer charted arrivals

In a planetary-chart or discoverable-region journey, pause seated flight, open
**Flight instruments & navigation**, expand the chart and select **Closer
charted arrivals**. The choice stays with the journey when saved. Changing it
advances no time and spends no fuel or credits.

The option applies when the destination system's primary planet has recorded
identity and resolved location evidence. A newly found star can have a known
system location while its planets remain unknown; that trip keeps the wide
arrival. Origin keeps its station approach. The chart explains which placement
the selected route will use.

Arrival retains the existing velocity, attitude and spin rules. The pilot still
establishes an orbit or flies a descent. The charge, rated range, spool, transit,
arrival uncertainty and optional recovery/strain rules stay the same. The
choice cannot change during spooling, transit or pending recovery.

## Placement and persistence

C++ selects the larger of 1.5 planet radii from the centre or a conservative
target shell. The shell includes the atmospheric-flight space boundary or the
supported terrain ceiling, whichever is higher, plus the registered hull
radius, 100 km margin and a 202 km allowance for maximum rated scatter and
recovery. Exact distance, alignment and clearance are then assessed normally.
The allowance is a bound, not another random displacement.

This bounds clearance from the target planet's atmosphere and terrain while
retaining the system provider's existing star and primary-planet checks.
Secondary bodies and other environmental limits remain as documented in
[jump targeting](FREEDOM_JUMP_TARGETING.md). Closer placement is not a guarantee
of a stable orbit or a successful landing.

The absent-by-default personal `charted_arrivals` record is outside sensor
facts. Its version 1 selection permits a system request of version 4, carrying
its own arrival policy and optional existing recovery state. Earlier request
versions and save meanings remain unchanged. Disabling today's preference
does not erase or reinterpret a previous closer arrival.

Historical receipts validate retained navigation evidence dated before their
commitment. Later observations remain valid; the ordinary knowledge query is
not a historical query. Standard replacement retains this personal choice.

The [wide-arrival paid reference](FREEDOM_ENDURANCE.md#ordinary-paid-survey-reference)
is retained separately. The opt-in voyage fixture accepts
`--neighbor --commands --commission --charted-arrivals` to measure the selected
approach through real commands. This remains one candidate route, separate from
manual handling, general price balance or procedural-start viability.

## Measured paid survey reference

The [charted-arrival reference](research/charted-paid-survey-reference-v1.json)
completed the seed 42 paid survey and returned to Origin through public
commands, including physical capture, report submission and replenishment.
Both compiler runs matched all 46 route artifacts and 21 Continue checkpoints.

| Recorded candidate | Game minutes | Flight and jump replenishment | Final credits |
| --- | ---: | ---: | ---: |
| Earlier wide arrival | 103.057 | 43 | 237 |
| Selected closer arrival | 28.089 | 36 | 244 |

Both begin with 200 credits, spend 20 on the initial four-hour berth, earn 100
for the report and finish with full flight stores and three jump charges.
Neither incurs another berth bill or debt. These are two recorded candidate
routes with the same seed, target and test controller. They do not establish
optimal routing, typical start viability, player pacing or general price
balance; those remain work under #293. The earlier reference is unchanged.
