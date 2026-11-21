// MobileGlues-RS - tools/rsdiff/rsdiff.cpp
// Differential equivalence & benchmark harness (host build, runs in CI).
//
// Compares the Rust text-processing core (libmobileglues_rs.a, host build)
// against the ORIGINAL std::regex implementations copied verbatim from
// gl/glsl/glsl_for_es.cpp, byte-for-byte, over the corpus in corpus/.
// It also times both implementations on the same inputs and prints the
// measured numbers that go into the release changelog.
//
// Build & run (as CI does):
//   cargo build --release                       # host staticlib
//   g++ -O2 -std=c++20 -o rsdiff rsdiff.cpp \
//       <repo>/MobileGlues/MobileGlues-cpp/rust/target/release/libmobileglues_rs.a \
//       -lpthread -ldl
//   ./rsdiff corpus
//
// Exit code 0 = every function byte-identical on every corpus file.
// Exit code 1 = mismatch (CI must fail the build in that case).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

// --------------------------------------------------------------------------
// Rust FFI (mirrors rust/src/lib.rs)
// --------------------------------------------------------------------------
extern "C" {
int mg_rs_available();
const char* mg_rs_version();
void mg_rs_free(char* ptr);
int mg_rs_get_glsl_version(const char* src);
char* mg_rs_remove_layout_binding(const char* src);
char* mg_rs_process_out_color_locations(const char* src);
char* mg_rs_force_supported_output(const char* src);
char* mg_rs_process_sampler_buffer_rewrite(const char* src);
long long mg_rs_find_uniform_insert_pos(const char* src);
int mg_rs_has_texture_query_lod_def(const char* src);
int mg_rs_has_temporal_filter_def(const char* src);
}

// --------------------------------------------------------------------------
// ORIGINAL C++ implementations — copied verbatim from
// gl/glsl/glsl_for_es.cpp (upstream MobileGlues, LGPL-2.1)
// --------------------------------------------------------------------------
static int cpp_getGLSLVersion(const char* glsl_code) {
    std::string code(glsl_code);
    static std::regex version_pattern(R"(#version\s+(\d{3}))");
    std::smatch match;
    if (std::regex_search(code, match, version_pattern)) {
        return std::stoi(match[1].str());
    }
    return -1;
}

static std::string cpp_removeLayoutBinding(const std::string& glslCode) {
    static std::regex bindingRegex(R"(layout\s*\(\s*binding\s*=\s*\d+\s*\)\s*)");
    std::string result = std::regex_replace(glslCode, bindingRegex, "");
    static std::regex bindingRegex2(R"(layout\s*\(\s*binding\s*=\s*\d+\s*,)");
    result = std::regex_replace(result, bindingRegex2, "layout(");
    return result;
}

static std::string cpp_processOutColorLocations(const std::string& glslCode) {
    const static std::regex pattern(R"(\n(out highp vec4 outColor)(\d+);)");
    const std::string replacement = "\nlayout(location=$2) $1$2;";
    return std::regex_replace(glslCode, pattern, replacement);
}

static std::string cpp_forceSupporterOutput(const std::string& glslCode) {
    bool hasPrecisionFloat =
        glslCode.find("precision ") != std::string::npos && glslCode.find("float;") != std::string::npos;
    bool hasPrecisionInt =
        glslCode.find("precision ") != std::string::npos && glslCode.find("int;") != std::string::npos;

    std::string result = glslCode;
    std::string precisionFloat;
    std::string precisionInt;

    if (hasPrecisionFloat && hasPrecisionInt) {
        std::istringstream iss(result);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(iss, line)) {
            bool isPrecisionLine = (line.find("precision ") != std::string::npos) &&
                                   (line.find("float;") != std::string::npos || line.find("int;") != std::string::npos);
            if (!isPrecisionLine) {
                lines.push_back(line);
            }
        }
        result.clear();
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i != 0) result += '\n';
            result += lines[i];
        }
        precisionFloat = "precision highp float;\n";
        precisionInt = "precision highp int;\n";
    } else {
        precisionFloat = hasPrecisionFloat ? "" : "precision highp float;\n";
        precisionInt = hasPrecisionInt ? "" : "precision highp int;\n";
    }

    size_t lastExtensionPos = result.rfind("#extension");
    size_t insertionPos = 0;

    if (lastExtensionPos != std::string::npos) {
        size_t nextNewline = result.find('\n', lastExtensionPos);
        if (nextNewline != std::string::npos) {
            insertionPos = nextNewline + 1;
        } else {
            insertionPos = result.length();
        }
    } else {
        size_t firstNewline = result.find('\n');
        if (firstNewline != std::string::npos) {
            insertionPos = firstNewline + 1;
        } else {
            result = precisionFloat + precisionInt + result;
            return result;
        }
    }

    result.insert(insertionPos, precisionFloat + precisionInt);
    return result;
}

