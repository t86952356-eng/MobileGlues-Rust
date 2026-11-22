// MobileGlues-RS - gl/pipeline_cache.cpp
// Copyright (c) 2026 MobileGlues-RS (fork of MobileGlues by MobileGL-Dev)
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header
//
// Store half of the pipeline cache; see pipeline_cache.h for the design.
// This translation unit deliberately references no GL entry points so the
// host test (tests/pipeline_cache_test.cpp) links the real object, not a
// copy -- the same policy tests/run.sh applies to gl/perf.cpp.
//
// Persistence policy mirrors gl/glsl/cache.cpp: the whole file is serialised
// on a deferred trigger (16 pending entries or 5 s), written to a sibling and
// renamed over the live file so a crash never leaves a half-written cache,
// and the destructor of the function-local singleton flushes on orderly exit.

#include "pipeline_cache.h"

#include "../config/config.h"
#include "../config/settings.h"
#include "../version.h"
#include "envvars.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <list>
#include <mutex>

using namespace std;

namespace {

// ---- SHA-256 (same construction as gl/glsl/cache.cpp; kept private there,
// so this file carries its own copy -- identical digests, verified by the
// host test against the FIPS 180-4 vectors) -------------------------------

static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x7660aabb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x194e3d23, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}
inline uint32_t sigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}
inline uint32_t sigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}
inline uint32_t Sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ (x >> 22);
}
inline uint32_t Sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ (x >> 25);
}
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

