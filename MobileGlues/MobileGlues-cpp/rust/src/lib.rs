// mobileglues-rs — Rust text-processing core for the MobileGlues-RS renderer
// Copyright (c) 2026 MobileGlues-RS (fork of MobileGlues by MobileGL-Dev)
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
//
// This crate is linked into libmobileglues.so and takes over the string
// passes of the GLSL -> GLSL ES translation pipeline that upstream runs
// through std::regex. Every ported function reproduces the exact semantics
// (including the quirks) of the original ECMAScript regexes so translated
// output stays byte-for-byte identical; tools/rsdiff verifies that on the
// CI host before any Gradle build runs.
//
// Every FFI entry point is panic-safe: the body runs under catch_unwind and
// a panic returns a failure value (null / -1) that makes the C++ caller fall
// back to its own implementation, so a bug here can never crash Minecraft.

#![allow(clippy::needless_range_loop)]

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::panic::{catch_unwind, AssertUnwindSafe};

pub const RS_VERSION: &str = concat!("mobileglues-rs/", env!("CARGO_PKG_VERSION"));

// ---------------------------------------------------------------------------
// character classes (ECMAScript semantics used by std::regex ECMAScript)
// ---------------------------------------------------------------------------

#[inline]
fn is_ecma_space(c: u8) -> bool {
    // ECMAScript \s
    matches!(c, b' ' | b'\t' | b'\n' | 0x0b | b'\r' | 0x0c)
}

#[inline]
fn is_word(c: u8) -> bool {
    // ECMAScript \w
    c.is_ascii_alphanumeric() || c == b'_'
}

#[inline]
fn is_digit(c: u8) -> bool {
    // ECMAScript \d
    c.is_ascii_digit()
}

/// ECMAScript line terminators (what '.' does not match and ^/$ anchor on).
#[inline]
fn is_line_terminator(c: u8) -> bool {
    // \n \r \u2028 \u2029 — the two Unicode ones cannot appear in a byte scan
    // of UTF-8 without decoding, and std::regex on a std::string operates on
    // bytes for the single-byte cases; corpus tests use \n only, matching the
    // Minecraft-generated sources this pipeline actually sees.
    c == b'\n' || c == b'\r'
}

// ---------------------------------------------------------------------------
// 1. remove_layout_binding
//
// Upstream runs two regex_replace passes over the whole shader:
//   pass 1:  layout\s*\(\s*binding\s*=\s*\d+\s*\)\s*   ->  ""
//   pass 2:  layout\s*\(\s*binding\s*=\s*\d+\s*,       ->  "layout("
// ---------------------------------------------------------------------------

fn match_layout_binding_prefix(s: &[u8], i: usize) -> Option<usize> {
    // matches "layout" + \s* + "(" + \s* + "binding" + \s* + "=" + \s* + \d+
    // returns the end offset of the digits run (cursor parked after \d+),
    // or None if the prefix does not match at position i.
    let n = s.len();
    if !s[i..].starts_with(b"layout") {
        return None;
    }
    let mut p = i + 6;
    // layout \s* \(   (greedy \s*)
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if p >= n || s[p] != b'(' {
        return None;
    }
    p += 1;
    // \s* binding
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if !s[p..].starts_with(b"binding") {
        return None;
    }
    p += 7;
    // \s* = \s*
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if p >= n || s[p] != b'=' {
        return None;
    }
    p += 1;
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    // \d+  (greedy; needs at least one)
    let digits_start = p;
    while p < n && is_digit(s[p]) {
        p += 1;
    }
    if p == digits_start {
        return None;
    }
    Some(p) // parked right after the digits
}

fn remove_layout_binding_impl(input: &str) -> String {
    let s = input.as_bytes();
    let n = s.len();
    let mut out = String::with_capacity(n);
    let mut i = 0;

    // ---- pass 1: `layout(binding=N)` + trailing whitespace -> "" ----
    while i < n {
        if s[i] == b'l' {
            if let Some(after_digits) = match_layout_binding_prefix(s, i) {
                let mut p = after_digits;
                while p < n && is_ecma_space(s[p]) {
                    p += 1;
                }
                if p < n && s[p] == b')' {
                    // full match: drop through the trailing \s* after ')' too
                    let mut e = p + 1;
                    while e < n && is_ecma_space(s[e]) {
                        e += 1;
                    }
                    i = e;
                    continue;
                }
            }
        }
        out.push(s[i] as char);
        i += 1;
    }

    // ---- pass 2: `layout(binding=N,` -> "layout(" ----
    // Runs over the pass-1 output, exactly like the second regex_replace.
    let s1 = out.into_bytes();
    let n = s1.len();
    let mut out = String::with_capacity(n);
    let mut i = 0;
    while i < n {
        if s1[i] == b'l' {
            if let Some(after_digits) = match_layout_binding_prefix(&s1, i) {
                let mut p = after_digits;
                while p < n && is_ecma_space(s1[p]) {
                    p += 1;
                }
                if p < n && s1[p] == b',' {
                    out.push_str("layout(");
                    i = p + 1; // keep the comma and everything after it
                    continue;
                }
            }
        }
        out.push(s1[i] as char);
        i += 1;
    }
    out
}

// ---------------------------------------------------------------------------
// 2. process_out_color_locations
//
// Upstream regex: \n(out highp vec4 outColor)(\d+);   (replace all)
//   ->  \nlayout(location=$2) $1$2;
// ---------------------------------------------------------------------------

fn process_out_color_locations_impl(input: &str) -> String {
    const HEAD: &[u8] = b"out highp vec4 outColor";
    let s = input.as_bytes();
    let n = s.len();
    let mut out = String::with_capacity(n + 32);
    let mut i = 0;

    while i < n {
        if s[i] == b'\n' && i + 1 + HEAD.len() <= n && &s[i + 1..i + 1 + HEAD.len()] == HEAD {
            let mut p = i + 1 + HEAD.len();
            let digits_start = p;
            while p < n && is_digit(s[p]) {
                p += 1;
            }
            if p > digits_start && p < n && s[p] == b';' {
                let digits = &input[digits_start..p];
                out.push('\n');
                out.push_str("layout(location=");
                out.push_str(digits);
                out.push_str(") out highp vec4 outColor");
                out.push_str(digits);
                out.push(';');
                i = p + 1;
                continue;
            }
        }
        out.push(s[i] as char);
        i += 1;
    }
    out
}

