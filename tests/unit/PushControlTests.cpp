// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Local control (ui/PushControl.h): who may ask, what may be asked, and what is answered. Pure: also builds with g++.
#include "veyra/ui/PushControl.h"
#include <cmath>
#include <cstdio>
#include <string>
using namespace veyra::ui::control;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)

static const std::string kToken = "q7Yw2mVbT9xKc4LrN8sHd1ZfJ6pGu3EaWo5XiQyRtUv";   // 43 characters, as PushFPS makes them
static std::string request(const std::string& line, const std::string& headers) { return line + "\r\n" + headers + "\r\n"; }
static std::string ok(const std::string& line, const std::string& more = "") {
    return request(line, "Host: 127.0.0.1:18766\r\nAuthorization: Bearer " + kToken + "\r\n" + more);
}
static Decision ask(const std::string& text, std::string_view token = kToken) { return decide(text, 18766, token); }

int main() {
    // Tokens.
    CHECK(validToken(kToken));
    CHECK(!validToken("short"));
    CHECK(!validToken(std::string(31, 'a')) && validToken(std::string(32, 'a')) && validToken(std::string(128, 'a')) && !validToken(std::string(129, 'a')));
    CHECK(!validToken(std::string(40, 'a') + " "));
    CHECK(!validToken(std::string(40, 'a') + "\r\n"));
    CHECK(sameText("abc", "abc") && !sameText("abc", "abd") && !sameText("abc", "ab") && !sameText("", "a") && sameText("", ""));

    // Everything that may be asked.
    CHECK(ask(ok("GET /v1/status HTTP/1.1")).status == 200 && ask(ok("GET /v1/status HTTP/1.1")).action == Action::Status);
    CHECK(ask(ok("POST /v1/record/start HTTP/1.1")).action == Action::RecordStart);
    CHECK(ask(ok("POST /v1/record/stop HTTP/1.1")).action == Action::RecordStop);
    CHECK(ask(ok("POST /v1/replay/save HTTP/1.1")).action == Action::ReplaySave);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1")).action == Action::Marker);
    CHECK(ask(ok("POST /v1/screenshot HTTP/1.1")).action == Action::Screenshot);
    { const auto d = ask(ok("POST /v1/scene/3 HTTP/1.1")); CHECK(d.status == 200 && d.action == Action::Scene && d.scene == 3); }
    CHECK(ask(ok("POST /v1/scene/9 HTTP/1.1")).scene == 9);
    CHECK(ask(ok("POST /v1/marker HTTP/1.0")).status == 200);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "Content-Length: 0\r\nUser-Agent: curl/8\r\nAccept: */*\r\n")).status == 200);
    CHECK(ask(request("POST /v1/marker HTTP/1.1", "host: LOCALHOST:18766\r\nAUTHORIZATION: bearer " + kToken + "\r\n")).status == 200);   // names are not case sensitive

    // Nothing else exists: no file, no command, no setting.
    CHECK(ask(ok("POST /v1/scene/0 HTTP/1.1")).status == 404);
    CHECK(ask(ok("POST /v1/scene/10 HTTP/1.1")).status == 404);
    CHECK(ask(ok("POST /v1/scene/3/x HTTP/1.1")).status == 404);
    CHECK(ask(ok("GET / HTTP/1.1")).status == 404);
    CHECK(ask(ok("GET /v1/status?x=1 HTTP/1.1")).status == 404);
    CHECK(ask(ok("GET /v1/../../Windows/win.ini HTTP/1.1")).status == 404);
    CHECK(ask(ok("POST /v1/settings HTTP/1.1")).status == 404);
    CHECK(ask(ok("POST /v1/status HTTP/1.1")).status == 405);
    CHECK(ask(ok("GET /v1/record/start HTTP/1.1")).status == 405);     // a link or an image tag can never start a recording
    CHECK(ask(ok("GET /v1/scene/2 HTTP/1.1")).status == 405);
    CHECK(ask(ok("DELETE /v1/marker HTTP/1.1")).status == 405);
    CHECK(ask(ok("OPTIONS /v1/marker HTTP/1.1")).status == 405);       // a browser's preflight gets no permission

    // The token.
    CHECK(ask(request("GET /v1/status HTTP/1.1", "Host: 127.0.0.1:18766\r\n")).status == 401);
    { const auto d = ask(request("GET /v1/status HTTP/1.1", "Host: 127.0.0.1:18766\r\nAuthorization: Bearer " + std::string(43, 'x') + "\r\n"));
      CHECK(d.status == 401 && d.wrongToken && d.action == Action::None); }
    CHECK(!ask(request("GET /v1/status HTTP/1.1", "Host: 127.0.0.1:18766\r\n")).wrongToken);   // none given is not a guess
    CHECK(ask(request("GET /v1/status HTTP/1.1", "Host: 127.0.0.1:18766\r\nAuthorization: Basic " + kToken + "\r\n")).status == 401);
    CHECK(ask(request("GET /v1/status HTTP/1.1", "Host: 127.0.0.1:18766\r\nAuthorization: Bearer " + kToken + "x\r\n")).status == 401);
    CHECK(ask(request("GET /v1/status HTTP/1.1", "Host: 127.0.0.1:18766\r\nAuthorization: Bearer " + kToken.substr(0, 42) + "\r\n")).status == 401);
    CHECK(ask(request("GET /v1/status HTTP/1.1", "Host: 127.0.0.1:18766\r\nAuthorization: Bearer " + kToken + "\r\nAuthorization: Bearer " + kToken + "\r\n")).status == 400);
    CHECK(ask(ok("GET /v1/status HTTP/1.1"), "").status == 503);        // no token set: nothing is allowed, not everything
    CHECK(ask(ok("GET /v1/status HTTP/1.1"), "weak").status == 503);

    // Where it comes from: never a web page, and only this PC's own address.
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "Origin: https://example.com\r\n")).status == 403);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "Origin: http://127.0.0.1:18766\r\n")).status == 403);     // also a page served from this PC
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "origin: null\r\n")).status == 403);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "Referer: https://example.com/\r\n")).status == 403);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "Sec-Fetch-Site: cross-site\r\n")).status == 403);
    CHECK(ask(ok("GET /v1/status HTTP/1.1", "Sec-Fetch-Mode: no-cors\r\n")).status == 403);
    // A page is refused before the token is looked at, so it cannot be used to guess one.
    CHECK(!ask(request("GET /v1/status HTTP/1.1", "Host: 127.0.0.1:18766\r\nOrigin: https://example.com\r\nAuthorization: Bearer " + std::string(43, 'x') + "\r\n")).wrongToken);
    const auto hostOnly = [](const std::string& host) { return ask(request("GET /v1/status HTTP/1.1", "Host: " + host + "\r\nAuthorization: Bearer " + kToken + "\r\n")).status; };
    CHECK(hostOnly("127.0.0.1:18766") == 200 && hostOnly("localhost:18766") == 200);
    CHECK(hostOnly("evil.example:18766") == 403);        // a made-up name that points at this PC (DNS rebinding)
    CHECK(hostOnly("127.0.0.1.evil.example:18766") == 403);
    CHECK(hostOnly("127.0.0.1:80") == 403);
    CHECK(hostOnly("127.0.0.1") == 403);
    CHECK(hostOnly("192.168.1.20:18766") == 403);
    CHECK(hostOnly("127.0.0.1:18766x") == 403);
    CHECK(hostOnly("127.0.0.1:") == 403);
    CHECK(ask(request("GET /v1/status HTTP/1.1", "Authorization: Bearer " + kToken + "\r\n")).status == 403);   // no Host at all
    CHECK(ask(request("GET /v1/status HTTP/1.1", "Host: 127.0.0.1:18766\r\nHost: evil.example\r\nAuthorization: Bearer " + kToken + "\r\n")).status == 400);

    // Framing: no body, nothing smuggled, nothing oversized.
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "Content-Length: 5\r\n")).status == 413);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "Content-Length: 0\r\nContent-Length: 0\r\n")).status == 413);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "Transfer-Encoding: chunked\r\n")).status == 400);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1") + "extra").status == 400);
    CHECK(ask("POST /v1/marker HTTP/1.1\r\nHost: 127.0.0.1:18766\r\n").status == 400);             // never finished
    CHECK(ask(ok("POST /v1/marker HTTP/2")).status == 400);
    CHECK(ask(ok("POST  /v1/marker HTTP/1.1")).status == 400);
    CHECK(ask(ok("POST /v1/marker")).status == 400);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "no colon here\r\n")).status == 400);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "X Bad: 1\r\n")).status == 400);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", " folded: 1\r\n")).status == 400);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", ": empty name\r\n")).status == 400);
    CHECK(ask(ok("POST /v1/marker HTTP/1.1", "X-Pad: " + std::string(kMaxRequest, 'a') + "\r\n")).status == 413);
    CHECK(ask("").status == 400 && ask("\r\n\r\n").status == 400 && ask("GARBAGE").status == 400);
    CHECK(framing("GET / HTTP/1.1\r\n") == Framing::Incomplete);
    CHECK(framing("GET / HTTP/1.1\r\n\r\n") == Framing::Complete);
    CHECK(framing(std::string(kMaxRequest, 'a')) == Framing::TooLarge);
    CHECK(framing(std::string(kMaxRequest - 2, 'a') + "\r\n\r\n") == Framing::TooLarge);

    // The limiter: a few a second, and a pause after wrong tokens.
    {
        Limiter limit;
        for (int i = 0; i < Limiter::kPerSecond; ++i) CHECK(limit.allow(1000 + i));
        CHECK(!limit.allow(1500));                      // the eleventh in one second
        CHECK(limit.allow(2000));                       // the next second
        for (int i = 0; i < Limiter::kStrikes - 1; ++i) limit.wrongToken(3000 + i);
        CHECK(!limit.locked(3100) && limit.allow(3100));
        limit.wrongToken(3200);                         // the fifth
        CHECK(limit.locked(3201) && !limit.allow(3201));
        CHECK(limit.locked(3200 + Limiter::kLockMs - 1));
        CHECK(!limit.locked(3200 + Limiter::kLockMs) && limit.allow(3200 + Limiter::kLockMs));
        // Wrong tokens far apart are mistakes, not guessing.
        Limiter slow;
        for (int i = 0; i < 20; ++i) slow.wrongToken(std::int64_t(i) * (Limiter::kStrikeWindowMs + 1));
        CHECK(!slow.locked(20 * (Limiter::kStrikeWindowMs + 1)));
        // A clock that jumps back does not lock it for ever.
        Limiter back;
        CHECK(back.allow(100000)); CHECK(back.allow(5));
    }

    // The status: only what is listed, in a fixed order, and nothing that could carry a name or a path.
    {
        Facts f;
        f.version = "4.1.0"; f.source = "capture"; f.playing = true; f.frameGeneration = true; f.fps = 119.71; f.latencyMs = 14.23;
        f.recording = true; f.recordingSeconds = 83; f.markers = 2; f.replay = true;
        CHECK(statusJson(f) == "{\"ok\":true,\"app\":\"FrameWeave\",\"api\":1,\"version\":\"4.1.0\",\"playing\":true,\"source\":\"capture\","
                               "\"frameGeneration\":true,\"fps\":119.7,\"latencyMs\":14.2,\"recording\":true,\"recordingSeconds\":83,"
                               "\"markers\":2,\"replay\":true,\"streaming\":false}");
        Facts odd;
        odd.version = "4.1\"}\r\n<script>C:\\Users\\name"; odd.source = "C:\\Users\\name\\video.mp4"; odd.fps = -1; odd.recordingSeconds = -5; odd.markers = 2000000000;
        const auto text = statusJson(odd);
        CHECK(text.find("Users") == std::string::npos || text.find("\\") == std::string::npos);
        CHECK(text.find("\\") == std::string::npos && text.find("<") == std::string::npos && text.find("\r") == std::string::npos);
        CHECK(text.find("\"source\":\"\"") != std::string::npos);            // a word that is not on the list is no word
        CHECK(text.find("\"fps\":null") != std::string::npos && text.find("\"recordingSeconds\":0") != std::string::npos);
        CHECK(text.find("\"markers\":1000000") != std::string::npos);
        Facts nan; nan.fps = std::nan(""); nan.latencyMs = 1e300;
        CHECK(statusJson(nan).find("\"fps\":null") != std::string::npos && statusJson(nan).find("\"latencyMs\":null") != std::string::npos);
    }
    CHECK(errorJson("wrong token") == "{\"ok\":false,\"error\":\"wrong token\"}");
    CHECK(errorJson("a\"b\\c\r\n") == "{\"ok\":false,\"error\":\"abc\"}");
    CHECK(resultJson(true) == "{\"ok\":true}" && resultJson(false).find("\"ok\":false") != std::string::npos);
    // A scene has to be there: its number from 1, at most 9 (Ctrl+1..9), and no further than the list.
    CHECK(sceneExists(1, 4) && sceneExists(4, 4) && !sceneExists(5, 4));
    CHECK(!sceneExists(0, 4) && !sceneExists(-1, 4) && !sceneExists(1, 0));
    CHECK(sceneExists(9, 12) && !sceneExists(10, 12));

    // The answer: never a header that lets a web page read it.
    {
        const auto body = errorJson("token required");
        const auto text = response(401, body);
        CHECK(text.rfind("HTTP/1.1 401 Unauthorized\r\n", 0) == 0);
        CHECK(text.find("Access-Control") == std::string::npos);
        CHECK(text.find("Content-Length: " + std::to_string(body.size()) + "\r\n") != std::string::npos);
        CHECK(text.find("Cache-Control: no-store\r\n") != std::string::npos && text.find("Connection: close\r\n") != std::string::npos);
        CHECK(text.find("WWW-Authenticate: Bearer\r\n") != std::string::npos);
        CHECK(text.size() > body.size() + 4 && text.substr(text.size() - body.size() - 4) == "\r\n\r\n" + body);
        CHECK(response(200, "{}").find("WWW-Authenticate") == std::string::npos);
        CHECK(response(429, "{}").rfind("HTTP/1.1 429 Too Many Requests\r\n", 0) == 0);
    }
    if (failures) { std::printf("%d failed\n", failures); return 1; }
    std::printf("PushControlTests: all passed\n");
    return 0;
}
