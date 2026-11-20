#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS crash notice: what PushFPS tells the user at the next start after it stopped unexpectedly,
// and the summary they can copy to whoever helps them.
//
// The crash handler (apps/veyra-qml/main.cpp) leaves pushfps-last-failure.txt beside the log:
//   line 1  "crash <module file name>"        (or "device" for a lost graphics device)
//   line 2  known-risky injected components, comma separated
//   line 3  "code=0x... offset=0x... dump=<file name> time=<YYYY-MM-DD HH:MM>"   (since 3.2.2)
// and a crash file (.dmp) in the same folder. The crash file holds the Windows user name and folder
// paths, so it is never read or sent by PushFPS; the summary is built only from the three lines
// above, from facts about the PC that carry no personal data (PushFPS version, Windows build,
// graphics card) and from the session note below.
//
// The session note is one line the bridge rewrites while PushFPS runs (pushfps-last-session.txt): what
// kind of source was playing and which methods were on. It holds choices and numbers only: never a
// device name, a host, an address or a file name.
//
// Everything that goes into the summary passes safeField(), which keeps a short run of plain
// characters and drops whatever could be part of a path, so a value that is not what it should be
// cannot carry a user name out. Pure: no Windows or Qt types (tests/unit/PushCrashNoteTests.cpp).
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace veyra::engine::crashnote {

// Letters, digits, space and . _ - + ( ) only, at most `max` characters; empty when the value holds
// a path separator, a colon or anything else (a path, an address, a URL).
inline std::string safeField(std::string_view value, size_t max = 64) {
    std::string out;
    for (const char ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool plain = std::isalnum(c) || c == ' ' || c == '.' || c == '_' || c == '-' || c == '+' || c == '(' || c == ')';
        if (!plain) return {};
        if (out.size() < max) out.push_back(ch);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    size_t start = 0;
    while (start < out.size() && out[start] == ' ') ++start;
    return out.substr(start);
}

// For a graphics card name ("AMD Radeon RX 7900 XT/XTX", "Intel(R) Arc(TM)"): the same characters,
// anything else becomes a space. A card name is not personal; this only keeps the summary plain text.
inline std::string plainText(std::string_view value, size_t max = 64) {
    std::string out;
    for (const char ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool plain = std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == '+' || c == '(' || c == ')';
        if (plain) out.push_back(ch);
        else if (!out.empty() && out.back() != ' ') out.push_back(' ');
        if (out.size() >= max) break;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// The last path component, then safeField: "C:\Users\me\x\onnxruntime.dll" -> "onnxruntime.dll".
inline std::string safeFileName(std::string_view value, size_t max = 64) {
    const size_t cut = value.find_last_of("/\\");
    if (cut != std::string_view::npos) value.remove_prefix(cut + 1);
    return safeField(value, max);
}

// "0x" followed by hexadecimal digits (an exception code, an offset inside a module).
inline std::string safeHex(std::string_view value) {
    if (value.size() < 3 || value.size() > 18 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X')) return {};
    for (size_t i = 2; i < value.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return {};
    return std::string(value);
}

// "2026-11-18 21:14": digits, '-', ':' and one space.
inline std::string safeTime(std::string_view value) {
    if (value.size() != 16) return {};
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        const bool ok = (i == 4 || i == 7) ? c == '-' : i == 10 ? c == ' ' : i == 13 ? c == ':' : std::isdigit(static_cast<unsigned char>(c)) != 0;
        if (!ok) return {};
    }
    return std::string(value);
}

struct Marker {
    bool present = false;   // the file held something
    bool crash = false;     // a crash (not a lost graphics device)
    std::string module;     // file name of the module the fault was in
    std::string injected;   // known-risky injected components, as file names
    std::string code, offset, dump, time;
};

inline std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return lines;
}

inline Marker parseMarker(std::string_view text) {
    Marker m;
    const auto lines = splitLines(text);
    if (lines.empty() || lines[0].empty()) return m;
    m.present = true;
    m.crash = lines[0].substr(0, 5) == "crash";
    if (m.crash && lines[0].size() > 6) m.module = safeFileName(lines[0].substr(6));
    if (lines.size() > 1) {
        std::string_view rest = lines[1];
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            const std::string name = safeFileName(rest.substr(0, comma));
            if (!name.empty()) m.injected += (m.injected.empty() ? "" : ", ") + name;
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
    }
    if (lines.size() > 2) {
        std::string_view rest = lines[2];
        const auto take = [&rest](std::string_view key, bool toEnd) -> std::string_view {
            const size_t at = rest.find(key);
            if (at == std::string_view::npos) return {};
            std::string_view value = rest.substr(at + key.size());
            if (!toEnd) value = value.substr(0, value.find(' '));
            return value;
        };
        m.code = safeHex(take("code=", false));
        m.offset = safeHex(take("offset=", false));
        // A file name the handler made itself: "pushfps-crash-<date>-<time>-<pid>.dmp". Anything with a
        // folder in it, or that is not a .dmp, is not that.
        const std::string_view dump = take("dump=", false);
        if (dump.find_first_of("/\\:") == std::string_view::npos && dump.size() > 4 && dump.substr(dump.size() - 4) == ".dmp")
            m.dump = safeField(dump, 80);
        m.time = safeTime(take("time=", true));
    }
    return m;
}

// The session note: "key=value key=value" with a fixed set of keys, each with its own shape, so the
// line cannot hold anything but choices and numbers (no device name, host, address or file name):
//   source  letters and digits (capture, ps5, xbox, moonlight, screen, file, url, frameinput, image)
//   size    <width>x<height>
//   fg      "off", or method, multiplier and tier: letters, digits, '-' and '_' (for spaces)
//   nr, sr, hdr   0 or 1
inline bool sessionValueValid(std::string_view key, std::string_view value) {
    const auto all = [&value](auto&& pred) {
        if (value.empty()) return false;
        for (const char ch : value) if (!pred(static_cast<unsigned char>(ch))) return false;
        return true;
    };
    if (key == "source") return value.size() <= 16 && all([](unsigned char c) { return std::isalnum(c) != 0; });
    if (key == "size") {
        const size_t x = value.find('x');
        if (x == std::string_view::npos || x == 0 || x > 5 || value.size() - x - 1 == 0 || value.size() - x - 1 > 5) return false;
        for (size_t i = 0; i < value.size(); ++i)
            if (i != x && !std::isdigit(static_cast<unsigned char>(value[i]))) return false;
        return true;
    }
    if (key == "fg") return value.size() <= 40 && all([](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
    if (key == "nr" || key == "sr" || key == "hdr") return value == "0" || value == "1";
    return false;
}
struct SessionPart { std::string_view key; std::string value; };
inline std::string sessionNote(const std::vector<SessionPart>& parts) {
    std::string out;
    for (const auto& p : parts) {
        std::string value = p.value;
        for (char& c : value) if (c == ' ') c = '_';
        if (!sessionValueValid(p.key, value)) continue;
        out += (out.empty() ? "" : " ");
        out += std::string(p.key) + "=" + value;
    }
    return out;
}
// One value back out of a note ("" when absent or not of that key's shape).
inline std::string sessionValue(std::string_view note, std::string_view key) {
    size_t at = 0;
    while (at < note.size()) {
        const size_t end = note.find(' ', at);
        const std::string_view item = note.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        if (item.size() > key.size() && item.substr(0, key.size()) == key && item[key.size()] == '=') {
            const std::string_view value = item.substr(key.size() + 1);
            return sessionValueValid(key, value) ? std::string(value) : std::string();
        }
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    return {};
}

struct Facts {
    std::string version;   // PushFPS version
    std::string windows;   // Windows build, e.g. "10.0.26100"
    std::string gpu;       // graphics card name
    std::string session;   // the session note of the run that crashed
    std::string optimise;  // what Optimise for my PC was testing when it stopped ("" = it was not running)
};

// The text the user copies. English on purpose: it is read by whoever helps, in any language.
inline std::string summary(const Marker& m, const Facts& f) {
    const auto orUnknown = [](const std::string& s) { return s.empty() ? std::string("unknown") : s; };
    // Values were stored with '_' for spaces.
    const auto value = [&f](std::string_view key) {
        std::string v = sessionValue(f.session, key);
        for (char& c : v) if (c == '_') c = ' ';
        return v;
    };
    std::string out = "PushFPS crash summary\n";
    out += "Version: " + orUnknown(safeField(f.version, 40)) + "\n";
    out += "Crashed in: " + orUnknown(m.module);
    if (!m.offset.empty()) out += " +" + m.offset;
    if (!m.code.empty()) out += " (code " + m.code + ")";
    out += "\n";
    out += "Time: " + orUnknown(m.time) + "\n";
    out += "Windows: " + orUnknown(safeField(f.windows, 40)) + "\n";
    out += "Graphics card: " + orUnknown(plainText(f.gpu, 64)) + "\n";
    const std::string source = value("source");
    if (source.empty()) out += "Playing: nothing recorded\n";
    else {
        out += "Playing: " + source;
        if (const std::string size = value("size"); !size.empty()) out += " " + size;
        if (const std::string hdr = value("hdr"); hdr == "1") out += " HDR";
        out += "\n";
        const std::string fg = value("fg");
        out += "Frame generation: " + (fg.empty() || fg == "off" ? std::string("off") : fg) + "\n";
        out += "AI picture enhancement: " + std::string(value("nr") == "1" ? "on" : "off") + "\n";
        out += "Upscaling: " + std::string(value("sr") == "1" ? "on" : "off") + "\n";
    }
    if (const std::string step = safeField(f.optimise, 40); !step.empty()) out += "Optimise for my PC: was running, testing " + step + "\n";
    if (!m.injected.empty()) out += "Injected: " + m.injected + "\n";
    out += "Crash file: " + (m.dump.empty() ? std::string("not recorded") : m.dump) + "\n";
    return out;
}

}  // namespace veyra::engine::crashnote
