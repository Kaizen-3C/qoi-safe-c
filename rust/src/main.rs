//! Safe-Rust QOI decoder.
//!
//! A port of the reference QOI decoder (phoboslab/qoi, MIT) to safe Rust:
//! `#![forbid(unsafe_code)]`, no FFI, all indexing bounds-checked by the
//! language. It is a behaviour-preserving port -- byte-for-byte identical
//! output to the reference on every input (that is what `make check` proves
//! against the same valid + hostile corpus as the safe-C decoder).
//!
//! We claim only equivalence + memory safety, both re-provable. We do NOT
//! claim performance or idiomatic superiority; for a fast, idiomatic
//! hand-written Rust QOI codec see `aldanor/qoi-rust`.
//!
//! Used as a subprocess by the differential harness:
//!   qoi_rs <file> <channels(0|3|4)>
//! exit 0 + stdout = [u32 w LE][u32 h LE][u8 channels][u8 colorspace][pixels]
//! exit 2         = invalid input (reference would return NULL)
//! exit 3         = usage / IO error

#![forbid(unsafe_code)]

use std::io::Write;

const MAGIC: u32 = ((b'q' as u32) << 24) | ((b'o' as u32) << 16)
                 | ((b'i' as u32) << 8) | (b'f' as u32);
const HEADER: usize = 14;
const PADDING: usize = 8;
const PIXELS_MAX: u32 = 400_000_000;

struct Desc {
    width: u32,
    height: u32,
    channels: u8,
    colorspace: u8,
}

/// Bounds-checked reader: a read past the end returns 0 and clears `ok`
/// (mirrors the safe-C H1 cursor). Within a truthfully-sized buffer it
/// returns exactly the bytes the reference reads.
struct Reader<'a> {
    b: &'a [u8],
    p: usize,
    ok: bool,
}

impl<'a> Reader<'a> {
    fn u8(&mut self) -> u32 {
        match self.b.get(self.p) {
            Some(&v) => { self.p += 1; v as u32 }
            None => { self.ok = false; 0 }
        }
    }
    fn u32(&mut self) -> u32 {
        let a = self.u8();
        let b = self.u8();
        let c = self.u8();
        let d = self.u8();
        (a << 24) | (b << 16) | (c << 8) | d
    }
}

#[derive(Clone, Copy, Default)]
struct Rgba {
    r: u8,
    g: u8,
    b: u8,
    a: u8,
}

fn decode(data: &[u8], channels: i32) -> Option<(Desc, Vec<u8>)> {
    if (channels != 0 && channels != 3 && channels != 4) || data.len() < HEADER + PADDING {
        return None;
    }

    let mut r = Reader { b: data, p: 0, ok: true };
    let magic = r.u32();
    let width = r.u32();
    let height = r.u32();
    let hchan = r.u8() as u8;
    let hcspc = r.u8() as u8;
    if !r.ok {
        return None; // unreachable given the size guard; explicit anyway
    }

    // Reject set reproduced verbatim from the reference (incl. the div-form cap).
    if width == 0 || height == 0
        || hchan < 3 || hchan > 4
        || hcspc > 1
        || magic != MAGIC
        || height >= PIXELS_MAX / width
    {
        return None;
    }

    let channels: usize = if channels == 0 { hchan as usize } else { channels as usize };

    // Width-independent pixel count + explicit alloc-size guard (the cap above
    // already bounds width*height < 400M, so neither can fire here).
    let px_count = width as u64 * height as u64;
    if px_count > (i32::MAX as u64) / channels as u64 {
        return None;
    }
    let px_len = px_count as usize * channels;

    let mut pixels = vec![0u8; px_len];
    let mut index = [Rgba::default(); 64];
    let mut px = Rgba { r: 0, g: 0, b: 0, a: 255 };

    let chunks_len = data.len() - PADDING;
    let mut run: i32 = 0;
    let mut px_pos = 0usize;

    while px_pos < px_len {
        if run > 0 {
            run -= 1;
        } else if r.p < chunks_len {
            let b1 = r.u8() as i32;

            if b1 == 0xfe {
                // QOI_OP_RGB
                px.r = r.u8() as u8;
                px.g = r.u8() as u8;
                px.b = r.u8() as u8;
            } else if b1 == 0xff {
                // QOI_OP_RGBA
                px.r = r.u8() as u8;
                px.g = r.u8() as u8;
                px.b = r.u8() as u8;
                px.a = r.u8() as u8;
            } else if (b1 & 0xc0) == 0x00 {
                // QOI_OP_INDEX
                px = index[(b1 & 63) as usize];
            } else if (b1 & 0xc0) == 0x40 {
                // QOI_OP_DIFF
                px.r = px.r.wrapping_add((((b1 >> 4) & 0x03) - 2) as u8);
                px.g = px.g.wrapping_add((((b1 >> 2) & 0x03) - 2) as u8);
                px.b = px.b.wrapping_add(((b1 & 0x03) - 2) as u8);
            } else if (b1 & 0xc0) == 0x80 {
                // QOI_OP_LUMA
                let b2 = r.u8() as i32;
                let vg = (b1 & 0x3f) - 32;
                px.r = px.r.wrapping_add((vg - 8 + ((b2 >> 4) & 0x0f)) as u8);
                px.g = px.g.wrapping_add(vg as u8);
                px.b = px.b.wrapping_add((vg - 8 + (b2 & 0x0f)) as u8);
            } else if (b1 & 0xc0) == 0xc0 {
                // QOI_OP_RUN
                run = b1 & 0x3f;
            }

            let h = (px.r as i32 * 3 + px.g as i32 * 5 + px.b as i32 * 7 + px.a as i32 * 11) & 63;
            index[h as usize] = px;
        }

        pixels[px_pos] = px.r;
        pixels[px_pos + 1] = px.g;
        pixels[px_pos + 2] = px.b;
        if channels == 4 {
            pixels[px_pos + 3] = px.a;
        }
        px_pos += channels;
    }

    Some((
        Desc { width, height, channels: hchan, colorspace: hcspc },
        pixels,
    ))
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        eprintln!("usage: qoi_rs <file> <channels 0|3|4>");
        std::process::exit(3);
    }
    let channels: i32 = args[2].parse().unwrap_or(-1);
    let data = match std::fs::read(&args[1]) {
        Ok(d) => d,
        Err(_) => std::process::exit(3),
    };

    match decode(&data, channels) {
        None => std::process::exit(2),
        Some((d, px)) => {
            let out = std::io::stdout();
            let mut o = out.lock();
            let mut preamble = Vec::with_capacity(10);
            preamble.extend_from_slice(&d.width.to_le_bytes());
            preamble.extend_from_slice(&d.height.to_le_bytes());
            preamble.push(d.channels);
            preamble.push(d.colorspace);
            if o.write_all(&preamble).is_err() || o.write_all(&px).is_err() {
                std::process::exit(3);
            }
        }
    }
}
