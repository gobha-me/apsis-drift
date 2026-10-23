# Optional paid operations

In a planetary-chart journey, attach at Origin, pause, open **Survey
commissions**, then **Paid operations**. Enrollment is explicit and stays
selected in that saved journey. Existing journeys retain free technical fuel
service until enrollment. Loading an older save rewinds the whole voyage.

These are provisional engineering prices, not a balanced economy:

| Item | Prototype terms |
| --- | --- |
| Opening budget | 200 credits once, including 20 for four prepaid berth hours |
| Flight fuel | Missing fraction of a 40-credit full tank, rounded up |
| Jump fuel | 15 credits per missing charge |
| Berth | One lodging-and-parking bill, 5 credits per game hour |
| Survey report | 100 credits for a fresh completed commission |
| Replacement | 100-credit insurance excess; unpaid excess capped at 100 |

The page shows actual missing quantities and the full restoration price before
confirmation. Purchase debits cash and restores both stores together. A stale
quote or insufficient cash changes neither. Menus and offline time do not
advance game time. The first new berth period becomes due exactly four game
hours after enrollment, with subsequent periods due hourly. Reading or saving
never posts another bill; every transaction uses the same elapsed-time bill.
Prior settled survey earnings carry into the opening cash balance.

A poor pilot at Origin can accept fresh survey work and review a **noncash
operating retainer** before acquiring any measurement. It restores the starter
stores and prepays berth through the accepted deadline, once per commission.
Cash pays first, then an operating tab capped at 200 credits. Necessary support
above that cap is explicitly shown as hardship subsidy. Support supplies no
cash or resale inventory. A full starter has three jump charges; an ordinary
outward-and-home trip consumes two. A living pilot stranded elsewhere still
needs actual help.

A completed report survives ship loss. Actual replacement charges the excess
once: available cash pays first, with unpaid excess retained separately from
the operating tab. Report income pays outstanding excess first, then uses up to
half the remainder to repay the operating tab. A retained 100-credit report
with 100 excess outstanding produces no new cash. Repeated loss cannot build
unlimited debt, and the sole starter is never seized. Losses before enrollment
have no retroactive fee. Continue grants no new opening budget or replacement.

C++ owns quantities, time, quotes, billing, income and replacement settlement.
The absent-by-default version1 `paid_operations` record contains fixed-purpose
counters in the existing knowledge envelope. Cash derives from the opening
budget plus settled survey gross minus cumulative expenditure; there is no
second stored wallet or invoice history. Godot displays these terms and sends
explicit, bounded quote tokens. World generation and physical flight stay
unchanged.

The earlier measured reference voyage would cost 39 credits under these rates,
but it was not a fulfilled commission. Automated scenarios use disclosed
initialized space/orbit/approach poses and financial boundary fixtures. They do
not establish ordinary commission profitability, sparse-start viability or
player pacing. Those remain part of
[#293](https://github.com/gobha-me/apsis-drift/issues/293).