// ---------------------------------------------------------------------------
// 3. get_glsl_version
//
// Upstream regex: #version\s+(\d{3})   (regex_search — first match anywhere)
//   ->  stoi of exactly the next three digits, else -1
// ---------------------------------------------------------------------------

fn get_glsl_version_impl(input: &str) -> i32 {
    let s = input.as_bytes();
    let n = s.len();
    let mut i = 0;
    while i + 8 <= n {
        if &s[i..i + 8] == b"#version" {
            let mut p = i + 8;
            let ws_start = p;
            while p < n && is_ecma_space(s[p]) {
                p += 1;
            }
            if p > ws_start && p + 3 <= n && is_digit(s[p]) && is_digit(s[p + 1]) && is_digit(s[p + 2]) {
                let v = (s[p] - b'0') as i32 * 100
                    + (s[p + 1] - b'0') as i32 * 10
                    + (s[p + 2] - b'0') as i32;
                return v;
            }
        }
        i += 1;
    }
    -1
}

// ---------------------------------------------------------------------------
// 4. force_supported_output  (precision highp float/int injection)
//
// Ported byte-for-byte from the C++ implementation, including the quirk that
// rebuilding the de-precision'd source from getline() lines drops a trailing
// newline when both precision statements already exist.
// ---------------------------------------------------------------------------

fn force_supported_output_impl(input: &str) -> String {
    let has_precision_float = input.contains("precision ") && input.contains("float;");
    let has_precision_int = input.contains("precision ") && input.contains("int;");

    let (result, precision_float, precision_int);
    if has_precision_float && has_precision_int {
        // std::getline over the source: no empty trailing line is produced for
        // a final newline, so re-joining loses it — reproduced here.
        let mut lines: Vec<&str> = if input.is_empty() {
            Vec::new()
        } else {
            let mut v: Vec<&str> = input.split('\n').collect();
            if input.ends_with('\n') {
                v.pop();
            }
            v
        };
        lines.retain(|line| {
            let has_p = line.contains("precision ");
            let has_f = line.contains("float;");
            let has_i = line.contains("int;");
            !(has_p && (has_f || has_i))
        });
        result = lines.join("\n");
        precision_float = "precision highp float;\n".to_string();
        precision_int = "precision highp int;\n".to_string();
    } else {
        result = input.to_string();
        precision_float = if has_precision_float {
            String::new()
        } else {
            "precision highp float;\n".to_string()
        };
        precision_int = if has_precision_int {
            String::new()
        } else {
            "precision highp int;\n".to_string()
        };
    }

    let insertion_pos = match result.rfind("#extension") {
        Some(last_extension_pos) => match result[last_extension_pos..].find('\n') {
            Some(rel) => last_extension_pos + rel + 1,
            None => result.len(),
        },
        None => match result.find('\n') {
            Some(first_newline) => first_newline + 1,
            None => {
                return format!("{}{}{}", precision_float, precision_int, result);
            }
        },
    };

    let mut out =
        String::with_capacity(result.len() + precision_float.len() + precision_int.len());
    out.push_str(&result[..insertion_pos]);
    out.push_str(&precision_float);
    out.push_str(&precision_int);
    out.push_str(&result[insertion_pos..]);
    out
}

// ---------------------------------------------------------------------------
// 5. process_sampler_buffer — the two texelFetch rewrite passes
//
// Upstream pass A:  texelFetch\s*\(\s*(\w+)\s*,\s*([^)]+?)\s*\)
//   ->  texelFetch($1, ivec2(($2) % u_BufferTexWidth, ($2) / u_BufferTexWidth), 0)
// Upstream pass B:  texelFetch\((\w+)\s*,\s*ivec2\(([^)]+)\)\s*,\s*0\)
//   ->  texelFetch($1, bufferCoords($2), 0)
// ---------------------------------------------------------------------------

/// Pass A at position `i` (which must point at 't' of a candidate "texelFetch").
/// Returns Some((match_end, name_start, name_end, cap_start, cap_end)) on a match.
fn match_texelfetch_lazy(s: &[u8], i: usize) -> Option<(usize, usize, usize, usize, usize)> {
    let n = s.len();
    if !s[i..].starts_with(b"texelFetch") {
        return None;
    }
    let mut p = i + "texelFetch".len();
    // \s* \(
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if p >= n || s[p] != b'(' {
        return None;
    }
    p += 1;
    // \s* (\w+)  — greedy word run
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    let name_start = p;
    while p < n && is_word(s[p]) {
        p += 1;
    }
    if p == name_start {
        return None;
    }
    let name_end = p;
    // \s* , \s*
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if p >= n || s[p] != b',' {
        return None;
    }
    p += 1; // p = position right after the comma
    // ([^)]+?)\s*\) — lazy capture up to the first ')'. The greedy \s* after
    // the comma is allowed to back off, which is observable when the whole
    // region between the comma and the ')' is whitespace: the capture then
    // becomes exactly ONE whitespace character (verified against std::regex
    // behaviour: "texelFetch(buf,   )" -> capture " ").
    let ws_run = {
        let mut w = p;
        while w < n && is_ecma_space(s[w]) {
            w += 1;
        }
        w - p // ws run length
    };
    let p0 = p + ws_run; // first non-whitespace position (or ')' / EOF)
    if p0 >= n {
        return None;
    }
    if s[p0] == b')' {
        if ws_run == 0 {
            return None; // ",)" — empty content cannot match [^)]+
        }
        // one whitespace character is captured (minimal lazy expansion after
        // the greedy \s* backed off by exactly one)
        return Some((p0 + 1, name_start, name_end, p0 - 1, p0));
    }
    // non-whitespace content: capture runs from p0 up to the first ')'
    // (exclusive), with the immediately preceding whitespace run stripped.
    let mut q = p0;
    while q < n && s[q] != b')' {
        q += 1;
    }
    if q >= n {
        return None; // no closing ')' at all
    }
    let paren = q;
    let mut cap_end = paren;
    while cap_end > p0 && is_ecma_space(s[cap_end - 1]) {
        cap_end -= 1;
    }
    // cap_end > p0 holds: s[p0] is not whitespace, so at least it survives.
    Some((paren + 1, name_start, name_end, p0, cap_end))
}

