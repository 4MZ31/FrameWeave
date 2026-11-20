#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS local control: a button box (a Stream Deck), a script or a streaming program on the same
// PC starts and stops a recording, saves the instant replay, marks a moment, takes a screenshot or
// switches a scene, and reads whether PushFPS is playing and recording.
//
// Off unless switched on (Settings). When on:
//   - it listens on 127.0.0.1 only, never on a network address, and nothing is forwarded;
//   - every request needs the token PushFPS made for it ("Authorization: Bearer <token>"), 32 random
//     bytes, kept encrypted for this Windows user and shown only when its Copy button is pressed;
//   - a request that comes from a web page is refused whatever it carries (any Origin, Referer or
//     Sec-Fetch-* header), and the Host header must name this PC's own loopback address and port,
//     so a page open in a browser cannot drive PushFPS, also not through a made-up host name;
//   - wrong tokens lock it for half a minute, and requests are limited to a few a second;
//   - what it can do is the fixed list below. There is no file path, no command, no setting, no
//     stream key and no console account anywhere in it, in what it takes or in what it answers:
//     the status holds yes/no values, numbers and words from a fixed list.
//
//   GET  /v1/status            what is going on (JSON)
//   POST /v1/record/start      start a recording (with the settings chosen last; no dialog)
//   POST /v1/record/stop       stop it
//   POST /v1/replay/save       save the instant replay
//   POST /v1/marker            mark this moment of the recording
//   POST /v1/screenshot        take a screenshot
//   POST /v1/scene/<1..9>      switch to that scene of Streaming mode
//
// This header is the whole decision, pure (no Qt, no Windows), so every rule above is unit-tested
// (tests/unit/PushControlTests.cpp, also g++). The socket is src/ui/bridge/PushControl.cpp.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace veyra::ui::control {
inline constexpr std::size_t kMaxRequest = 4096;   // the whole request; a request has no body
inline constexpr unsigned kDefaultPort = 18766;
inline constexpr int kMaxScene = 9;

enum class Action { None, Status, RecordStart, RecordStop, ReplaySave, Marker, Screenshot, Scene };

struct Decision {
    int status = 400;               // the HTTP status to answer with
    Action action = Action::None;   // what to do, when status is 200
    int scene = 0;                  // Action::Scene: 1..kMaxScene
    const char* error = "bad request";
    bool wrongToken = false;        // a token was given and it was not the right one (counts as a strike)
};

// A token as PushFPS makes them: 32 to 128 characters of A-Z a-z 0-9 - _ (base64url of 32 bytes is 43).
constexpr bool validToken(std::string_view token) {
    if (token.size() < 32 || token.size() > 128) return false;
    for (const char c : token)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

// Equal or not, in a time that does not depend on where the first difference is.
constexpr bool sameText(std::string_view a, std::string_view b) {
    unsigned difference = static_cast<unsigned>(a.size() ^ b.size());
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return difference == 0;
}

namespace detail {
constexpr char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
constexpr std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}
constexpr bool sameNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(b[i])) return false;
    return true;
}
constexpr bool startsNoCase(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && sameNoCase(text.substr(0, prefix.size()), prefix);
}
// "127.0.0.1:18766" or "localhost:18766" for this port, nothing else.
inline bool ownHost(std::string_view host, unsigned port) {
    const auto colon = host.rfind(':');
    if (colon == std::string_view::npos) return false;
    const auto name = host.substr(0, colon), digits = host.substr(colon + 1);
    if (digits.empty() || digits.size() > 5) return false;
    unsigned value = 0;
    for (const char c : digits) { if (c < '0' || c > '9') return false; value = value * 10 + static_cast<unsigned>(c - '0'); }
    return value == port && (name == "127.0.0.1" || sameNoCase(name, "localhost"));
}
struct Route { std::string_view method, path; Action action; };
inline constexpr std::array<Route, 6> kRoutes{{
    {"GET", "/v1/status", Action::Status},
    {"POST", "/v1/record/start", Action::RecordStart},
    {"POST", "/v1/record/stop", Action::RecordStop},
    {"POST", "/v1/replay/save", Action::ReplaySave},
    {"POST", "/v1/marker", Action::Marker},
    {"POST", "/v1/screenshot", Action::Screenshot},
}};
}

