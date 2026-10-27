# glibc hypot source provenance

Upstream: GNU C Library, release tag `glibc-2.41`.
Source: https://github.com/bminor/glibc/blob/glibc-2.41/sysdeps/ieee754/dbl-64/e_hypot.c

`e_hypot.c.txt` is the unmodified source, retained under its upstream
LGPL-2.1-or-later license. `COPYING.LIB` and `COPYING` are the release's
unmodified license texts. The compiled C++ adaptation is
`../../src/flight_numerics.cpp`; see its change description and
`../../THIRD_PARTY_NOTICES.md`, including binary distribution requirements.

No glibc private headers, dynamic loader shim or prebuilt library is shipped.

SHA-256 of retained upstream files:

```text
c4f4148619e1f4df04cec589e69eff2a369b3f664e147411f445fd13ce527918  e_hypot.c.txt
dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551  COPYING.LIB
8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643  COPYING
```
