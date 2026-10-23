# Origin specimen buyer

In a planetary-chart or four-charted-plus-two-uncharted journey, carry physical
geological specimens back to Origin and attach to a port. Pause, open
**Survey commissions**, then **Sell carried specimens**. Review the contents
and quote before confirming. Station walking and seated flight use the same
buyer and account.

The provisional price is **40 credits per specimen**, with the existing
four-slot pouch sold as one batch. There is no assay or rarity price. The quote
shows gross proceeds and, in paid operation, insurance and operating-tab
repayment plus net proceeds and the resulting cash. Prices are engineering
starting values; they are not evidence that an ordinary flight is profitable.

A sale removes only the carried specimens. First-collection notes, inspections,
discoveries and any site marker remain. Notes alone cannot be sold. The existing
collection rules allow a newly collected specimen from a previously noted site
to be sold again. Discarding, losing specimens, docking, reading and Continue
produce no sale. An empty pouch, remote location or stale quote changes nothing.

Before paid enrollment, sale earnings are retained. Explicit later enrollment
carries both earlier survey and specimen income into the opening cash balance.
In [paid operation](PAID_OPERATIONS.md), old bills are reconciled first, then
income settles outstanding insurance excess and uses up to half the remainder
to repay the operating tab. For example, four specimens yield160 gross;100
insurance excess and a sufficiently large tab deduct100 and30, leaving30.
Adding this income source grants no second opening budget and changes no fee.

Standard replacement retains completed sales, earnings, receipts and field
notes. It still loses carried specimens; a replacement never sells the retired
pouch. A successful new sale after replacement needs newly collected physical
specimens. Loading an older save rewinds the entire voyage and its accounts.

C++ owns Origin admission, pouch provenance, quotes, counters and atomic
removal/payment. Godot displays the quote and sends an explicit bounded token.
The absent-by-default sales-v1 record retains one bounded cumulative count and
one latest receipt containing up to four sold records. Gross specimen income is
count times the fixed policy price, without a second wallet or invoice history.

Paid-v1 retains its exact nine-field, survey-only meaning. An actual first sale
in an enrolled journey, or explicit enrollment with earlier sales, selects
paid-v2 and its additional settled-specimen counter. Existing account values
carry over; opening, polling, Save and Continue never promote a policy.
World generation, seeds, collection capacity, movement and resources retain
their meanings. Counter exhaustion refuses a sale while preserving specimens.

Qualification covers invalid schemas, provenance, bounds, stale/duplicate
transactions, notes, prior earnings, mixed income, ordinary paid services and
recovery. Native fixtures use actual inspect/collect and service commands,
with explicitly initialized contact, return, account and full-pouch boundaries.
They do not prove uninterrupted flown profitability, procedural-start viability
or manual/controller/GPU acceptance. Those remain separate, including
[#293](https://github.com/gobha-me/apsis-drift/issues/293).
