# Ensrick fork notes

This fork tracks SeaSparrowOG's Apache-2.0-licensed Container Distribution
Framework. It exists to carry narrowly scoped, reviewable fixes needed by the
Ensrick Skyrim modpack while they are evaluated for upstream submission.

## Vendor-only rule fix

Base: upstream `38ca39282e24ed8f7f8423f106d0a660689673c6` (CDF 3.1.0).

CDF parsed a true `conditions.onlyVendors` field but evaluated the local
`onlyVendors` variable as a no-op expression. The registered rule therefore
retained `onlyVendors == false` and could also run against ordinary containers.
The fix assigns the flag and retains the intended implication that a
vendor-only rule is allowed to touch vendor containers.

`tests/only-vendors-regression.ps1` guards both sides of the regression: the
assignment must exist and the former no-op statement must not return.

## Skyrim 1.7.104 runtime support

The upstream project pins SeaSparrowOG/CommonLibSSE at `1baab74e`, from
February 2026. That revision predates Skyrim 1.7.104 and Address Library format
5. This fork advances the existing CommonLibSSE submodule to
`d27f40968783bfd7b371e4fe17ec239c60661974` (2026-08-31), preserving the
project’s CommonLib lineage while adding the current runtime's loader support.
The local `commonlib-shared` overlay also removes its native MessageBox calls:
fatal errors remain logged and terminate cleanly without interrupting the
desktop with a modal dialog.

## Signed inventory-count safety

`TESObjectREFR::GetInventory()` can retain zero and negative signed
`countDelta` entries for depleted inventory. The upstream rules treated map
membership as proof that an item had a positive count. In a replacement with a
leveled-list target, `-15` and `-16` consequently crossed an unsigned boundary
as `4294967281` and `4294967280`, then became requests for 32,767 items at the
engine's signed 16-bit leveled-list API.

This fork now rejects nonpositive inventory counts before removal,
replacement, or random-add iteration. Keyword rules retain signed per-entry
counts, ignore nonpositive entries, and use saturating aggregation rather than
unsigned wrap. Genuine positive leveled-list requests preserve the existing
behavior: values through 32,767 are exact and larger values clamp to 32,767.
Direct positive additions are split at the engine's signed 32-bit boundary so
their total is preserved.

Runtime diagnostics include the JSON path, friendly rule name, container and
base FormIDs, and source/target FormIDs where available. The standalone
`count-safety` CTest covers `-16`, `-15`, `0`, `1`, `32767`, `32768`, aggregate
overflow, and the owned C.O.I.N. `DE5012 -> DE5016` regression seam. See
[Ensrick/skyrim-mod-assistant#230](https://github.com/Ensrick/skyrim-mod-assistant/issues/230).