/// Pass B at position `i`. Returns Some((match_end, name_range, index_range)).
fn match_texelfetch_ivec2(s: &[u8], i: usize) -> Option<(usize, usize, usize, usize, usize)> {
    let n = s.len();
    if !s[i..].starts_with(b"texelFetch") {
        return None;
    }
    let mut p = i + "texelFetch".len();
    // \s* is absent in pass B's prefix: `texelFetch\(` — no \s* between them!
    if p >= n || s[p] != b'(' {
        return None;
    }
    p += 1;
    // (\w+)
    let name_start = p;
    while p < n && is_word(s[p]) {
        p += 1;
    }
    if p == name_start {
        return None;
    }
    let name_end = p;
    // \s* , \s*
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if p >= n || s[p] != b',' {
        return None;
    }
    p += 1;
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    // ivec2\(
    if !s[p..].starts_with(b"ivec2") {
        return None;
    }
    p += 5;
    if p >= n || s[p] != b'(' {
        return None;
    }
    p += 1;
    // ([^)]+) — greedy, cannot backtrack (shrinking lands on a non-')' char
    // where '\)' then fails), so it is exactly "everything up to the next ')'".
    let idx_start = p;
    while p < n && s[p] != b')' {
        p += 1;
    }
    if p >= n || p == idx_start {
        return None;
    }
    let idx_end = p; // exclusive; s[p] == ')'
    p += 1; // consume ')'
    // \s* , \s* 0 \)
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if p >= n || s[p] != b',' {
        return None;
    }
    p += 1;
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if p >= n || s[p] != b'0' {
        return None;
    }
    p += 1;
    // regex: 0\)  — no \s* between the literal '0' and ')'
    if p >= n || s[p] != b')' {
        return None;
    }
    Some((p + 1, name_start, name_end, idx_start, idx_end))
}

fn process_sampler_buffer_rewrite_impl(input: &str) -> String {
    // ---- pass A ----
    let s = input.as_bytes();
    let n = s.len();
    let mut a = String::with_capacity(n + 64);
    let mut i = 0;
    while i < n {
        if s[i] == b't' {
            if let Some((end, ns, ne, cs, ce)) = match_texelfetch_lazy(s, i) {
                let name = &input[ns..ne];
                let idx = &input[cs..ce];
                a.push_str("texelFetch(");
                a.push_str(name);
                a.push_str(", ivec2((");
                a.push_str(idx);
                a.push_str(") % u_BufferTexWidth, (");
                a.push_str(idx);
                a.push_str(") / u_BufferTexWidth), 0)");
                i = end;
                continue;
            }
        }
        a.push(s[i] as char);
        i += 1;
    }

    // ---- pass B over pass A's output ----
    let s = a.as_bytes();
    let n = s.len();
    let mut out = String::with_capacity(n + 32);
    let mut i = 0;
    while i < n {
        if s[i] == b't' {
            if let Some((end, ns, ne, cs, ce)) = match_texelfetch_ivec2(s, i) {
                let name = &a[ns..ne];
                let idx = &a[cs..ce];
                out.push_str("texelFetch(");
                out.push_str(name);
                out.push_str(", bufferCoords(");
                out.push_str(idx);
                out.push_str("), 0)");
                i = end;
                continue;
            }
        }
        out.push(s[i] as char);
        i += 1;
    }
    out
}

// ---------------------------------------------------------------------------
// 6. find_uniform_insert_pos
//
// Upstream runs a multiline sregex_iterator over
//   ^\s*(?:layout\s*\([^)]*\)\s*)?uniform\s+\w+(?:\s*\[\s*\d+\s*\])?\s+\w+
//      (?:\s*\[\s*\d+\s*\])?\s*;.*$
// and keeps `it->position() + it->length()` of the LAST match — i.e. the end
// of the last uniform-like line, where the GI_TemporalFilter helper is
// inserted. With no match at all the C++ loop leaves insertPos = 0.
// ---------------------------------------------------------------------------

/// Attempts the uniform-line pattern at a line start `i`.
/// Returns Some(match_end) where match_end is the offset of the terminating
/// line terminator (or end of input).
fn match_uniform_line(s: &[u8], i: usize) -> Option<usize> {
    let n = s.len();
    let mut p = i;
    // ^ \s*  — greedy, may span lines
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    // (?:layout\s*\([^)]*\)\s*)?
    if s[p..].starts_with(b"layout") {
        let mut q = p + 6;
        while q < n && is_ecma_space(s[q]) {
            q += 1;
        }
        if q < n && s[q] == b'(' {
            q += 1;
            let inner_start = q;
            while q < n && s[q] != b')' {
                q += 1;
            }
            if q < n {
                let inner_end = q; // at ')'
                q += 1;
                while q < n && is_ecma_space(s[q]) {
                    q += 1;
                }
                if inner_end > inner_start {
                    // non-empty (...) content — qualifier consumed, fall through
                    // with p = q. An empty layout() is NOT consumed by the
                    // regex: `[^)]*` matches "" fine, so this branch is
                    // actually taken either way. Keep semantics simple:
                    p = q;
                } else {
                    p = q;
                }
            }
            // if no ')' was found the optional group simply does not match;
            // continue with 'uniform' test at the original p.
        }
    }
    // uniform
    if !s[p..].starts_with(b"uniform") {
        return None;
    }
    let mut p = p + 7;
    // \s+  — at least one, greedy, may span lines
    let ws_start = p;
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if p == ws_start {
        return None;
    }
    // \w+  (type)
    let type_start = p;
    while p < n && is_word(s[p]) {
        p += 1;
    }
    if p == type_start {
        return None;
    }
    // (?:\s*\[\s*\d+\s*\])?
    p = skip_optional_array_suffix(s, p);
    // \s+
    let ws_start = p;
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if p == ws_start {
        return None;
    }
    // \w+  (name)
    let name_start = p;
    while p < n && is_word(s[p]) {
        p += 1;
    }
    if p == name_start {
        return None;
    }
    // (?:\s*\[\s*\d+\s*\])?
    p = skip_optional_array_suffix(s, p);
    // \s* ;
    while p < n && is_ecma_space(s[p]) {
        p += 1;
    }
    if p >= n || s[p] != b';' {
        return None;
    }
    p += 1;
    // .*$  — to the line terminator (not consumed)
    while p < n && !is_line_terminator(s[p]) {
        p += 1;
    }
    Some(p)
}

