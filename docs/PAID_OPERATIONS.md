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
Prior settled survey and [specimen-sale](SPECIMEN_SALES.md) earnings carry into
the opening cash balance.

A poor pilot at Origin can accept fresh orbital survey work and review a **noncash
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
The absent-by-default `paid_operations` record contains fixed-purpose counters
in the existing knowledge envelope. Survey-only version1 stays byte-exact; an
explicit specimen sale or enrollment with earlier sales selects version2 and
its additional settled-specimen counter, carrying every existing account value. Cash derives from the opening
budget plus settled income minus cumulative expenditure; there is no
second stored wallet or invoice history. Godot displays these terms and sends
explicit, bounded quote tokens. World generation and physical flight stay
unchanged.

The earlier measured reference voyage would cost 39 credits under these rates,
but it was not a fulfilled commission. Automated scenarios use disclosed
initialized space/orbit/approach poses and financial boundary fixtures. They do
not establish ordinary commission profitability, sparse-start viability or
player pacing. A separate [ordinary paid-survey reference](FREEDOM_ENDURANCE.md#ordinary-paid-survey-reference)
now measures one fully flown seed 42 trip: cash of 180 after enrollment becomes
237 after actual payment and complete fuel restoration, over 103 game minutes.
This single candidate does not settle pricing, failed work, sparse starts or
player pacing; those remain part of
[#293](https://github.com/gobha-me/apsis-drift/issues/293).

The optional [closer-arrival reference](CHARTED_ARRIVALS.md#measured-paid-survey-reference)
finishes that candidate with 244 credits after replenishment. A separate
[incomplete-survey reference](research/incomplete-paid-survey-reference-v1.json)
skips orbital acquisition and returns without a payable report. That actual
trip takes 12.585 game minutes and costs 32 credits to restore both fuel pools,
leaving 148 credits, full stores and a newly accepted job with no reused work.
Submission refuses without changing the Save; explicit abandonment grants no
payment. This measures one unexpired unsuccessful attempt, not deadline expiry
or general starting viability. Previous reference data remains unchanged.

## Optional recovered-canister income

The separately selected [derelict recovery prototype](DERELICT_RECOVERY.md)
adds one finite canister sale at Origin for a provisional 100 credits. Actual
sale or explicit enrollment with earlier canister income selects paid-v3;
reading, selection and Continue do not. Paid-v3 adds its settled canister counter
and retains the specimen counter, including zero. Earlier v1/v2 accounts keep
their original shapes and meanings. Existing insurance-first/tab settlement,
opening credit, fuel/berth prices and recovery excess are unchanged.

## Repeated classified-loss reference

The [paid-recovery reference](research/paid-recovery-reference-v1.json) starts
from the public seed-42 planetary New Game, accepts unpaid orbital work and
enrolls once. Three real walking, boarding, release and short fuel-consuming
flight cycles each end with an authenticated **classified loss**, followed by
actual replacement. No poses, resources or clocks are assigned between actions.
The event is an application fixture, not a simulated physical collision.

Cash changes from 180 to **80, 0, 0**; outstanding excess becomes **0, 20, 100**.
Each replacement retains one current serviceable ship, full starter stores and
zero earnings. A repeated replacement command is Save-neutral; actual
Save/Continue and fresh unpaid acceptance remain available. The later
five-credit berth-only retainer is quoted, never funded. All three cycles take
68.85 game seconds, inside the initial four prepaid hours.

Use your configured native build directory in place of `build-native`:

```sh
cmake --build build-native --target apsis-drift-native-orbital-survey-tests
build-native/apsis-drift-native-orbital-survey-tests --fixtures "$PWD/build-recovery-reference"
```

The existing exporter retains nineteen complete Saves and a primitive action
transcript under `paid-recovery-reference/`. Default CTest runs the same checks
with an owned temporary directory. GCC and Clang exports matched byte-for-byte
on the recorded numerical profile, including unchanged earlier fixture files.
Recorded SHA256 values attest that observation; they do not replace the
cross-environment numerical contract tracked in #255.

This checks bounded debt, replacement ownership and the next unpaid command.
It does not establish cargo survival, a completed earning voyage, full rent
exposure, sparse starts, indefinite failures or price balance. The remaining
[#293](https://github.com/gobha-me/apsis-drift/issues/293) criteria stay open.
