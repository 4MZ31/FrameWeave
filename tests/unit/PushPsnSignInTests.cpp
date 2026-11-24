// SPDX-License-Identifier: GPL-3.0-or-later
#include "veyra/remoteplay/PushPsnSignIn.h"
#include "veyra/remoteplay/Types.h"

#include <cstdio>
#include <string>

namespace psn = veyra::remoteplay::psn;
static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)

int main() {
    const std::string url = psn::signInUrl();
    CHECK(url.rfind("https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?", 0) == 0);
    CHECK(url.find("client_id=ba495a24-818c-472b-b12d-ff231c1b5745") != std::string::npos);
    CHECK(url.find("redirect_uri=https%3A%2F%2Fremoteplay.dl.playstation.net%2Fremoteplay%2Fredirect") != std::string::npos);
    CHECK(url.find("request_locale=en_US") != std::string::npos);
    CHECK(url.find("prompt=always") != std::string::npos);
    CHECK(url.find(' ') == std::string::npos);
    CHECK(psn::signInUrl("nl_NL").find("request_locale=nl_NL") != std::string::npos);
    CHECK(psn::signInUrl("x&y=z").find("request_locale") == std::string::npos);
    CHECK(psn::signInUrl("").find("request_locale") == std::string::npos);

    const std::string red = std::string(psn::kRedirect);
    auto p = psn::readPaste(red + "?code=AbC123-_.~&cid=5");
    CHECK(p.what == psn::Pasted::Code && p.code == "AbC123-_.~");
    p = psn::readPaste("  \"" + red + "?cid=5&code=v3.XyZ\"\n");
    CHECK(p.what == psn::Pasted::Code && p.code == "v3.XyZ");
    p = psn::readPaste(red + "?code=v3%2EXy#frag");
    CHECK(p.what == psn::Pasted::Code && p.code == "v3.Xy");
    p = psn::readPaste("v3.OnlyTheCode");
    CHECK(p.what == psn::Pasted::Code && p.code == "v3.OnlyTheCode");
    CHECK(psn::readPaste("").what == psn::Pasted::Empty);
    CHECK(psn::readPaste("   ").what == psn::Pasted::Empty);
    // Another site's address, even one that looks alike or carries Sony's address inside it, is refused.
    CHECK(psn::readPaste("https://evil.example/remoteplay/redirect?code=abc").what == psn::Pasted::OtherAddress);
    CHECK(psn::readPaste("https://remoteplay.dl.playstation.net.evil.example/remoteplay/redirect?code=abc").what == psn::Pasted::OtherAddress);
    CHECK(psn::readPaste("http://remoteplay.dl.playstation.net/remoteplay/redirect?code=abc").what == psn::Pasted::OtherAddress);
    CHECK(psn::readPaste("https://x.example/?u=" + red + "?code=abc").what == psn::Pasted::OtherAddress);
    CHECK(psn::readPaste(red + "?error=access_denied").what == psn::Pasted::NoCode);
    CHECK(psn::readPaste(red + "?code=").what == psn::Pasted::NoCode);
    CHECK(psn::readPaste(red + "?code=a&code=b").what == psn::Pasted::NoCode);
    CHECK(psn::readPaste(red + "?code=%zz").what == psn::Pasted::NoCode);
    CHECK(psn::readPaste("two words").what == psn::Pasted::NotACode);
    CHECK(psn::readPaste(red + "?code=a%26b").what == psn::Pasted::NotACode);
    CHECK(psn::readPaste(std::string(600, 'a')).what == psn::Pasted::NotACode);
    CHECK(psn::readPaste(std::string(600, 'a')).code.empty());

    const std::string body = psn::tokenRequestBody("v3.Ab+c");
    CHECK(body.rfind("grant_type=authorization_code&code=v3.Ab%2Bc&scope=", 0) == 0);
    CHECK(body.find("redirect_uri=https%3A%2F%2F") != std::string::npos);

    CHECK(psn::userIdDigits("1234567890123456789") == std::optional<std::string>("1234567890123456789"));
    CHECK(!psn::userIdDigits(""));
    CHECK(!psn::userIdDigits("12a"));
    CHECK(!psn::userIdDigits("-5"));
    CHECK(!psn::userIdDigits(std::string(21, '1')));

    // The number Sony answers becomes the 12-character form the pairing takes, and back again.
    const auto id = veyra::remoteplay::accountIdFromDecimal("1234567890123456789");
    CHECK(id.has_value());
    if (id) {
        const auto b64 = veyra::remoteplay::accountIdToBase64(*id);
        CHECK(b64.size() == 12 && b64.back() == '=');
        const auto back = veyra::remoteplay::accountIdFromBase64(b64);
        CHECK(back && *back == *id);
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