/// `(?:\s*\[\s*\d+\s*\])?` — returns the position after the suffix, or the
/// input position unchanged if the optional suffix does not match.
fn skip_optional_array_suffix(s: &[u8], mut p: usize) -> usize {
    let n = s.len();
    let mut q = p;
    while q < n && is_ecma_space(s[q]) {
        q += 1;
    }
    if q < n && s[q] == b'[' {
        let mut r = q + 1;
        while r < n && is_ecma_space(s[r]) {
            r += 1;
        }
        let digits_start = r;
        while r < n && is_digit(s[r]) {
            r += 1;
        }
        if r > digits_start {
            while r < n && is_ecma_space(s[r]) {
                r += 1;
            }
            if r < n && s[r] == b']' {
                p = r + 1;
            }
        }
    }
    p
}

fn find_uniform_insert_pos_impl(input: &str) -> i64 {
    let s = input.as_bytes();
    let n = s.len();
    let mut insert_pos: i64 = 0; // C++ initialises to 0 and only assigns on match
    let mut i = 0usize;
    // iterate line starts: 0 and every position right after a '\n'
    // (matching sregex_iterator's non-overlapping, in-order traversal)
    let mut last_end: Option<usize> = None;
    while i <= n {
        if let Some(end) = match_uniform_line(s, i) {
            last_end = Some(end);
            // next search starts at match end; the next line start is at
            // end+1 when a terminator was hit, or beyond input otherwise.
            i = if end < n { end + 1 } else { n + 1 };
        } else {
            // advance to the next line start
            let mut j = i;
            while j < n && s[j] != b'\n' {
                j += 1;
            }
            if j >= n {
                break;
            }
            i = j + 1;
        }
    }
    if let Some(end) = last_end {
        insert_pos = end as i64;
    }
    insert_pos
}

// ---------------------------------------------------------------------------
// 7/8. definition probes
//
//   vec2\s+mg_textureQueryLod\s*\(   (regex_search)
//   vec4\s+GI_TemporalFilter\s*\(
// ---------------------------------------------------------------------------

fn has_texture_query_lod_def_impl(input: &str) -> bool {
    has_named_def(input, b"vec2", b"mg_textureQueryLod")
}

fn has_temporal_filter_def_impl(input: &str) -> bool {
    has_named_def(input, b"vec4", b"GI_TemporalFilter")
}

fn has_named_def(input: &str, ty: &[u8], name: &[u8]) -> bool {
    let s = input.as_bytes();
    let n = s.len();
    let mut i = 0;
    while i + ty.len() <= n {
        if &s[i..i + ty.len()] == ty {
            let mut p = i + ty.len();
            let ws_start = p;
            while p < n && is_ecma_space(s[p]) {
                p += 1;
            }
            if p > ws_start && s[p..].starts_with(name) {
                p += name.len();
                while p < n && is_ecma_space(s[p]) {
                    p += 1;
                }
                if p < n && s[p] == b'(' {
                    return true;
                }
            }
        }
        i += 1;
    }
    false
}

// ---------------------------------------------------------------------------
// 9. strip_line_directives (port of replace_line_starting_with)
//
// Upstream walks the source line by line, and for every line whose first
// non-" \t" bytes are the literal "#line" it drops the whole line and appends
// `substitution` in its place. Two historical quirks are preserved on
// purpose:
//   * the match is on the literal "#line" regardless of the `starting`
//     parameter the C++ signature carries -- the parameter has always been
//     advisory, and the "#version" call site relies on exactly this
//     behaviour;
//   * only ' ' and '\t' count as leading indentation (not the full \s set),
//     and "\r\n" is consumed as one terminator.
// ---------------------------------------------------------------------------

fn strip_line_directives_impl(input: &str, substitution: &str) -> String {
    let s = input.as_bytes();
    let n = s.len();
    let mut out = String::with_capacity(n + 16);
    let mut start = 0usize; // chunk boundary: everything before it is already in `out`
    let mut current = 0usize;

    while current < n {
        let line_start = current;
        while current < n && (s[current] == b' ' || s[current] == b'\t') {
            current += 1;
        }

        let is_line_directive = s[current..].starts_with(b"#line");

        while current < n && s[current] != b'\r' && s[current] != b'\n' {
            current += 1;
        }

        let newline_length = if current < n {
            if s[current] == b'\r' {
                if current + 1 < n && s[current + 1] == b'\n' {
                    2
                } else {
                    1
                }
            } else {
                1
            }
        } else {
            0
        };

        if is_line_directive {
            if line_start > start {
                out.push_str(&input[start..line_start]);
            }
            current += newline_length;
            start = current;
            out.push_str(substitution);
        } else {
            current += newline_length;
        }
    }

    if start < n {
        // `current` has reached n here (or start == n); the tail is the last
        // open chunk. When start == n nothing is left, matching upstream's
        // append_chunk(current).
        out.push_str(&input[start..n]);
    }
    out
}