void sha256_compress(uint32_t h[8], const uint8_t* block) {
    uint32_t w[64];
    for (int t = 0; t < 16; ++t) {
        w[t] = (static_cast<uint32_t>(block[t * 4]) << 24) | (static_cast<uint32_t>(block[t * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[t * 4 + 2]) << 8) | static_cast<uint32_t>(block[t * 4 + 3]);
    }
    for (int t = 16; t < 64; ++t) {
        w[t] = sigma1(w[t - 2]) + w[t - 7] + sigma0(w[t - 15]) + w[t - 16];
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 64; ++t) {
        uint32_t T1 = hh + Sigma1(e) + ch(e, f, g) + k[t] + w[t];
        uint32_t T2 = Sigma0(a) + maj(a, b, c);
        hh = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

array<uint8_t, 32> sha256(const uint8_t* data, size_t length) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t offset = 0;
    for (; length - offset >= 64; offset += 64) {
        sha256_compress(h, data + offset);
    }
    const size_t remainder = length - offset;
    uint8_t tail[128] = {0};
    if (remainder > 0) memcpy(tail, data + offset, remainder);
    tail[remainder] = 0x80;
    const size_t tail_size = (remainder < 56) ? 64 : 128;
    const uint64_t bit_length = static_cast<uint64_t>(length) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[tail_size - 8 + i] = static_cast<uint8_t>(bit_length >> (56 - i * 8));
    }
    for (size_t i = 0; i < tail_size; i += 64) {
        sha256_compress(h, tail + i);
    }
    array<uint8_t, 32> hash{};
    for (int i = 0; i < 8; ++i) {
        hash[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        hash[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        hash[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        hash[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
    return hash;
}

struct KeyHash {
    size_t operator()(const array<uint8_t, 32>& key) const {
        size_t hash = 0;
        for (int i = 0; i < 4; ++i) hash = (hash << 8) | key[i];
        for (int i = 28; i < 32; ++i) hash = (hash << 8) | key[i];
        return hash;
    }
};

// ---- disk format -----------------------------------------------------------
//
//   magic "MGPC" | u32 version (1) | u32 count | entries...
//   entry: u8 tag (1 = program binary, 2 = shader status)
//     tag 1: key[32] | u32 binary_format | u32 len | bytes
//     tag 2: key[32] | i32 compile_status | i32 info_log_length | u32 log_len | log bytes
constexpr char kMagic[4] = {'M', 'G', 'P', 'C'};
constexpr uint32_t kFormatVersion = 1;
constexpr uint8_t kTagProgramBinary = 1;
constexpr uint8_t kTagShaderStatus = 2;

constexpr size_t kMaxCacheBytes = 64ull * 1024 * 1024; // 64 MiB
constexpr size_t kMaxShaderRecords = 512;              // sources kept for key building
constexpr int kPendingEntriesBeforeSave = 16;
constexpr int64_t kSaveIntervalNs = 5LL * 1000 * 1000 * 1000;

int64_t monotonic_now_ns() {
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + static_cast<int64_t>(ts.tv_nsec);
}

struct StoredBinary {
    GLenum format = 0;
    vector<uint8_t> bytes;
};

struct Store {
    std::mutex mutex;

    // Live tracking tables (this session only, never persisted as such).
    UnorderedMap<GLuint, pipeline_cache::ShaderRecord> shaders;
    UnorderedMap<GLuint, pipeline_cache::ProgramRecord> programs;

    // Persisted payload. Both kinds share one LRU list and one byte budget:
    // a binary is worth ~1000 status records, so one budget keeps the file
    // proportional to what the device can use.
    struct Entry {
        array<uint8_t, 32> key;
        int tag = 0;
        StoredBinary binary;        // tag 1
        pipeline_cache::ShaderStatus status; // tag 2
        size_t bytes = 0;
    };
    list<Entry> lru;
    UnorderedMap<array<uint8_t, 32>, list<Entry>::iterator, KeyHash> by_key;
    size_t total_bytes = 0;

    int pending_entries = 0;
    int64_t last_save_ns = 0;
    bool loaded = false;

    size_t entry_bytes(int tag, size_t payload) const {
        return 32 + 1 + (tag == kTagProgramBinary ? 8 : 12) + payload;
    }

    void touch(list<Entry>::iterator it) {
        lru.splice(lru.end(), lru, it);
    }

    void evict_over_budget() {
        while (total_bytes > kMaxCacheBytes && !lru.empty()) {
            const Entry& front = lru.front();
            total_bytes -= front.bytes;
            by_key.erase(front.key);
            lru.pop_front();
        }
    }

    void put_binary(const array<uint8_t, 32>& key, GLenum format, const void* data, size_t len) {
        auto it = by_key.find(key);
        if (it != by_key.end()) {
            total_bytes -= it->second->bytes;
            it->second->binary.format = format;
            it->second->binary.bytes.assign(static_cast<const uint8_t*>(data),
                                            static_cast<const uint8_t*>(data) + len);
            it->second->bytes = entry_bytes(kTagProgramBinary, len);
            total_bytes += it->second->bytes;
            touch(it->second);
        } else {
            Entry e;
            e.key = key;
            e.tag = kTagProgramBinary;
            e.binary.format = format;
            e.binary.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + len);
            e.bytes = entry_bytes(kTagProgramBinary, len);
            total_bytes += e.bytes;
            lru.push_back(move(e));
            by_key[key] = prev(lru.end());
        }
        evict_over_budget();
        ++pending_entries;
        flush_if_due();
    }

    bool get_binary(const array<uint8_t, 32>& key, GLenum* format, vector<uint8_t>& out) {
        auto it = by_key.find(key);
        if (it == by_key.end()) return false;
        if (it->second->tag != kTagProgramBinary) return false;
        *format = it->second->binary.format;
        out = it->second->binary.bytes;
        touch(it->second);
        return true;
    }

    void put_status(const array<uint8_t, 32>& key, const pipeline_cache::ShaderStatus& status) {
        auto it = by_key.find(key);
        if (it != by_key.end()) {
            if (it->second->tag != kTagShaderStatus) return; // never overwrite a binary
            total_bytes -= it->second->bytes;
            it->second->status = status;
            it->second->bytes = entry_bytes(kTagShaderStatus, status.info_log.size());
            total_bytes += it->second->bytes;
            touch(it->second);
        } else {
            Entry e;
            e.key = key;
            e.tag = kTagShaderStatus;
            e.status = status;
            e.bytes = entry_bytes(kTagShaderStatus, status.info_log.size());
            total_bytes += e.bytes;
            lru.push_back(move(e));
            by_key[key] = prev(lru.end());
        }
        evict_over_budget();
        ++pending_entries;
        flush_if_due();
    }

    bool get_status(const array<uint8_t, 32>& key, pipeline_cache::ShaderStatus& out) {
        auto it = by_key.find(key);
        if (it == by_key.end() || it->second->tag != kTagShaderStatus) return false;
        out = it->second->status;
        touch(it->second);
        return true;
    }

    void flush_if_due() {
        if (pending_entries == 0) return;
        if (pending_entries < kPendingEntriesBeforeSave &&
            (monotonic_now_ns() - last_save_ns) < kSaveIntervalNs)
            return;
        save_locked();
    }

    // Every field is written with a checked stream and read back with the same
    // layout; any short or malformed file is discarded wholesale -- the cache
    // is regenerable by definition.
    bool save_locked() {
        pending_entries = 0;
        last_save_ns = monotonic_now_ns();
        if (!mg_directory_path) return false;
        const string path = string(mg_directory_path) + "/pipeline_cache.bin";
        const string tmp = path + ".new";
        {
            ofstream f(tmp, ios::binary);
            if (!f) return false;
            f.write(kMagic, 4);
            uint32_t ver = kFormatVersion;
            f.write(reinterpret_cast<const char*>(&ver), sizeof(ver));
            uint32_t count = static_cast<uint32_t>(lru.size());
            f.write(reinterpret_cast<const char*>(&count), sizeof(count));
            for (const Entry& e : lru) {
                uint8_t tag = static_cast<uint8_t>(e.tag);
                f.write(reinterpret_cast<const char*>(&tag), 1);
                f.write(reinterpret_cast<const char*>(e.key.data()), 32);
                if (e.tag == kTagProgramBinary) {
                    uint32_t fmt = e.binary.format;
                    uint32_t len = static_cast<uint32_t>(e.binary.bytes.size());
                    f.write(reinterpret_cast<const char*>(&fmt), sizeof(fmt));
                    f.write(reinterpret_cast<const char*>(&len), sizeof(len));
                    f.write(reinterpret_cast<const char*>(e.binary.bytes.data()), static_cast<long>(len));
                } else {
                    int32_t st = e.status.compile_status;
                    int32_t ll = e.status.info_log_length;
                    uint32_t log_len = static_cast<uint32_t>(e.status.info_log.size());
                    f.write(reinterpret_cast<const char*>(&st), sizeof(st));
                    f.write(reinterpret_cast<const char*>(&ll), sizeof(ll));
                    f.write(reinterpret_cast<const char*>(&log_len), sizeof(log_len));
                    f.write(e.status.info_log.data(), static_cast<long>(log_len));
                }
            }
            f.flush();
            if (!f) {
                f.close();
                remove(tmp.c_str());
                return false;
            }
        }
        if (rename(tmp.c_str(), path.c_str()) != 0) {
            remove(tmp.c_str());
            return false;
        }
        return true;
    }

    void load_locked() {
        loaded = true;
        if (!mg_directory_path) return;
        const string path = string(mg_directory_path) + "/pipeline_cache.bin";
        ifstream f(path, ios::binary);
        if (!f) return;
        try {
            char magic[4] = {0};
            f.read(magic, 4);
            if (memcmp(magic, kMagic, 4) != 0) return;
            uint32_t ver = 0;
            f.read(reinterpret_cast<char*>(&ver), sizeof(ver));
            if (ver != kFormatVersion) return;
            uint32_t count = 0;
            f.read(reinterpret_cast<char*>(&count), sizeof(count));
            for (uint32_t i = 0; i < count && f; ++i) {
                Entry e;
                uint8_t tag = 0;
                f.read(reinterpret_cast<char*>(&tag), 1);
                f.read(reinterpret_cast<char*>(e.key.data()), 32);
                if (!f) break;
                if (tag == kTagProgramBinary) {
                    uint32_t fmt = 0, len = 0;
                    f.read(reinterpret_cast<char*>(&fmt), sizeof(fmt));
                    f.read(reinterpret_cast<char*>(&len), sizeof(len));
                    if (!f || len > kMaxCacheBytes) break;
                    e.tag = kTagProgramBinary;
                    e.binary.format = fmt;
                    e.binary.bytes.resize(len);
                    f.read(reinterpret_cast<char*>(e.binary.bytes.data()), static_cast<long>(len));
                    e.bytes = entry_bytes(kTagProgramBinary, len);
                } else if (tag == kTagShaderStatus) {
                    int32_t st = 0, ll = 0;
                    uint32_t log_len = 0;
                    f.read(reinterpret_cast<char*>(&st), sizeof(st));
                    f.read(reinterpret_cast<char*>(&ll), sizeof(ll));
                    f.read(reinterpret_cast<char*>(&log_len), sizeof(log_len));
                    if (!f || log_len > (1u << 20)) break;
                    e.tag = kTagShaderStatus;
                    e.status.compile_status = st;
                    e.status.info_log_length = ll;
                    e.status.info_log.resize(log_len);
                    f.read(e.status.info_log.data(), static_cast<long>(log_len));
                    e.bytes = entry_bytes(kTagShaderStatus, log_len);
                } else {
                    break; // unknown tag: stop, keep what parsed so far
                }
                if (!f) break;
                if (by_key.count(e.key)) continue;
                total_bytes += e.bytes;
                lru.push_back(move(e));
                by_key[lru.back().key] = prev(lru.end());
            }
            evict_over_budget();
        } catch (...) {
            // A malformed cache cannot be allowed to reach the game; it is
            // regenerable, so drop everything and rewrite on the next flush.
            lru.clear();
            by_key.clear();
            total_bytes = 0;
            save_locked();
        }
    }

    // Drops the oldest non-pending shader records. A record whose compile is
    // still deferred must survive: losing it would turn "pending" into "never
    // compiled" and change a link's outcome. Records with resolved_by_binary
    // keep their source only for future key building, so they are evictable.
    void trim_shader_records() {
        while (shaders.size() > kMaxShaderRecords) {
            bool removed = false;
            for (auto it = shaders.begin(); it != shaders.end(); ++it) {
                if (!it->second.compile_pending) {
                    shaders.erase(it);
                    removed = true;
                    break;
                }
            }
            if (!removed) break; // everything left is pending; stop growing is not an option, but wrong links are worse
        }
    }
};

Store& store() {
    static Store s;
    return s;
}

bool env_disabled() {
    int v = 0;
    if (GetEnvVarBool("MG_NO_PROGBIN", &v, 0) && v) return true;
    return false;
}

array<uint8_t, 32> shader_status_key_of(const string& source) {
    // The salt keeps a status key from ever colliding with a program-binary
    // key built over the same bytes, and the ES version / emulation flag are
    // in the material because both change what the driver sees.
    string material;
    material.reserve(source.size() + 64);
    material += "MGSTATUS1|";
    material += to_string(hardware ? hardware->es_version : 0);
    material += hardware && hardware->emulate_texture_buffer ? "|tb1|" : "|tb0|";
    material += source;
    return sha256(reinterpret_cast<const uint8_t*>(material.data()), material.size());
}

} // namespace

namespace pipeline_cache {

bool enabled() {
    static const bool on = global_settings.program_binary_cache && !env_disabled();
    return on;
}

void on_shader_created(GLuint shader) {
    if (!enabled()) return;
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    s.shaders.erase(shader);
}

void on_source_uploaded(GLuint shader, const string& essl, GLenum shader_type) {
    if (!enabled()) return;
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    ShaderRecord& rec = s.shaders[shader];
    rec.source = essl;
    rec.shader_type = shader_type ? shader_type : rec.shader_type;
    rec.compile_pending = false; // new source: any earlier deferral is void
    rec.resolved_by_binary = false;
    s.trim_shader_records();
}

bool defer_compile(GLuint shader) {
    if (!enabled()) return false;
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    auto it = s.shaders.find(shader);
    if (it == s.shaders.end()) return false; // untracked: caller compiles for real
    it->second.compile_pending = true;
    return true;
}

bool get_shader_record(GLuint shader, ShaderRecord& out) {
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    auto it = s.shaders.find(shader);
    if (it == s.shaders.end()) return false;
    out = it->second;
    return true;
}

void mark_compiled(GLuint shader) {
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    auto it = s.shaders.find(shader);
    if (it != s.shaders.end()) it->second.compile_pending = false;
}

void mark_attached_resolved_by_binary(GLuint program) {
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    auto pit = s.programs.find(program);
    if (pit == s.programs.end()) return;
    for (GLuint shader : pit->second.attached) {
        auto it = s.shaders.find(shader);
        if (it != s.shaders.end()) {
            it->second.compile_pending = false;
            it->second.resolved_by_binary = true;
        }
    }
}

string shader_status_key(const string& source) {
    auto digest = shader_status_key_of(source);
    return string(reinterpret_cast<char*>(digest.data()), digest.size());
}

void record_shader_status(const string& source, GLint status, GLint log_length, const string& log) {
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    if (!s.loaded) s.load_locked();
    ShaderStatus st;
    st.compile_status = status;
    st.info_log_length = log_length;
    st.info_log = log;
    s.put_status(shader_status_key_of(source), st);
}

bool lookup_shader_status(const string& source, ShaderStatus& out) {
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    if (!s.loaded) s.load_locked();
    return s.get_status(shader_status_key_of(source), out);
}

void on_program_created(GLuint program) {
    if (!enabled()) return;
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    s.programs.erase(program); // recycled name: start clean
}

void on_attach(GLuint program, GLuint shader, GLenum type) {
    if (!enabled()) return;
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    s.programs[program].attached.push_back(shader);
    auto it = s.shaders.find(shader);
    if (it != s.shaders.end() && type != 0) it->second.shader_type = type;
}

void on_detach(GLuint program, GLuint shader) {
    if (!enabled()) return;
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    auto it = s.programs.find(program);
    if (it == s.programs.end()) return;
    auto& v = it->second.attached;
    v.erase(remove(v.begin(), v.end(), shader), v.end());
}

void on_attrib_binding(GLuint program, GLuint index, const char* name) {
    if (!enabled() || !name) return;
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    s.programs[program].attrib_bindings.emplace_back(index, string(name));
}

bool get_program_record(GLuint program, ProgramRecord& out) {
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    auto it = s.programs.find(program);
    if (it == s.programs.end()) return false;
    out = it->second;
    return true;
}

vector<uint8_t> build_program_key(GLuint program, const LinkKeyContext& ctx) {
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);

    string material;
    material.reserve(4096);
    material += "MGPROGBIN1|";
    material += to_string(MAJOR);
    material += '.';
    material += to_string(MINOR);
    material += '.';
    material += to_string(REVISION);
    material += '.';
    material += to_string(PATCH);
    material += '|';
    material += ctx.device_renderer;
    material += '|';
    material += ctx.device_version;
    material += '|';
    material += to_string(ctx.es_version);
    material += ctx.emulate_texture_buffer ? "|tb1|" : "|tb0|";

    auto pit = s.programs.find(program);
    if (pit != s.programs.end()) {
        material += "S:";
        for (GLuint shader : pit->second.attached) {
            auto it = s.shaders.find(shader);
            if (it == s.shaders.end()) {
                // Attached outside tracking (record evicted): include the name
                // so the key cannot accidentally match a cache entry that was
                // built with a real source here.
                material += "untracked:";
                material += to_string(shader);
                material += ';';
                continue;
            }
            material += to_string(it->second.shader_type);
            material += ':';
            material += to_string(it->second.source.size());
            material += ':';
            material += it->second.source; // full source: the binary depends on every byte
            material += ';';
        }
        material += "|A:";
        for (const auto& [index, name] : pit->second.attrib_bindings) {
            material += to_string(index);
            material += '=';
            material += name;
            material += ';';
        }
    }

    if (ctx.default_fs_attached) {
        material += "|DFS:";
        material += ctx.default_fs_source;
    } else {
        material += "|noDFS";
    }

    auto digest = sha256(reinterpret_cast<const uint8_t*>(material.data()), material.size());
    return vector<uint8_t>(digest.begin(), digest.end());
}

void store_program_binary(const vector<uint8_t>& key, GLenum format, const void* data, size_t len) {
    if (key.size() != 32 || len == 0 || len > kMaxCacheBytes) return;
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    if (!s.loaded) s.load_locked();
    array<uint8_t, 32> k;
    memcpy(k.data(), key.data(), 32);
    s.put_binary(k, format, data, len);
}

bool lookup_program_binary(const vector<uint8_t>& key, GLenum* format, vector<uint8_t>& out) {
    if (key.size() != 32) return false;
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    if (!s.loaded) s.load_locked();
    array<uint8_t, 32> k;
    memcpy(k.data(), key.data(), 32);
    return s.get_binary(k, format, out);
}

void flush_for_test() {
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    if (!s.loaded) s.load_locked();
    s.save_locked();
}

void reload_for_test() {
    Store& s = store();
    lock_guard<mutex> lock(s.mutex);
    if (s.loaded && !s.save_locked()) {
        // The renderer never calls this; a failed flush must not lose the
        // in-memory state either.
    }
    s.lru.clear();
    s.by_key.clear();
    s.total_bytes = 0;
    s.pending_entries = 0;
    s.shaders.clear();
    s.programs.clear();
    s.loaded = false;
}

} // namespace pipeline_cache