static std::string cpp_process_sampler_buffer_rewrite(const std::string& input) {
    std::string source = input;
    std::regex pattern(R"(texelFetch\s*\(\s*(\w+)\s*,\s*([^)]+?)\s*\))");
    source = std::regex_replace(source, pattern,
                                "texelFetch($1, ivec2(($2) % u_BufferTexWidth, ($2) / u_BufferTexWidth), 0)");
    source = std::regex_replace(source, std::regex("texelFetch\\((\\w+)\\s*,\\s*ivec2\\(([^)]+)\\)\\s*,\\s*0\\)"),
                                "texelFetch($1, bufferCoords($2), 0)");
    return source;
}

static size_t cpp_find_uniform_insert_pos(const std::string& glsl) {
    const std::regex uniformRegex(
        R"(^\s*(?:layout\s*\([^)]*\)\s*)?uniform\s+\w+(?:\s*\[\s*\d+\s*\])?\s+\w+(?:\s*\[\s*\d+\s*\])?\s*;.*$)",
        std::regex::ECMAScript | std::regex::multiline);
    std::sregex_iterator it(glsl.begin(), glsl.end(), uniformRegex);
    std::sregex_iterator end;
    size_t insertPos = 0;
    for (; it != end; ++it) {
        insertPos = it->position() + it->length();
    }
    return insertPos;
}

static bool cpp_has_texture_query_lod_def(const std::string& glsl) {
    const std::regex defRegex(R"(vec2\s+mg_textureQueryLod\s*\()", std::regex::ECMAScript);
    return std::regex_search(glsl, defRegex);
}

static bool cpp_has_temporal_filter_def(const std::string& glsl) {
    const std::regex defRegex(R"(vec4\s+GI_TemporalFilter\s*\()", std::regex::ECMAScript);
    return std::regex_search(glsl, defRegex);
}

// --------------------------------------------------------------------------
// helpers
// --------------------------------------------------------------------------
static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct RustStr {
    char* p;
    explicit RustStr(char* q) : p(q) {}
    ~RustStr() { if (p) mg_rs_free(p); }
    RustStr(const RustStr&) = delete;
    RustStr& operator=(const RustStr&) = delete;
    bool ok() const { return p != nullptr; }
    std::string str() const { return p ? std::string(p) : std::string(); }
};

using Clock = std::chrono::steady_clock;

// Runs `fn` repeatedly until >= min_ms, returns ns per iteration.
// Simple doubling: always makes progress, capped at 2^20 iterations.
template <typename F>
static long long bench(F&& fn, double min_ms) {
    fn(); // warmup
    const long long target_ns = (long long)(min_ms * 1e6);
    long long iters = 1;
    long long ns = 0;
    for (;;) {
        auto t0 = Clock::now();
        for (long long i = 0; i < iters; ++i) fn();
        ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
        if (ns >= target_ns || iters >= (1LL << 20)) break;
        iters *= 2;
    }
    return ns / iters;
}