// What to answer to a request head (everything up to and including the empty line that ends it).
// `port` is the port listened on, `token` the token in force (an invalid one refuses everything).
inline Decision decide(std::string_view head, unsigned port, std::string_view token) {
    using namespace detail;
    Decision d;
    if (head.size() > kMaxRequest) { d.status = 413; d.error = "request too large"; return d; }
    const auto end = head.find("\r\n\r\n");
    if (end == std::string_view::npos) { d.status = 400; d.error = "incomplete request"; return d; }
    // No body is taken: anything after the head is refused, and so is a head that announces one.
    if (end + 4 != head.size()) { d.status = 400; d.error = "unexpected request data"; return d; }
    head = head.substr(0, end);
    const auto firstEnd = head.find("\r\n");
    const auto first = head.substr(0, firstEnd);
    const auto space1 = first.find(' '), space2 = first.rfind(' ');
    if (space1 == std::string_view::npos || space2 == space1) return d;
    const auto method = first.substr(0, space1), path = first.substr(space1 + 1, space2 - space1 - 1), version = first.substr(space2 + 1);
    if (method.empty() || path.empty() || path.find(' ') != std::string_view::npos || (version != "HTTP/1.1" && version != "HTTP/1.0")) return d;

    std::string_view host, authorization;
    bool haveHost = false, haveAuthorization = false, fromBrowser = false, haveLength = false;
    auto rest = firstEnd == std::string_view::npos ? std::string_view{} : head.substr(firstEnd + 2);
    while (!rest.empty()) {
        const auto lineEnd = rest.find("\r\n");
        const auto line = rest.substr(0, lineEnd);
        rest = lineEnd == std::string_view::npos ? std::string_view{} : rest.substr(lineEnd + 2);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) { d.error = "invalid header"; return d; }
        const auto name = line.substr(0, colon), value = trim(line.substr(colon + 1));
        // A header name with a space in it, or a folded line, is how requests are smuggled: refused.
        if (name.find(' ') != std::string_view::npos || name.find('\t') != std::string_view::npos) { d.error = "invalid header"; return d; }
        if (sameNoCase(name, "host")) { if (haveHost) { d.error = "duplicate header"; return d; } haveHost = true; host = value; }
        else if (sameNoCase(name, "authorization")) { if (haveAuthorization) { d.error = "duplicate header"; return d; } haveAuthorization = true; authorization = value; }
        else if (sameNoCase(name, "content-length")) {
            if (haveLength || value != "0") { d.status = 413; d.error = "no request body is accepted"; return d; }
            haveLength = true;
        }
        else if (sameNoCase(name, "transfer-encoding")) { d.error = "no request body is accepted"; return d; }
        else if (sameNoCase(name, "origin") || sameNoCase(name, "referer") || startsNoCase(name, "sec-fetch-")) fromBrowser = true;
    }
    // Where it came from is judged before the token: a page in a browser learns nothing from the answer.
    if (fromBrowser) { d.status = 403; d.error = "requests from a web page are not accepted"; return d; }
    if (!haveHost || !ownHost(host, port)) { d.status = 403; d.error = "host is not this PC's loopback address"; return d; }
    if (!validToken(token)) { d.status = 503; d.error = "local control has no token"; return d; }
    constexpr std::string_view bearer = "Bearer ";
    if (!haveAuthorization || !startsNoCase(authorization, bearer)) { d.status = 401; d.error = "token required"; return d; }
    const auto given = trim(authorization.substr(bearer.size()));
    if (!sameText(given, token)) { d.status = 401; d.error = "wrong token"; d.wrongToken = true; return d; }

    for (const auto& route : kRoutes) {
        if (path != route.path) continue;
        if (method != route.method) { d.status = 405; d.error = "method not allowed"; return d; }
        d.status = 200; d.action = route.action; d.error = ""; return d;
    }
    constexpr std::string_view scenePrefix = "/v1/scene/";
    if (path.size() == scenePrefix.size() + 1 && path.substr(0, scenePrefix.size()) == scenePrefix) {
        const char digit = path.back();
        if (digit >= '1' && digit <= '0' + kMaxScene) {
            if (method != "POST") { d.status = 405; d.error = "method not allowed"; return d; }
            d.status = 200; d.action = Action::Scene; d.scene = digit - '0'; d.error = ""; return d;
        }
    }
    d.status = 404; d.error = "not found";
    return d;
}

// Whether a request head is complete, too large, or still arriving.
enum class Framing { Incomplete, Complete, TooLarge };
constexpr Framing framing(std::string_view received) {
    const auto end = received.find("\r\n\r\n");
    if (end != std::string_view::npos) return end + 4 <= kMaxRequest ? Framing::Complete : Framing::TooLarge;
    return received.size() >= kMaxRequest ? Framing::TooLarge : Framing::Incomplete;
}

