# Third-party code

## vendor/qoi.h — the reference QOI implementation

- **Upstream:** https://github.com/phoboslab/qoi (`qoi.h`, the single-file reference)
- **Author:** Dominic Szablewski
- **License:** MIT
- **sha256:** `7de6fca1a285b1c20d38f2723dec8b774eb9f144edb9710800a95feeea09375a`
- **Modifications:** none. Included verbatim as the decoder we harden against and differentially
  compare to. `src/original.c` is a thin wrapper that exposes its `qoi_decode` under a stable name;
  it adds no logic.

```
Copyright (c) 2021 Dominic Szablewski

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Referenced (not vendored)

Named in the README as the sibling "roads" to a safe QOI decoder; no code is included from either:

- `Fabien-Chouteau/qoi-spark` — Ada/SPARK QOI, proved free of run-time errors.
- `aldanor/qoi-rust` — pure safe Rust QOI.
