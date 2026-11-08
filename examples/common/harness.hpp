#pragma once
// Shared scaffolding for Prism example games: a strict command-line parser plus a runner that
// handles the windowed loop, deterministic headless runs (screenshots, frame hash, pass/fail
// checks) and a built-in determinism self-check. A game supplies a GameSpec and nothing else:
//
//     int main(int argc, char** argv) {
//         int expect = -1;
//         harness::GameSpec spec;
//         spec.binary = "my_game";
//         spec.title = "My game";
//         spec.add_options = [&](harness::Cli& cli) { cli.integer("--expect-score", &expect, "N", "fail below N"); };
//         spec.make_scene = [&](const harness::CommonOptions& o) { return std::make_unique<MyScene>(o.autopilot); };
//         return harness::run_game(argc, argv, spec);
//     }
//
// Common flags every game gets:  --headless  --autopilot  --frames N  --shot F PATH  --press F KEY
//                                --size WxH  --seed N  --colliders  --stats  --verify-determinism  --help
//
// Exit codes: 0 ok | 1 fatal error | 2 the game's own check failed | 3 run was not deterministic
//             64 bad command line
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "prism/prism.hpp"

namespace prism::harness {

// ───────────────────────────── command line ─────────────────────────────

// Strict integer parsing: the whole string must be a number inside [lo, hi]. (atoi() would turn
// "--frames abc" into 0 and carry on, silently running the wrong thing.)
inline bool parse_int(const std::string& s, long long lo, long long hi, long long& out, std::string* err = nullptr) {
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    if (s.empty()) return fail("expected a number, got an empty value");
    errno = 0;
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return fail("'" + s + "' is not an integer");
    if (errno == ERANGE || v < lo || v > hi)
        return fail("'" + s + "' is out of range (" + std::to_string(lo) + ".." + std::to_string(hi) + ")");
    out = v;
    return true;
}

// "a", "P", "space", "Enter", "left", "f1"... (case-insensitive). Key::Unknown if not a key.
inline Key key_from_name(std::string name) {
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const std::pair<const char*, Key> named[] = {
        {"left", Key::Left},       {"right", Key::Right},   {"up", Key::Up},         {"down", Key::Down},
        {"space", Key::Space},     {"enter", Key::Enter},   {"return", Key::Enter},  {"escape", Key::Escape},
        {"esc", Key::Escape},      {"tab", Key::Tab},       {"backspace", Key::Backspace}, {"lshift", Key::LShift},
        {"rshift", Key::RShift},   {"shift", Key::LShift},  {"lctrl", Key::LCtrl},   {"rctrl", Key::RCtrl},
        {"ctrl", Key::LCtrl},
    };
    for (const auto& n : named)
        if (name == n.first) return n.second;
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z')
        return static_cast<Key>(static_cast<int>(Key::A) + (name[0] - 'a'));
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9')
        return static_cast<Key>(static_cast<int>(Key::Num0) + (name[0] - '0'));
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'f') {
        long long n = 0;
        if (parse_int(name.substr(1), 1, 12, n)) return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(n) - 1);
    }
    return Key::Unknown;
}

class Cli {
public:
    // Handler gets the option's arguments and returns an error message, or "" on success.
    using Handler = std::function<std::string(const std::vector<std::string>&)>;

    void custom(std::string name, int nargs, std::string meta, std::string help, Handler fn) {
        opts_.push_back({std::move(name), nargs, std::move(meta), std::move(help), std::move(fn)});
    }
    void flag(const std::string& name, bool* out, const std::string& help) {
        custom(name, 0, "", help, [out](const std::vector<std::string>&) {
            *out = true;
            return std::string();
        });
    }
    void integer(const std::string& name, int* out, const std::string& meta, const std::string& help, int lo = INT_MIN,
                 int hi = INT_MAX) {
        custom(name, 1, meta, help, [out, lo, hi](const std::vector<std::string>& a) {
            long long v = 0;
            std::string err;
            if (!parse_int(a[0], lo, hi, v, &err)) return err;
            *out = static_cast<int>(v);
            return std::string();
        });
    }
    void unsigned64(const std::string& name, uint64_t* out, const std::string& meta, const std::string& help) {
        custom(name, 1, meta, help, [out](const std::vector<std::string>& a) {
            long long v = 0;
            std::string err;
            if (!parse_int(a[0], 0, LLONG_MAX, v, &err)) return err;
            *out = static_cast<uint64_t>(v);
            return std::string();
        });
    }
    void text(const std::string& name, std::string* out, const std::string& meta, const std::string& help) {
        custom(name, 1, meta, help, [out](const std::vector<std::string>& a) {
            *out = a[0];
            return std::string();
        });
    }

