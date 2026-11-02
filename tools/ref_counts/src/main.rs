//! Independent ITCH 5.0 message counts on top of the `itchy` parser (D-03, AC-W-01).
//!
//! `itchy` 0.3 has no parser for two types added to ITCH 5.0 later on: Operational Halt (`h`) and
//! Direct Listing with Capital Raise (`O`). It panics on a tag it does not know, so `FrameFilter`
//! takes those frames out of the stream and counts them from the frame header. All other messages
//! go through `itchy`, and only `itchy`'s tag decides how they are counted.

use std::collections::BTreeMap;
use std::fs::File;
use std::io::{self, Read, Write};
use std::path::PathBuf;
use std::process::ExitCode;
use std::time::Instant;

const UNSUPPORTED: [u8; 2] = [b'h', b'O'];
const READ_CHUNK: usize = 1 << 20;

/// Copies length-prefixed frames from `inner`, withholding the types `itchy` cannot parse.
struct FrameFilter<R> {
    inner: R,
    input: Vec<u8>,
    start: usize,
    /// Bytes of a frame already started, when the caller's buffer ended inside it.
    carry: usize,
    framed_only: [u64; 256],
}

impl<R: Read> FrameFilter<R> {
    fn new(inner: R) -> Self {
        Self {
            inner,
            input: Vec::with_capacity(2 * READ_CHUNK),
            start: 0,
            carry: 0,
            framed_only: [0; 256],
        }
    }

    fn refill(&mut self) -> io::Result<usize> {
        self.input.drain(..self.start);
        self.start = 0;
        let filled = self.input.len();
        self.input.resize(filled + READ_CHUNK, 0);
        let got = self.inner.read(&mut self.input[filled..])?;
        self.input.truncate(filled + got);
        Ok(got)
    }
}

impl<R: Read> Read for FrameFilter<R> {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        let mut written = 0;
        loop {
            let available = &self.input[self.start..];
            if self.carry > 0 && !available.is_empty() {
                let part = self.carry.min(available.len()).min(out.len() - written);
                if part == 0 {
                    return Ok(written);
                }
                out[written..written + part].copy_from_slice(&available[..part]);
                written += part;
                self.start += part;
                self.carry -= part;
                continue;
            }
            if self.carry == 0 && available.len() >= 2 {
                let length = u16::from_be_bytes([available[0], available[1]]) as usize;
                let total = 2 + length;
                if available.len() >= total {
                    let tag = if length > 0 { available[2] } else { 0 };
                    if UNSUPPORTED.contains(&tag) {
                        self.framed_only[tag as usize] += 1;
                        self.start += total;
                        continue;
                    }
                    self.carry = total;
                    continue;
                }
            }
            if written > 0 {
                return Ok(written);
            }
            if self.refill()? == 0 {
                // End of input. Any partial frame is passed on, so that `itchy` reports it.
                let rest = &self.input[self.start..];
                let part = rest.len().min(out.len());
                out[..part].copy_from_slice(&rest[..part]);
                self.start += part;
                return Ok(part);
            }
        }
    }
}

struct Options {
    file: PathBuf,
    json: Option<PathBuf>,
    bench: bool,
}

fn parse_args() -> Result<Options, String> {
    let mut args = std::env::args().skip(1);
    let mut file = None;
    let mut json = None;
    let mut bench = false;
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--json" => json = Some(PathBuf::from(args.next().ok_or("--json needs a path")?)),
            "--bench" => bench = true,
            path if file.is_none() => file = Some(PathBuf::from(path)),
            other => return Err(format!("unexpected argument {other}")),
        }
    }
    Ok(Options {
        file: file.ok_or("usage: ref_counts FILE [--json OUT] [--bench]")?,
        json,
        bench,
    })
}

fn report(counts: &[u64; 256], framed_only: &[u64; 256], errors: u64, wall_s: Option<f64>) -> String {
    let mut by_type = BTreeMap::new();
    let mut only = BTreeMap::new();
    for byte in 0..256 {
        let tag = byte as u8 as char;
        if counts[byte] + framed_only[byte] > 0 {
            by_type.insert(tag, counts[byte] + framed_only[byte]);
        }
        if framed_only[byte] > 0 {
            only.insert(tag, framed_only[byte]);
        }
    }
    let total: u64 = by_type.values().sum();
    let list = |map: &BTreeMap<char, u64>| {
        map.iter()
            .map(|(k, v)| format!("\"{k}\":{v}"))
            .collect::<Vec<_>>()
            .join(",")
    };
    let timing = wall_s.map_or(String::new(), |s| format!(",\"wall_s\":{s:.3}"));
    format!(
        "{{\"by_type\":{{{}}},\"errors\":{errors},\"framed_only\":{{{}}},\"parser\":\"itchy 0.3.4\",\"total\":{total}{timing}}}\n",
        list(&by_type),
        list(&only)
    )
}

fn run(options: &Options) -> Result<bool, Box<dyn std::error::Error>> {
    let mut filter = FrameFilter::new(File::open(&options.file)?);
    let start = Instant::now();
    let mut counts = [0u64; 256];
    let mut errors = 0u64;
    {
        let stream = itchy::MessageStream::from_reader(&mut filter);
        for message in stream {
            match message {
                Ok(message) => counts[message.tag as usize] += 1,
                Err(error) => {
                    eprintln!("ref_counts: {error}");
                    errors += 1;
                }
            }
        }
    }
    let wall_s = options.bench.then(|| start.elapsed().as_secs_f64());
    let text = report(&counts, &filter.framed_only, errors, wall_s);
    match &options.json {
        Some(path) => File::create(path)?.write_all(text.as_bytes())?,
        None => io::stdout().write_all(text.as_bytes())?,
    }
    Ok(errors == 0)
}

fn main() -> ExitCode {
    let outcome = parse_args()
        .map_err(Into::into)
        .and_then(|options| run(&options));
    match outcome {
        Ok(true) => ExitCode::SUCCESS,
        Ok(false) => ExitCode::from(3),
        Err(error) => {
            eprintln!("ref_counts: {error}");
            ExitCode::from(2)
        }
    }
}