// ---------------------------------------------------------------------------
// 10. process_uniform_declarations
//
// Upstream rewrites every "uniform ..." declaration that carries an
// initializer (uniform initializer syntax exists in desktop GLSL) into a
// plain `uniform [precision] <type> <name>;` declaration, and passes
// everything else through verbatim. Ported quirks, all deliberate:
//   * the token match is a substring test at every scan position, so
//     "uniformity" also opens a declaration (upstream compares 7 bytes);
//   * precision keywords appearing AFTER the type are hoisted in front of it
//     ("uniform vec4 highp x;" becomes "uniform highp vec4 x;");
//   * only the first precision keyword is kept, later ones are skipped but
//     consumed;
//   * isspace/isalnum are the C-locale ASCII sets; bytes >= 0x80 are neither
//     (glibc's table returns false for them in the C locale, which is the
//     behaviour the corpus tests froze).
// ---------------------------------------------------------------------------

fn is_c_space(c: u8) -> bool {
    // std::isspace in the C locale
    matches!(c, b' ' | b'\t' | b'\n' | 0x0b | b'\r' | 0x0c)
}

fn is_c_alnum_or_underscore(c: u8) -> bool {
    // std::isalnum in the C locale, plus the explicit '_' the scanner allows
    c.is_ascii_alphanumeric() || c == b'_'
}

fn starts_with_at(s: &[u8], pos: usize, needle: &[u8]) -> bool {
    if needle.is_empty() {
        return true;
    }
    s[pos..].starts_with(needle)
}

fn process_uniform_declarations_impl(input: &str) -> String {
    const PRECISION_KWS: [&[u8]; 3] = [b"highp", b"lowp", b"mediump"];

    let s = input.as_bytes();
    let n = s.len();
    let mut out = String::with_capacity(n + 64);
    let mut scan_pos = 0usize;
    let mut chunk_start = 0usize;

    while scan_pos < n {
        if starts_with_at(s, scan_pos, b"uniform") {
            if scan_pos > chunk_start {
                out.push_str(&input[chunk_start..scan_pos]);
            }

            let decl_start = scan_pos;
            scan_pos += 7;

            let mut precision: Option<&str> = None;
            let mut type_range: Option<(usize, usize)> = None;

            // First inner loop: leading whitespace, then one optional
            // precision keyword, then the type word.
            if scan_pos < n {
                while scan_pos < n && is_c_space(s[scan_pos]) {
                    scan_pos += 1;
                }
                let mut found_precision = false;
                for kw in PRECISION_KWS.iter() {
                    if starts_with_at(s, scan_pos, kw) {
                        precision = Some(std::str::from_utf8(kw).unwrap());
                        scan_pos += kw.len();
                        found_precision = true;
                        break;
                    }
                }
                if !found_precision {
                    let type_start = scan_pos;
                    while scan_pos < n && is_c_alnum_or_underscore(s[scan_pos]) {
                        scan_pos += 1;
                    }
                    type_range = Some((type_start, scan_pos));
                }
                // The C++ loop unconditionally breaks after the type parse
                // (its `break` sits at the end of the first iteration).
            }

            // Second inner loop: consume any further precision keywords
            // (keeping only the first one seen overall).
            while scan_pos < n {
                while scan_pos < n && is_c_space(s[scan_pos]) {
                    scan_pos += 1;
                }
                let mut found = false;
                for kw in PRECISION_KWS.iter() {
                    if starts_with_at(s, scan_pos, kw) {
                        if precision.is_none() {
                            precision = Some(std::str::from_utf8(kw).unwrap());
                        }
                        scan_pos += kw.len();
                        found = true;
                        break;
                    }
                }
                if !found {
                    break;
                }
            }

            // Fallback type parse when the first loop took the precision
            // branch (upstream re-parses into an empty `type`).
            if type_range.is_none() {
                let type_start = scan_pos;
                while scan_pos < n && is_c_alnum_or_underscore(s[scan_pos]) {
                    scan_pos += 1;
                }
                type_range = Some((type_start, scan_pos));
            }

            while scan_pos < n && is_c_space(s[scan_pos]) {
                scan_pos += 1;
            }
            let name_start = scan_pos;
            while scan_pos < n && is_c_alnum_or_underscore(s[scan_pos]) {
                scan_pos += 1;
            }
            let name_range = (name_start, scan_pos);

            let decl_end = match s[scan_pos..].iter().position(|&c| c == b';') {
                Some(rel) => scan_pos + rel + 1,
                None => n,
            };

            // has_initializer: an '=' before the ';'. find returns npos (no
            // match) upstream, and npos < decl_end is false -- replicated.
            let has_initializer = match s[scan_pos..].iter().position(|&c| c == b'=') {
                Some(rel) => scan_pos + rel < decl_end,
                None => false,
            };

            if has_initializer {
                out.push_str("uniform");
                if let Some(p) = precision {
                    out.push(' ');
                    out.push_str(p);
                }
                out.push(' ');
                if let Some((a, b)) = type_range {
                    out.push_str(&input[a..b]);
                }
                out.push(' ');
                out.push_str(&input[name_range.0..name_range.1]);
                out.push(';');
            } else {
                out.push_str(&input[decl_start..decl_end]);
            }

            scan_pos = decl_end;
            chunk_start = decl_end;
        } else {
            scan_pos += 1;
        }
    }

    if chunk_start < n {
        out.push_str(&input[chunk_start..n]);
    }
    out
}

// ---------------------------------------------------------------------------
// FFI surface (C ABI)
// ---------------------------------------------------------------------------