// Requests a second, and a pause after wrong tokens. Times are milliseconds of any steady clock.
class Limiter {
public:
    static constexpr int kPerSecond = 10;          // requests in any one second
    static constexpr int kStrikes = 5;             // wrong tokens within kStrikeWindowMs ...
    static constexpr std::int64_t kStrikeWindowMs = 60000;
    static constexpr std::int64_t kLockMs = 30000; // ... refuse everything for this long

    bool locked(std::int64_t now) const { return now < lockedUntil_; }
    // True when the request may be looked at; false = answer 429 and do nothing else.
    bool allow(std::int64_t now) {
        if (locked(now)) return false;
        if (now - windowStart_ >= 1000 || now < windowStart_) { windowStart_ = now; inWindow_ = 0; }
        if (inWindow_ >= kPerSecond) return false;
        ++inWindow_;
        return true;
    }
    void wrongToken(std::int64_t now) {
        if (strikes_ == 0 || now - firstStrike_ > kStrikeWindowMs || now < firstStrike_) { firstStrike_ = now; strikes_ = 0; }
        if (++strikes_ >= kStrikes) { lockedUntil_ = now + kLockMs; strikes_ = 0; }
    }

private:
    std::int64_t windowStart_ = 0, firstStrike_ = 0, lockedUntil_ = 0;
    int inWindow_ = 0, strikes_ = 0;
};

// What the status answers with: yes/no values, numbers, and a word from a fixed list.
struct Facts {
    std::string_view version;       // e.g. "4.1.0"; anything but digits, letters, dots, + and - is dropped
    std::string_view source;        // "", "capture", "ps5", "moonlight", "xbox", "screen", "file", "url", "image", "frameinput"
    bool playing = false, recording = false, replay = false, streaming = false, frameGeneration = false;
    int recordingSeconds = 0, markers = 0;
    double fps = -1, latencyMs = -1;   // below 0: not known (null)
};

inline std::string statusJson(const Facts& f) {
    static constexpr std::array<std::string_view, 9> kSources{"capture", "ps5", "moonlight", "xbox", "screen", "file", "url", "image", "frameinput"};
    std::string version;
    for (const char c : f.version)
        if (version.size() < 32 && ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '+' || c == '-')) version += c;
    std::string_view source;
    for (const auto known : kSources) if (f.source == known) source = known;
    const auto number = [](double v, int decimals) {
        if (!(v >= 0) || v > 1e6) return std::string("null");
        char text[32];
        std::snprintf(text, sizeof text, "%.*f", decimals, v);
        return std::string(text);
    };
    const auto flag = [](bool b) { return b ? "true" : "false"; };
    const auto count = [](int v) { return std::to_string(v < 0 ? 0 : v > 1000000 ? 1000000 : v); };
    std::string out = "{\"ok\":true,\"app\":\"PushFPS\",\"api\":1,\"version\":\"" + version + "\"";
    out += ",\"playing\":"; out += flag(f.playing);
    out += ",\"source\":\""; out += source; out += "\"";
    out += ",\"frameGeneration\":"; out += flag(f.frameGeneration);
    out += ",\"fps\":" + number(f.fps, 1);
    out += ",\"latencyMs\":" + number(f.latencyMs, 1);
    out += ",\"recording\":"; out += flag(f.recording);
    out += ",\"recordingSeconds\":" + count(f.recordingSeconds);
    out += ",\"markers\":" + count(f.markers);
    out += ",\"replay\":"; out += flag(f.replay);
    out += ",\"streaming\":"; out += flag(f.streaming);
    out += "}";
    return out;
}

// {"ok":false,"error":"..."} for a refusal (the reason is one of this header's own fixed texts).
inline std::string errorJson(std::string_view reason) {
    std::string out = "{\"ok\":false,\"error\":\"";
    for (const char c : reason) if (c >= 0x20 && c != '"' && c != '\\' && static_cast<unsigned char>(c) < 0x7F) out += c;
    out += "\"}";
    return out;
}
// {"ok":true|false} for an action that was carried out or refused by PushFPS itself.
inline std::string resultJson(bool done) { return done ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"not possible right now\"}"; }

constexpr const char* reasonPhrase(int status) {
    return status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 401 ? "Unauthorized" : status == 403 ? "Forbidden"
         : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : status == 408 ? "Request Timeout"
         : status == 409 ? "Conflict" : status == 413 ? "Content Too Large" : status == 429 ? "Too Many Requests" : "Service Unavailable";
}

// The whole answer. Never a CORS header: no web page is meant to read it.
inline std::string response(int status, std::string_view body) {
    std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reasonPhrase(status) + "\r\n";
    out += "Content-Type: application/json; charset=utf-8\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n";
    if (status == 401) out += "WWW-Authenticate: Bearer\r\n";
    out += "\r\n";
    out += body;
    return out;
}
}
