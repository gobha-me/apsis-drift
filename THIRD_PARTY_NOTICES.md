# Third-Party Notices

## RtAudio

RtAudio 6.0.1 is used by the optional audio-device backend.

Copyright (c) 2001-2023 Gary P. Scavone

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

Any person wishing to distribute modifications to the Software is asked to
send the modifications to the original developer so that they can be
incorporated into the canonical version.  This is, however, not a binding
provision of this license.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## TinySoundFont

TinySoundFont renders the production First Light score and the default-off
offline MIDI evidence targets. It is pinned at commit
`853a0a171759f1ddba0de1442133a75912bbeffa`.

Copyright (C) 2017-2023 Bernhard Schelling
(based on SFZero, Copyright (C) 2012 Steve Folta)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Controlled flight hypot (glibc 2.41)

`src/flight_numerics.cpp` contains an adaptation of the non-FMA binary64 hypot
kernel from [glibc 2.41 e_hypot.c](https://github.com/bminor/glibc/blob/glibc-2.41/sysdeps/ieee754/dbl-64/e_hypot.c).
Copyright (C) 2021-2025 Free Software Foundation, Inc.
Licensed under the GNU Lesser General Public License, version 2.1 or (at your
option) any later version. This file retains that license; the repository's BSD
license does not replace it. No warranty is provided.

The unmodified upstream source is retained as
`third_party/glibc/e_hypot.c.txt`. The adaptation fixes the non-FMA branch,
replaces glibc internals with C++ standard operations, supplies namespaced
profile dispatch and defines application NaN/Inf results without an errno or
exception-flag guarantee. Upstream credits Carlos F. Borges,
[An Improved Algorithm for hypot(a,b)](https://arxiv.org/abs/1904.09481).

Complete texts: [LGPL 2.1](third_party/glibc/COPYING.LIB) and
[GPL 2](third_party/glibc/COPYING), referenced by the LGPL. Preserve these
notices and the adapted source when distributing it. Binary distributors must
satisfy LGPL section 6, including allowing modification/reverse engineering for
debugging such modifications. For this static-link build, accompany the binary
with the complete corresponding library source and application object files,
link commands and dependencies needed to relink against a modified
`flight_numerics.cpp.o` (or another compliant section-6 distribution mechanism).
The source checkout alone is not a substitute for the relinkable application
objects when distributing a statically linked executable.