int main(int argc, char** argv) {
    const std::string corpus_dir = argc > 1 ? argv[1] : "corpus";
    if (mg_rs_available() != 1) {
        printf("FATAL: Rust core not linked\n");
        return 2;
    }
    printf("rust core: %s\n", mg_rs_version());

    std::vector<std::string> files;
    DIR* d = opendir(corpus_dir.c_str());
    if (!d) {
        printf("FATAL: cannot open corpus dir %s\n", corpus_dir.c_str());
        return 2;
    }
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".glsl") == 0) files.push_back(corpus_dir + "/" + n);
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        printf("FATAL: no .glsl corpus files\n");
        return 2;
    }

    int failures = 0;
    struct OpStats { long long cpp_ns = 0, rust_ns = 0; };
    OpStats st_version, st_layout, st_outcolor, st_precision, st_texel, st_uniform, st_lod, st_gi;

    for (const auto& path : files) {
        std::string src = read_file(path);
        if (src.empty()) { printf("FATAL: empty/missing %s\n", path.c_str()); return 2; }
        const char* name = strrchr(path.c_str(), '/') + 1;
        bool file_ok = true;

        // ---- equivalence checks (every function, every file) ----
        int cpp_v = cpp_getGLSLVersion(src.c_str());
        int rust_v = mg_rs_get_glsl_version(src.c_str());
        if (rust_v < -1 || rust_v != cpp_v) { printf("MISMATCH version %s: cpp=%d rust=%d\n", name, cpp_v, rust_v); file_ok = false; }

        std::string cpp_s, rust_s;
        cpp_s = cpp_removeLayoutBinding(src);
        { RustStr r(mg_rs_remove_layout_binding(src.c_str())); rust_s = r.str(); }
        if (cpp_s != rust_s) { printf("MISMATCH removeLayoutBinding %s\n", name); file_ok = false; }

        cpp_s = cpp_processOutColorLocations(src);
        { RustStr r(mg_rs_process_out_color_locations(src.c_str())); rust_s = r.str(); }
        if (cpp_s != rust_s) { printf("MISMATCH processOutColorLocations %s\n", name); file_ok = false; }

        cpp_s = cpp_forceSupporterOutput(src);
        { RustStr r(mg_rs_force_supported_output(src.c_str())); rust_s = r.str(); }
        if (cpp_s != rust_s) { printf("MISMATCH forceSupporterOutput %s\n", name); file_ok = false; }

        cpp_s = cpp_process_sampler_buffer_rewrite(src);
        { RustStr r(mg_rs_process_sampler_buffer_rewrite(src.c_str())); rust_s = r.str(); }
        if (cpp_s != rust_s) { printf("MISMATCH process_sampler_buffer %s\n", name); file_ok = false; }

        size_t cpp_pos = cpp_find_uniform_insert_pos(src);
        long long rust_pos = mg_rs_find_uniform_insert_pos(src.c_str());
        if (rust_pos < 0 || (size_t)rust_pos != cpp_pos) { printf("MISMATCH find_uniform_insert_pos %s: cpp=%zu rust=%lld\n", name, cpp_pos, rust_pos); file_ok = false; }

        bool cpp_b = cpp_has_texture_query_lod_def(src);
        int rust_b = mg_rs_has_texture_query_lod_def(src.c_str());
        if (rust_b < 0 || (rust_b != 0) != cpp_b) { printf("MISMATCH has_texture_query_lod_def %s\n", name); file_ok = false; }

        cpp_b = cpp_has_temporal_filter_def(src);
        rust_b = mg_rs_has_temporal_filter_def(src.c_str());
        if (rust_b < 0 || (rust_b != 0) != cpp_b) { printf("MISMATCH has_temporal_filter_def %s\n", name); file_ok = false; }

        if (!file_ok) { failures++; continue; }

        // ---- benchmarks (only on verified-identical paths) ----
        const double min_ms = 25.0;
        st_version.cpp_ns   += bench([&] { cpp_getGLSLVersion(src.c_str()); }, min_ms);
        st_version.rust_ns  += bench([&] { mg_rs_get_glsl_version(src.c_str()); }, min_ms);
        st_layout.cpp_ns    += bench([&] { cpp_removeLayoutBinding(src); }, min_ms);
        st_layout.rust_ns   += bench([&] { char* p = mg_rs_remove_layout_binding(src.c_str()); mg_rs_free(p); }, min_ms);
        st_outcolor.cpp_ns  += bench([&] { cpp_processOutColorLocations(src); }, min_ms);
        st_outcolor.rust_ns += bench([&] { char* p = mg_rs_process_out_color_locations(src.c_str()); mg_rs_free(p); }, min_ms);
        st_precision.cpp_ns += bench([&] { cpp_forceSupporterOutput(src); }, min_ms);
        st_precision.rust_ns+= bench([&] { char* p = mg_rs_force_supported_output(src.c_str()); mg_rs_free(p); }, min_ms);
        st_texel.cpp_ns     += bench([&] { cpp_process_sampler_buffer_rewrite(src); }, min_ms);
        st_texel.rust_ns    += bench([&] { char* p = mg_rs_process_sampler_buffer_rewrite(src.c_str()); mg_rs_free(p); }, min_ms);
        st_uniform.cpp_ns   += bench([&] { cpp_find_uniform_insert_pos(src); }, min_ms);
        st_uniform.rust_ns  += bench([&] { mg_rs_find_uniform_insert_pos(src.c_str()); }, min_ms);
        st_lod.cpp_ns       += bench([&] { cpp_has_texture_query_lod_def(src); }, min_ms);
        st_lod.rust_ns      += bench([&] { mg_rs_has_texture_query_lod_def(src.c_str()); }, min_ms);
        st_gi.cpp_ns        += bench([&] { cpp_has_temporal_filter_def(src); }, min_ms);
        st_gi.rust_ns       += bench([&] { mg_rs_has_temporal_filter_def(src.c_str()); }, min_ms);
    }

    printf("\n=== EQUIVALENCE: %s (%d/%zu files byte-identical on all 8 functions) ===\n",
           failures == 0 ? "PASS" : "FAIL", (int)files.size() - failures, files.size());
    printf("\n=== BENCHMARK (sum over corpus, ns/call, lower is better) ===\n");
    auto row = [](const char* n, const OpStats& s) {
        if (s.rust_ns <= 0) return;
        printf("  %-28s cpp %9lld ns   rust %8lld ns   speedup %6.2fx\n", n, s.cpp_ns, s.rust_ns,
               (double)s.cpp_ns / (double)s.rust_ns);
    };
    row("getGLSLVersion", st_version);
    row("removeLayoutBinding", st_layout);
    row("processOutColorLocations", st_outcolor);
    row("forceSupporterOutput", st_precision);
    row("process_sampler_buffer", st_texel);
    row("find_uniform_insert_pos", st_uniform);
    row("has_texture_query_lod_def", st_lod);
    row("has_temporal_filter_def", st_gi);

    return failures == 0 ? 0 : 1;
}