    // Returns "" on success, otherwise a one-line description of what is wrong.
    std::string parse(const std::vector<std::string>& args) const {
        for (size_t i = 0; i < args.size(); ++i) {
            const Opt* o = find(args[i]);
            if (!o) return "unknown option '" + args[i] + "'";
            const size_t n = static_cast<size_t>(o->nargs);
            if (i + n >= args.size() && n > 0)  // the n values must exist at args[i+1 .. i+n]
                return "option '" + o->name + "' needs " + std::to_string(n) + " value" + (n > 1 ? "s" : "") +
                       (o->meta.empty() ? "" : " (" + o->meta + ")");
            std::vector<std::string> a(args.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                                       args.begin() + static_cast<std::ptrdiff_t>(i + n) + 1);
            std::string err = o->apply(a);
            if (!err.empty()) return o->name + ": " + err;
            i += n;
        }
        return "";
    }

    std::string usage(const std::string& program) const {
        std::string out = program + " [options]\n";
        size_t width = 0;
        auto left = [](const Opt& o) { return o.name + (o.meta.empty() ? "" : " " + o.meta); };
        for (const Opt& o : opts_) width = std::max(width, left(o).size());
        for (const Opt& o : opts_) {
            std::string l = left(o);
            out += "  " + l + std::string(width - l.size() + 2, ' ') + o.help + "\n";
        }
        return out;
    }

    bool has(const std::string& name) const { return find(name) != nullptr; }

private:
    struct Opt {
        std::string name;
        int nargs;
        std::string meta, help;
        Handler apply;
    };
    const Opt* find(const std::string& name) const {
        for (const Opt& o : opts_)
            if (o.name == name) return &o;
        return nullptr;
    }
    std::vector<Opt> opts_;
};

// ───────────────────────────── game description ─────────────────────────────

struct CommonOptions {
    bool headless = false, autopilot = false, debug_colliders = false, debug_overlay = false;
    bool verify_determinism = false, print_hash = false, help = false;
    int frames = 0;  // 0: the game's default
    int width = 1280, height = 720;
    uint64_t seed = 1;
    std::vector<std::pair<int, std::string>> shots;  // frame -> PNG path
    std::vector<std::pair<int, Key>> presses;        // frame -> key tapped (headless scripted input)
};

struct GameSpec {
    std::string binary = "game";  // shown in --help
    std::string title = "Prism";  // window title
    std::string tag = "game";     // prefix of the headless result line
    int default_frames = 900;

    std::function<void(Cli&)> add_options;                                   // game-specific flags
    std::function<std::unique_ptr<Scene>(const CommonOptions&)> make_scene;  // required; called once per run
    std::function<void(EngineConfig&, const CommonOptions&)> configure;      // tweak the EngineConfig (optional)
    std::function<std::string(Engine&)> summary;                             // text for the result line (optional)
    std::function<int(Engine&)> check;                                       // 0 = pass; nonzero => exit code 2
};

// ───────────────────────────── runner ─────────────────────────────

namespace detail {

struct RunResult {
    uint64_t hash = 0;
    std::string summary;
    int check = 0;
    int unreached_shots = 0;
};

inline EngineConfig make_config(const GameSpec& spec, const CommonOptions& o, bool headless) {
    EngineConfig cfg;
    cfg.title = spec.title;
    cfg.width = o.width;
    cfg.height = o.height;
    cfg.backend = headless ? Backend::Software : Backend::Auto;
    cfg.vsync = !headless;
    cfg.seed = o.seed;
    if (spec.configure) spec.configure(cfg, o);
    return cfg;
}

// One complete deterministic headless run in a fresh engine: exactly one fixed step per frame.
inline RunResult run_headless(const GameSpec& spec, const CommonOptions& o, int frames, bool with_shots) {
    Engine engine(make_config(spec, o, true));
    engine.set_scene(spec.make_scene(o));
    engine.debug_colliders = o.debug_colliders;
    engine.debug_overlay = o.debug_overlay;

    if (!o.presses.empty()) {
        std::vector<ScriptedKey> script;
        for (const auto& p : o.presses) {  // a tap: down, then up three frames later
            script.push_back({static_cast<uint64_t>(p.first), p.second, true});
            script.push_back({static_cast<uint64_t>(p.first) + 3, p.second, false});
        }
        engine.set_input_script(std::move(script));
    }

    std::vector<bool> fired(o.shots.size(), false);
    for (int f = 0; f < frames; ++f) {
        if (with_shots)
            for (size_t i = 0; i < o.shots.size(); ++i)
                if (o.shots[i].first == f) {
                    engine.request_screenshot(o.shots[i].second);
                    fired[i] = true;
                }
        engine.step(0);
    }
    RunResult r;
    Image last;
    engine.capture_frame(last);
    r.hash = last.hash();
    if (spec.summary) r.summary = spec.summary(engine);
    if (spec.check) r.check = spec.check(engine);
    if (with_shots)
        for (bool b : fired) r.unreached_shots += b ? 0 : 1;
    return r;
}

}  // namespace detail