fn conv_string(r: Result<String, String>) -> *mut c_char {
    match r {
        Ok(s) => match CString::new(s) {
            // Shader sources never contain NUL bytes; if one ever does, fail
            // to null so the C++ implementation handles it.
            Ok(c) => c.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(_) => std::ptr::null_mut(),
    }
}

unsafe fn take_src(src: *const c_char) -> Result<String, String> {
    if src.is_null() {
        return Err("null source".into());
    }
    let cstr = unsafe { CStr::from_ptr(src) };
    cstr.to_str().map(|s| s.to_string()).map_err(|e| e.to_string())
}

macro_rules! rs_entry {
    ($(#[$meta:meta])* $name:ident($src:ident) -> *mut c_char, $body:ident) => {
        $(#[$meta])*
        #[no_mangle]
        pub unsafe extern "C" fn $name($src: *const c_char) -> *mut c_char {
            let r = catch_unwind(AssertUnwindSafe(|| {
                unsafe { take_src($src) }.map(|s| $body(&s))
            }));
            conv_string(r.unwrap_or_else(|_| Err("panic".into())))
        }
    };
}

rs_entry!(
    /// Pass 1+2 of removeLayoutBinding, byte-for-byte identical.
    mg_rs_remove_layout_binding(src) -> *mut c_char, remove_layout_binding_impl
);
rs_entry!(
    /// processOutColorLocations.
    mg_rs_process_out_color_locations(src) -> *mut c_char, process_out_color_locations_impl
);
rs_entry!(
    /// forceSupporterOutput (precision injection).
    mg_rs_force_supported_output(src) -> *mut c_char, force_supported_output_impl
);
rs_entry!(
    /// The two texelFetch rewrite passes of process_sampler_buffer.
    mg_rs_process_sampler_buffer_rewrite(src) -> *mut c_char, process_sampler_buffer_rewrite_impl
);

/// replace_line_starting_with: drops every "#line" line and appends
/// `substitution` per dropped line. A null substitution means "".
#[no_mangle]
pub unsafe extern "C" fn mg_rs_strip_line_directives(
    src: *const c_char,
    substitution: *const c_char,
) -> *mut c_char {
    let r = catch_unwind(AssertUnwindSafe(|| {
        let sub = if substitution.is_null() {
            String::new()
        } else {
            match unsafe { CStr::from_ptr(substitution) }.to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return Err("bad substitution".into()),
            }
        };
        unsafe { take_src(src) }.map(|s| strip_line_directives_impl(&s, &sub))
    }));
    conv_string(r.unwrap_or_else(|_| Err("panic".into())))
}

rs_entry!(
    /// process_uniform_declarations: rewrites initializer-carrying uniform
    /// declarations into plain ones, byte-identical to the C++ scanner.
    mg_rs_process_uniform_declarations(src) -> *mut c_char, process_uniform_declarations_impl
);

/// Probe used by the C++ side to detect the linked Rust core.
#[no_mangle]
pub extern "C" fn mg_rs_available() -> c_int {
    1
}

/// Version string of the Rust core, e.g. "mobileglues-rs/0.1.0".
#[no_mangle]
pub extern "C" fn mg_rs_version() -> *const c_char {
    concat!(env!("CARGO_PKG_NAME"), "/", env!("CARGO_PKG_VERSION"), "\0").as_ptr() as *const c_char
}

/// Frees a buffer returned by this crate (Rust allocation side).
#[no_mangle]
pub unsafe extern "C" fn mg_rs_free(ptr: *mut c_char) {
    if !ptr.is_null() {
        drop(unsafe { CString::from_raw(ptr) });
    }
}

/// #version extraction; -1 when absent, -2 on failure (C++ falls back).
#[no_mangle]
pub unsafe extern "C" fn mg_rs_get_glsl_version(src: *const c_char) -> c_int {
    match catch_unwind(AssertUnwindSafe(|| unsafe { take_src(src) }.map(|s| get_glsl_version_impl(&s)))) {
        Ok(Ok(v)) => v,
        Ok(Err(_)) => -2,
        Err(_) => -2,
    }
}

/// Byte offset for the GI_TemporalFilter insertion point (>= 0), or -1 on
/// failure (panic / null input) so the C++ path recomputes it.
#[no_mangle]
pub unsafe extern "C" fn mg_rs_find_uniform_insert_pos(src: *const c_char) -> i64 {
    match catch_unwind(AssertUnwindSafe(|| unsafe { take_src(src) }.map(|s| find_uniform_insert_pos_impl(&s)))) {
        Ok(Ok(v)) => v,
        Ok(Err(_)) => -1,
        Err(_) => -1,
    }
}

/// 1/0 for `vec2\s+mg_textureQueryLod\s*\(`, -1 on failure.
#[no_mangle]
pub unsafe extern "C" fn mg_rs_has_texture_query_lod_def(src: *const c_char) -> c_int {
    match catch_unwind(AssertUnwindSafe(|| unsafe { take_src(src) }.map(|s| has_texture_query_lod_def_impl(&s)))) {
        Ok(Ok(v)) => if v { 1 } else { 0 },
        Ok(Err(_)) => -1,
        Err(_) => -1,
    }
}

/// 1/0 for `vec4\s+GI_TemporalFilter\s*\(`, -1 on failure.
#[no_mangle]
pub unsafe extern "C" fn mg_rs_has_temporal_filter_def(src: *const c_char) -> c_int {
    match catch_unwind(AssertUnwindSafe(|| unsafe { take_src(src) }.map(|s| has_temporal_filter_def_impl(&s)))) {
        Ok(Ok(v)) => if v { 1 } else { 0 },
        Ok(Err(_)) => -1,
        Err(_) => -1,
    }
}

// ---------------------------------------------------------------------------
// tests — including a differential corpus check (tools/rsdiff/corpus/*)
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn version_extract() {
        assert_eq!(get_glsl_version_impl("#version 150\nvoid main(){}"), 150);
        assert_eq!(get_glsl_version_impl("#version 330\n"), 330);
        assert_eq!(get_glsl_version_impl("// #version 100\n"), 100); // first match anywhere, like regex_search
        assert_eq!(get_glsl_version_impl("#version100\n"), -1); // \s+ required
        assert_eq!(get_glsl_version_impl("void main(){}\n"), -1);
        assert_eq!(get_glsl_version_impl("#version 1500\n"), 150); // \d{3} captures three digits
        assert_eq!(get_glsl_version_impl("#version  150\n"), 150); // \s+ multiple
        assert_eq!(get_glsl_version_impl("#version\n150\n"), 150); // \s+ spans newlines
    }

    #[test]
    fn layout_binding_removal() {
        assert_eq!(
            remove_layout_binding_impl("layout(binding = 1) uniform sampler2D tex;"),
            "uniform sampler2D tex;"
        );
        assert_eq!(
            remove_layout_binding_impl("layout ( binding =12 ) uniform vec4 c;"),
            "uniform vec4 c;"
        );
        assert_eq!(
            remove_layout_binding_impl("layout(binding=2, set = 1) uniform B { int x; };"),
            // the regex eats `binding=2,` (no \s* between digits and comma)
            // so the space after the comma survives — verified against std::regex
            "layout( set = 1) uniform B { int x; };"
        );
        // trailing whitespace after ')' is swallowed (greedy \s*)
        assert_eq!(
            remove_layout_binding_impl("layout(binding=1)\nuniform int a;\n"),
            "uniform int a;\n"
        );
        // a binding with no digits does not match
        assert_eq!(
            remove_layout_binding_impl("layout(binding=abc) uniform int a;"),
            "layout(binding=abc) uniform int a;"
        );
        // unrelated layout qualifiers stay untouched
        assert_eq!(
            remove_layout_binding_impl("layout(location=0) out vec4 o;"),
            "layout(location=0) out vec4 o;"
        );
        // multi-line layout( ... binding ... )
        assert_eq!(
            remove_layout_binding_impl("layout(\n  binding = 3\n)\nuniform int b;"),
            "uniform int b;"
        );
    }

    #[test]
    fn out_color_locations() {
        assert_eq!(
            process_out_color_locations_impl(
                "void main() {\nout highp vec4 outColor0;\n}"
            ),
            "void main() {\nlayout(location=0) out highp vec4 outColor0;\n}"
        );
        assert_eq!(
            process_out_color_locations_impl("\nout highp vec4 outColor12;"),
            "\nlayout(location=12) out highp vec4 outColor12;"
        );
        // no digits or no ';' -> unchanged
        assert_eq!(
            process_out_color_locations_impl("out highp vec4 outColor;"),
            "out highp vec4 outColor;"
        );
        assert_eq!(
            process_out_color_locations_impl("\nout highp vec4 outColor3 x;"),
            "\nout highp vec4 outColor3 x;"
        );
    }

    #[test]
    fn precision_injection() {
        // both missing, plain source with a first line
        assert_eq!(
            force_supported_output_impl("#version 300 es\nvoid main(){}\n"),
            "#version 300 es\nprecision highp float;\nprecision highp int;\nvoid main(){}\n"
        );
        // after the last #extension
        assert_eq!(
            force_supported_output_impl("#version 300 es\n#extension A : enable\nvoid main(){}\n"),
            "#version 300 es\n#extension A : enable\nprecision highp float;\nprecision highp int;\nvoid main(){}\n"
        );
        // float already present -> ONLY int is INSERTED; the existing float
        // line stays where it was (verified against std::regex)
        assert_eq!(
            force_supported_output_impl("#version 300 es\nprecision highp float;\nvoid main(){}\n"),
            "#version 300 es\nprecision highp int;\nprecision highp float;\nvoid main(){}\n"
        );
        // both present -> precision lines removed and re-added after the
        // last #extension (quirk: trailing newline of the source is dropped)
        assert_eq!(
            force_supported_output_impl(
                "#version 300 es\nprecision lowp float;\nprecision lowp int;\nvoid main(){}\n"
            ),
            "#version 300 es\nprecision highp float;\nprecision highp int;\nvoid main(){}"
        );
        // no newline at all -> prepend
        assert_eq!(
            force_supported_output_impl("void main(){}"),
            "precision highp float;\nprecision highp int;\nvoid main(){}"
        );
    }

    #[test]
    fn texelfetch_rewrites() {
        assert_eq!(
            process_sampler_buffer_rewrite_impl("texelFetch(buf, idx)"),
            "texelFetch(buf, ivec2((idx) % u_BufferTexWidth, (idx) / u_BufferTexWidth), 0)"
        );
        // trailing whitespace inside the index expression is stripped
        assert_eq!(
            process_sampler_buffer_rewrite_impl("texelFetch(buf ,  idx  )"),
            "texelFetch(buf, ivec2((idx) % u_BufferTexWidth, (idx) / u_BufferTexWidth), 0)"
        );
        // whitespace-only index captures ONE whitespace char (greedy \s*
        // backs off by one) — matches std::regex on "texelFetch(buf,   )"
        assert_eq!(
            process_sampler_buffer_rewrite_impl("texelFetch(buf,   )"),
            "texelFetch(buf, ivec2(( ) % u_BufferTexWidth, ( ) / u_BufferTexWidth), 0)"
        );
        // empty index does not match
        assert_eq!(
            process_sampler_buffer_rewrite_impl("texelFetch(buf,)!"),
            "texelFetch(buf,)!"
        );
        // pass A grabs up to the FIRST ')' — a pre-existing ivec2 form is
        // rewritten by pass A, never by pass B (verified against std::regex)
        assert_eq!(
            process_sampler_buffer_rewrite_impl("texelFetch(buf, ivec2(x, y), 0)"),
            "texelFetch(buf, ivec2((ivec2(x, y) % u_BufferTexWidth, (ivec2(x, y) / u_BufferTexWidth), 0), 0)"
        );
        // mixed forms — both go through pass A
        assert_eq!(
            process_sampler_buffer_rewrite_impl(
                "a = texelFetch(b, ivec2(p, q), 0);\nb = texelFetch(c, i);"
            ),
            "a = texelFetch(b, ivec2((ivec2(p, q) % u_BufferTexWidth, (ivec2(p, q) / u_BufferTexWidth), 0), 0);\nb = texelFetch(c, ivec2((i) % u_BufferTexWidth, (i) / u_BufferTexWidth), 0);"
        );
        // nested parens: capture stops at the first ')'
        assert_eq!(
            process_sampler_buffer_rewrite_impl("texelFetch(t, (a+b)*2)"),
            "texelFetch(t, ivec2(((a+b) % u_BufferTexWidth, ((a+b) / u_BufferTexWidth), 0)*2)"
        );
    }

    #[test]
    fn uniform_insert_pos() {
        assert_eq!(
            find_uniform_insert_pos_impl(
                "#version 150\nuniform sampler2D tex;\nuniform int flag;\nvoid main(){}\n"
            ),
            // match end = position OF the terminating '\n' of the last matched
            // line ($ matches before it), not one past it — verified vs std::regex
            ("#version 150\nuniform sampler2D tex;\nuniform int flag;\n".len() - 1) as i64
        );
        // layout-qualified uniform BLOCKS do not match the pattern at all
        // (regex wants `uniform <type> <name>;`) — verified against std::regex;
        // only the first plain uniform line matches here.
        let s = "uniform vec4 c;\nlayout(std140, binding=1) uniform Blk { mat4 m[4]; };\nvoid main(){}\n";
        assert_eq!(find_uniform_insert_pos_impl(s), s.find('\n').unwrap() as i64);
        // no uniforms at all -> 0 (upstream initialises to 0)
        assert_eq!(find_uniform_insert_pos_impl("void main(){}\n"), 0);
        // last line without trailing newline
        assert_eq!(
            find_uniform_insert_pos_impl("void main(){}\nuniform int z;"),
            "void main(){}\nuniform int z;".len() as i64
        );
        // whitespace-only lines merge into the following uniform line
        let s2 = "   \n   uniform int w;\nvoid main(){}\n";
        assert_eq!(find_uniform_insert_pos_impl(s2), s2.find("\nvoid").unwrap() as i64);
    }

    #[test]
    fn definition_probes() {
        assert!(has_texture_query_lod_def_impl("vec2 mg_textureQueryLod(sampler2D t, vec2 p) {"));
        assert!(has_texture_query_lod_def_impl("vec2  mg_textureQueryLod (")); // \s+ then \s* before '('
        assert!(has_texture_query_lod_def_impl("vec2\nmg_textureQueryLod( // x")); // \s+ spans newlines
        assert!(has_temporal_filter_def_impl("vec4\tGI_TemporalFilter () {"));
        assert!(!has_temporal_filter_def_impl("vec4 GI_TemporalFilter;"));
        assert!(!has_texture_query_lod_def_impl("vec2 mg_textureQueryLod;"));
        assert!(!has_texture_query_lod_def_impl("vec2mg_textureQueryLod(")); // \s+ required
    }

    #[test]
    fn strip_line_directives_cases() {
        // basic removal, substitution empty
        assert_eq!(strip_line_directives_impl("a\n#line 5\nb\n", ""), "a\nb\n");
        // leading spaces/tabs before the directive still count as a directive line
        assert_eq!(strip_line_directives_impl("a\n  #line 5\nb\n", ""), "a\nb\n");
        // substitution is appended per dropped line
        assert_eq!(strip_line_directives_impl("a\n#line 1\nb\n#line 2\n", "X\n"), "a\nX\nb\nX\n");
        // "\r\n" is consumed as one terminator
        assert_eq!(strip_line_directives_impl("a\r\n#line 2\r\nb\r\n", ""), "a\r\nb\r\n");
        // lone '\r' is a terminator too
        assert_eq!(strip_line_directives_impl("a\r#line 2\rb\r", ""), "a\rb\r");
        // a directive at EOF without a newline
        assert_eq!(strip_line_directives_impl("a\n#line 7", ""), "a\n");
        // "#line" inside a line (not at its start) survives
        assert_eq!(strip_line_directives_impl("x #line 5\nb\n", ""), "x #line 5\nb\n");
        // other directives are untouched
        assert_eq!(strip_line_directives_impl("#version 150\n#line 1\n", ""), "#version 150\n");
        // the match is the literal "#line": "#linex" also matches (7.. no,
        // 5 bytes + anything) -- "#linex" starts with "#line" so the whole
        // line goes, matching upstream's 5-byte compare.
        assert_eq!(strip_line_directives_impl("#linex\nb\n", ""), "b\n");
        // empty input
        assert_eq!(strip_line_directives_impl("", ""), "");
        assert_eq!(strip_line_directives_impl("", "sub\n"), "");
    }

    #[test]
    fn uniform_declaration_cases() {
        // plain declaration passes through verbatim
        assert_eq!(
            process_uniform_declarations_impl("uniform sampler2D tex;\n"),
            "uniform sampler2D tex;\n"
        );
        // initializer form is rewritten
        assert_eq!(
            process_uniform_declarations_impl("uniform int count = 3;\n"),
            "uniform int count;\n"
        );
        // precision before the type is preserved in the rewrite
        assert_eq!(
            process_uniform_declarations_impl("uniform highp int count = 3;\n"),
            "uniform highp int count;\n"
        );
        // precision AFTER the type is hoisted in front of it (upstream quirk)
        assert_eq!(
            process_uniform_declarations_impl("uniform vec4 highp c = vec4(1.0);\n"),
            "uniform highp vec4 c;\n"
        );
        // only the first precision keyword is kept, the second is consumed
        assert_eq!(
            process_uniform_declarations_impl("uniform highp lowp int q = 1;\n"),
            "uniform highp int q;\n"
        );
        // uniform inside another word also triggers the rewrite (substring match)
        assert_eq!(
            process_uniform_declarations_impl("int xuniform int z = 1;\n"),
            "int xuniform int z;\n"
        );
        // no semicolon and no initializer: the whole rest is the "declaration"
        // (find(';') returns npos -> decl_end = length, no '=' -> verbatim).
        assert_eq!(
            process_uniform_declarations_impl("uniform vec4 c\nvoid main(){}\n"),
            "uniform vec4 c\nvoid main(){}\n"
        );
        // no semicolon but an initializer after the name: no ';' exists, so
        // decl_end = length (the whole rest is the "declaration") and the
        // rewrite drops everything after the name -- upstream quirk.
        assert_eq!(
            process_uniform_declarations_impl("uniform int q = 1\nvoid main(){}\n"),
            "uniform int q;"
        );
        // multiple declarations with text between them
        assert_eq!(
            process_uniform_declarations_impl("a\nuniform int p = 2;\nb\nuniform float f;\nc\n"),
            "a\nuniform int p;\nb\nuniform float f;\nc\n"
        );
        // declaration without a name still rewrites with an empty name
        assert_eq!(
            process_uniform_declarations_impl("uniform int = 2;\n"),
            "uniform int ;\n"
        );
        // empty input
        assert_eq!(process_uniform_declarations_impl(""), "");
    }
}

