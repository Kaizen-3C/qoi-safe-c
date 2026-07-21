# qoi-safe-c

**Three roads to a memory-safe QOI decoder — and what each costs.**

[QOI](https://qoiformat.org/) is a small, fast, lossless image format. Because it's tiny and its spec
is stable, it has become a natural yardstick for *how* you make a decoder of untrusted input
memory-safe. There are (at least) three roads:

| Road | Project | The guarantee | What it costs |
|---|---|---|---|
| **Rewrite to Ada + prove it** | [`qoi-spark`](https://github.com/Fabien-Chouteau/qoi-spark) | SPARK proof of **absence of run-time errors** (no overflow, no out-of-bounds) | new language (Ada) + a formal-methods toolchain |
| **Rewrite to safe Rust** | [`qoi-rust`](https://github.com/aldanor/qoi-rust) | memory safety from the type system (`#![forbid(unsafe_code)]`) | new language (Rust) + ecosystem |
| **Stay in C, harden in place** | **this repo** | spatial memory safety (no out-of-bounds), **behaviour-identical to the reference**, checked by a re-runnable differential + adversarial-ASan harness | no rewrite — same language, same team, same build |

All three arrive at the same place — a decoder that won't be walked out of bounds by a malicious file.
They differ in the **cost of the guarantee**. This repo is the only road that never leaves C.

## An honest starting point

The reference decoder ([`phoboslab/qoi`](https://github.com/phoboslab/qoi), the single-file `qoi.h`) is
**already memory-safe** on adversarial input. We checked before writing a line: 3.16M structure-aware
executions plus a raw-file pass under AddressSanitizer, zero crashes. Its safety rests on three
*global, implicit* invariants — 8 bytes of read padding, a 400-million-pixel cap that interacts with
the platform `int` width to prevent an overflow, and up-front header validation.

So this is **not** a "we found a scary bug" project. It would be dishonest to pretend otherwise. What
`src/qoi_safe.c` adds is narrower and, we think, more useful:

- **Local auditability.** The reference is safe only if you hold all three global invariants in your
  head at once. `qoi_safe.c` makes every bound **explicit at the point of use** — a bounds-checked read
  cursor (so an over-read cannot dereference out of range even if a caller lies about the size or drops
  the padding), an explicit width-independent overflow guard, an asserted write bound. You verify safety
  line by line, not by a whole-file argument.
- **A re-runnable proof.** `make check` rebuilds both decoders under ASan **and** UBSan, regenerates a
  valid + hostile corpus, and asserts the safe decoder is **byte-for-byte identical** to the reference
  on every input. Behaviour-preserving *and* sanitizer-clean, on demand.

Scope is **spatial** safety (no out-of-bounds access). The decoder owns a single allocation and returns
it; temporal safety (use-after-free of that buffer) is the caller's and is not claimed here. Decoder
only — the decoder is the untrusted-input surface where memory safety is the whole game.

## Reproduce

```sh
make check          # build under ASan/UBSan, diff safe vs reference over the whole corpus
make fuzz T=180     # structure-aware libFuzzer soak on the safe decoder (needs clang)
```

`make check` prints `PASS n/n files: safe == reference (byte-identical), sanitizer-clean`, or the first
divergence and a non-zero exit. That line is the whole claim — and you can re-run it.

### What the corpus covers

`corpus/valid/` is roundtrip-encoded real QOI (gradient, RLE-heavy solid, index-reuse, 1×1). The
adversarial `corpus/hostile/` is generated deterministically by `test/gen_corpus.c`:

truncated header · truncated chunk stream · missing end-padding · width×height overflow · zero
dimensions · bad channel counts (0/1/2/5/255) · bad colorspace · bad magic · max-count run on a
1-pixel image · RGBA/LUMA opcodes placed at the last byte before the padding (maximal read overshoot) ·
one of every opcode.

## Layout

```
vendor/qoi.h        reference decoder (phoboslab/qoi, MIT), unmodified — see THIRD_PARTY.md
src/original.c      thin wrapper exposing the reference decoder for the harness
src/qoi_safe.[ch]   the hardened decoder — the substance
test/differential.c reference vs safe, byte-identical over the corpus (ASan/UBSan)
test/fuzz_safe.c    structure-aware libFuzzer target on the safe decoder
test/gen_corpus.c   deterministic valid + hostile corpus generator
```

## The hardening, specifically

Four changes, each replacing a global implicit invariant with a local explicit check, none of which
alters observable behaviour (that's what `make check` proves):

- **H1 — bounds-checked read cursor.** Every byte read goes through a reader that returns 0 and flags
  `ok = 0` past the end instead of dereferencing. Over-read becomes structurally impossible, no longer
  dependent on the reference's 8-byte read padding.
- **H2 — explicit overflow guard.** The pixel count is computed in a width-independent wide type with an
  explicit allocation-size guard, rather than relying on the 400M cap interacting with the range of
  `int`.
- **H3 — asserted write bound.** Every store into the output buffer is guarded by an assertion of its
  bound.
- **H4 — header validation verbatim.** The reference's reject set is reproduced exactly, so the safe
  decoder returns `NULL` on precisely the same inputs.

---

*Maintained by Kaizen-3C. We do memory-safety hardening of C and C-to-Rust migration commercially —
case studies and contact at [kaizen-3c](https://github.com/kaizen-3c).*