inline int run_game(int argc, char** argv, const GameSpec& spec) {
    CommonOptions o;
    Cli cli;
    cli.flag("--headless", &o.headless, "software renderer, no window; runs --frames frames, then exits");
    cli.flag("--autopilot", &o.autopilot, "a bot plays the game");
    cli.integer("--frames", &o.frames, "N", "frames to simulate in headless mode (default " + std::to_string(spec.default_frames) + ")", 1, 10000000);
    cli.custom("--shot", 2, "F PATH", "save a PNG screenshot at frame F (repeatable)", [&o](const std::vector<std::string>& a) {
        long long f = 0;
        std::string err;
        if (!parse_int(a[0], 0, 100000000, f, &err)) return err;
        o.shots.emplace_back(static_cast<int>(f), a[1]);
        return std::string();
    });
    cli.custom("--press", 2, "F KEY", "tap KEY at frame F in headless mode, e.g. --press 300 p (repeatable)",
               [&o](const std::vector<std::string>& a) {
                   long long f = 0;
                   std::string err;
                   if (!parse_int(a[0], 0, 100000000, f, &err)) return err;
                   Key k = key_from_name(a[1]);
                   if (k == Key::Unknown) return "'" + a[1] + "' is not a key name (try a letter, space, enter, left, f1, ...)";
                   o.presses.emplace_back(static_cast<int>(f), k);
                   return std::string();
               });
    cli.custom("--size", 1, "WxH", "logical resolution (default 1280x720)", [&o](const std::vector<std::string>& a) {
        const std::string& s = a[0];
        size_t x = s.find('x');
        long long w = 0, h = 0;
        std::string err;
        if (x == std::string::npos) return std::string("expected WxH, e.g. 1280x720");
        if (!parse_int(s.substr(0, x), 64, 8192, w, &err) || !parse_int(s.substr(x + 1), 64, 8192, h, &err)) return err;
        o.width = static_cast<int>(w);
        o.height = static_cast<int>(h);
        return std::string();
    });
    cli.unsigned64("--seed", &o.seed, "N", "RNG seed (default 1)");
    cli.flag("--colliders", &o.debug_colliders, "start with the F2 collider overlay on");
    cli.flag("--stats", &o.debug_overlay, "start with the F1 stats overlay on");
    cli.flag("--hash", &o.print_hash, "accepted for compatibility (the frame hash is always printed)");
    cli.flag("--verify-determinism", &o.verify_determinism, "run twice from scratch and fail (exit 3) unless both final frames match");
    cli.flag("--help", &o.help, "show this message");
    if (spec.add_options) spec.add_options(cli);

    std::vector<std::string> args(argv + 1, argv + argc);
    std::string err = cli.parse(args);
    if (!err.empty()) {
        std::fprintf(stderr, "%s: %s\n\n%s", spec.binary.c_str(), err.c_str(), cli.usage(spec.binary).c_str());
        return 64;
    }
    if (o.help) {
        std::fputs(cli.usage(spec.binary).c_str(), stdout);
        return 0;
    }
    if (!spec.make_scene) {
        std::fprintf(stderr, "%s: GameSpec::make_scene is not set\n", spec.binary.c_str());
        return 1;
    }

    try {
        if (!o.headless && !sdl_backend_available()) {
            std::printf("[%s] this build has no SDL2 window backend; running the headless autopilot instead\n", spec.tag.c_str());
            o.headless = true;
            o.autopilot = true;
        }

        if (!o.headless) {
            Engine engine(detail::make_config(spec, o, false));
            engine.set_scene(spec.make_scene(o));
            engine.debug_colliders = o.debug_colliders;
            engine.debug_overlay = o.debug_overlay;
            engine.run();
            return 0;
        }

        const int frames = o.frames > 0 ? o.frames : spec.default_frames;
        detail::RunResult r = detail::run_headless(spec, o, frames, true);
        std::printf("[%s] %d frames%s%s, frame hash %016llx\n", spec.tag.c_str(), frames, r.summary.empty() ? "" : ", ",
                    r.summary.c_str(), static_cast<unsigned long long>(r.hash));
        if (r.unreached_shots > 0)
            std::fprintf(stderr, "[%s] warning: %d --shot frame(s) were beyond --frames %d and never fired\n",
                         spec.tag.c_str(), r.unreached_shots, frames);

        if (o.verify_determinism) {
            detail::RunResult again = detail::run_headless(spec, o, frames, false);
            if (again.hash != r.hash) {
                std::fprintf(stderr, "[%s] NOT DETERMINISTIC: frame hash %016llx vs %016llx on an identical rerun\n",
                             spec.tag.c_str(), static_cast<unsigned long long>(r.hash),
                             static_cast<unsigned long long>(again.hash));
                return 3;
            }
            std::printf("[%s] deterministic: two identical runs produced the same final frame\n", spec.tag.c_str());
        }
        if (r.check != 0) {
            std::fprintf(stderr, "[%s] FAILED (check returned %d)\n", spec.tag.c_str(), r.check);
            return 2;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Fatal: %s\n", ex.what());
        if (!o.headless) std::fprintf(stderr, "(no usable display? try --headless)\n");
        return 1;
    }
    return 0;
}

}  // namespace prism::harness
