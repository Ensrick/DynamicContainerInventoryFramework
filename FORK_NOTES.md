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
