# qoi-safe-c

**Three roads to a memory-safe QOI decoder — and what each costs.** This repo walks two of them, from
the same C source, and proves both match the reference — byte for byte — with one command.

[QOI](https://qoiformat.org/) is a small, fast, lossless image format. Because it's tiny and its spec
is stable, it has become a natural yardstick for *how* you make a decoder of untrusted input
memory-safe. There are (at least) three strategies:

| Strategy | Public exemplar | The guarantee | What it costs |
|---|---|---|---|
| **Rewrite to Ada + prove it** | [`qoi-spark`](https://github.com/Fabien-Chouteau/qoi-spark) | SPARK proof of **absence of run-time errors** | new language (Ada) + a formal-methods toolchain |
| **Rewrite to safe Rust** | [`qoi-rust`](https://github.com/aldanor/qoi-rust) | memory safety from the type system | new language + ecosystem |
| **Stay in C, harden in place** | *this repo* | spatial memory safety, behaviour-identical to the reference | no rewrite — same language, team, build |

This repo implements **two** of these roads itself — a safe-Rust port (`rust/`) *and* a hardened-C
decoder (`src/qoi_safe.c`) — and `make check` proves **both** are byte-for-byte identical to the
reference C decoder over the same valid + adversarial corpus:

```
safe-C : PASS -- 22/22 files, 66 decode-cases, byte-identical to reference, sanitizer-clean
Rust   : PASS -- 22/22 files, 66 decode-cases, byte-identical to reference
```

## What we claim — and what we don't

> **We claim only two things, and both are re-provable by `make check`:**
> 1. **Equivalence** — our Rust and safe-C decoders produce byte-identical output to the reference C
>    decoder on every input in the corpus (valid and hostile), and return on exactly the same inputs.
> 2. **Memory safety** — no out-of-bounds access: the C decoders are clean under AddressSanitizer +
>    UndefinedBehaviorSanitizer here; the Rust decoder is `#![forbid(unsafe_code)]`.
>
> **We do not claim performance or idiomatic superiority.** For a fast, idiomatic, hand-written Rust
> QOI codec, use [`qoi-rust`](https://github.com/aldanor/qoi-rust). Our Rust port exists to show the
> *same source, shown byte-identical to the reference over the corpus, in a second target language* —
> not to win a benchmark.

This is the point: the value isn't "we wrote a decoder," it's a **re-runnable proof that the decoder is
equivalent and safe** — the same proof, whichever target language you migrate to.

## An honest starting point

The reference decoder ([`phoboslab/qoi`](https://github.com/phoboslab/qoi), the single-file `qoi.h`) is
**already memory-safe** on adversarial input. We checked before writing a line: 3.16M structure-aware
executions plus a raw-file pass under AddressSanitizer, zero crashes. Its safety rests on three
*global, implicit* invariants — 8 bytes of read padding, a 400-million-pixel cap that interacts with
the platform `int` width to prevent an overflow, and up-front header validation.

So this is **not** a "we found a scary bug" project. What our decoders add is narrower and, we think,
more useful:

- **Local auditability (the safe-C decoder).** The reference is safe only if you hold all three global
  invariants in your head at once. `src/qoi_safe.c` makes every bound **explicit at the point of use** —
  a bounds-checked read cursor (so an over-read can't dereference out of range even if a caller lies
  about the size or drops the padding), an explicit width-independent overflow guard, an asserted write
  bound. You verify safety line by line, not by a whole-file argument.
- **A second migration target (the Rust decoder), shown byte-identical over the same corpus.** The same
  logic in `#![forbid(unsafe_code)]` Rust, held to the same byte-identity contract.
- **A re-runnable proof.** `make check` rebuilds the C decoders under ASan **and** UBSan, regenerates a
  valid + hostile corpus, and asserts both decoders are byte-for-byte identical to the reference.

Scope is **spatial** safety (no out-of-bounds access). The decoders own a single allocation and return
it; temporal safety (use-after-free of that buffer) is the caller's and is not claimed here. Decoder
only — the decoder is the untrusted-input surface where memory safety is the whole game.

## Reproduce

```sh
make check          # build the C decoders under ASan/UBSan + the Rust decoder; diff both vs the reference
make bench          # decode throughput: reference C vs safe-C vs Rust (see Performance)
make fuzz T=180     # structure-aware libFuzzer soak on the safe-C decoder (needs clang)
```

`make check` needs a C compiler (clang or gcc), `cargo`, and ASan/UBSan. It prints one `PASS` line per
track, or the first divergence and a non-zero exit. Those two lines are the whole claim — and you can
re-run them.

### What the corpus covers

`corpus/valid/` is roundtrip-encoded real QOI (gradient, RLE-heavy solid, index-reuse, 1×1). The
adversarial `corpus/hostile/` is generated deterministically by `test/gen_corpus.c`:

truncated header · truncated chunk stream · missing end-padding · width×height overflow · zero
dimensions · bad channel counts (0/1/2/5/255) · bad colorspace · bad magic · max-count run on a
1-pixel image · RGBA/LUMA opcodes placed at the last byte before the padding (maximal read overshoot) ·
one of every opcode.

## Performance

Faithful translation first: the decoders match the reference's *output*; matching its *speed* is a
separate axis, and beating it (hand-tuning, idiomatic rewrites) is a deliberate optimization phase, not
part of the safety-preserving translation. `make bench` measures decode throughput (`-O2`/release, no
sanitizers) on a 1024×1024 near-incompressible image — a worst case where almost every pixel is a full
chunk:

| decoder | throughput | vs reference |
|---|---|---|
| reference C | ~440 Mpx/s | baseline |
| safe-C (every access bounds-checked) | ~350 Mpx/s | ~79% |
| Rust (`#![forbid(unsafe_code)]`) | ~340 Mpx/s | ~76% |

(Absolute numbers are machine-dependent — run `make bench` on your own box.) The ~20% is the honest cost
of local bounds-checking without a rewrite. For a hand-tuned Rust QOI codec that *beats* the reference C,
see [`qoi-rust`](https://github.com/aldanor/qoi-rust) — that's the optimization road; ours is the
faithful-and-safe one.

## Layout

```
vendor/qoi.h        reference decoder (phoboslab/qoi, MIT), unmodified — see THIRD_PARTY.md
src/original.c      thin wrapper exposing the reference decoder for the harness
src/qoi_safe.[ch]   the hardened-C decoder
rust/               the safe-Rust decoder (#![forbid(unsafe_code)]), driven as a subprocess
test/differential.c reference vs safe-C vs Rust, byte-identical over the corpus (ASan/UBSan)
test/fuzz_safe.c    structure-aware libFuzzer target on the safe-C decoder
test/bench.c        decode-throughput benchmark (reference C vs safe-C; Rust via qoi_rs --bench)
test/gen_corpus.c   deterministic valid + hostile corpus generator
```

## The safe-C hardening, specifically

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
  decoder returns `NULL` on precisely the same inputs. The Rust port applies the same four.

---

*Maintained by Kaizen-3C. We do memory-safety hardening of C and C-to-Rust migration commercially —
case studies and contact at [kaizen-3c](https://github.com/kaizen-3c).*
