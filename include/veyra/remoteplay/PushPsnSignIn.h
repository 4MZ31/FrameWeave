// SPDX-License-Identifier: GPL-3.0-or-later
// PushFPS: getting a PSN Account ID through Sony's own sign-in, with no lookup website in between.
//
// Sony's sign-in page for the PS Remote Play app (the identifiers below are that app's public ones, the same
// every open-source Remote Play client uses; they name the app and grant nothing without the user's own
// sign-in) ends on a fixed address that carries a one-time code. The user pastes that address back; PushFPS
// trades the code for a short-lived token at Sony's token address, reads the account's number from it and
// fills in the Account ID. The password is only ever typed on Sony's page; no token is kept or logged.
//
// This header is what can be decided without the network: the sign-in address, and what was pasted.
#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace veyra::remoteplay::psn {

inline constexpr std::string_view kAuthHost = "https://auth.api.sonyentertainmentnetwork.com";
inline constexpr std::string_view kClientId = "ba495a24-818c-472b-b12d-ff231c1b5745";
inline constexpr std::string_view kClientSecret = "mvaiZkRsAsI1IBkY";
inline constexpr std::string_view kRedirect = "https://remoteplay.dl.playstation.net/remoteplay/redirect";
inline constexpr std::string_view kScope = "psn:clientapp referenceDataService:countryConfig.read "
    "pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update";
// A code is a few dozen characters; anything far longer is not one.
inline constexpr std::size_t kMaxCode = 512;

inline std::string percent(std::string_view text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(char(c));
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}

// The page the browser opens. `locale` is Sony's own form ("en_US"); anything else is left out.
inline std::string signInUrl(std::string_view locale = "en_US") {
    bool localeOk = locale.size() == 5 && locale[2] == '_';
    for (std::size_t i = 0; localeOk && i < 5; ++i)
        if (i != 2 && !std::isalpha(static_cast<unsigned char>(locale[i]))) localeOk = false;
    std::string url{kAuthHost};
    url += "/2.0/oauth/authorize?service_entity=urn%3Aservice-entity%3Apsn&response_type=code&client_id=";
    url += percent(kClientId);
    url += "&redirect_uri="; url += percent(kRedirect);
    url += "&scope="; url += percent(kScope);
    if (localeOk) { url += "&request_locale="; url += percent(locale); }
    url += "&ui=pr&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal";
    return url;
}

enum class Pasted { Code, Empty, OtherAddress, NoCode, NotACode };
struct Paste { Pasted what = Pasted::Empty; std::string code; };

namespace detail {
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) || s.front() == '"' || s.front() == '\''))
        s.remove_prefix(1);
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) || s.back() == '"' || s.back() == '\''))
        s.remove_suffix(1);
    return s;
}
inline bool codeChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~';
}
inline std::optional<std::string> unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') { out.push_back(s[i]); continue; }
        if (i + 2 >= s.size() || !std::isxdigit(static_cast<unsigned char>(s[i + 1])) ||
            !std::isxdigit(static_cast<unsigned char>(s[i + 2]))) return std::nullopt;
        out.push_back(char(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16)));
        i += 2;
    }
    return out;
}
}

// Takes the whole address the browser landed on, or the code alone. Only Sony's own redirect address is
// accepted as an address: a code copied from anywhere else is not trusted to be Sony's.
inline Paste readPaste(std::string_view text) {
    Paste p;
    text = detail::trim(text);
    if (text.empty()) return p;
    std::string_view code = text;
    const bool isAddress = text.substr(0, 7) == "http://" || text.substr(0, 8) == "https://";
    if (isAddress) {
        const std::string prefix = std::string(kRedirect) + "?";
        if (text.substr(0, prefix.size()) != prefix) { p.what = Pasted::OtherAddress; return p; }
        std::string_view query = text.substr(prefix.size());
        if (const auto hash = query.find('#'); hash != std::string_view::npos) query = query.substr(0, hash);
        std::optional<std::string> found;
        int seen = 0;
        while (!query.empty()) {
            const auto amp = query.find('&');
            const auto part = query.substr(0, amp);
            if (part.substr(0, 5) == "code=") { ++seen; found = detail::unescape(part.substr(5)); }
            if (amp == std::string_view::npos) break;
            query.remove_prefix(amp + 1);
        }
        if (seen != 1 || !found || found->empty()) { p.what = Pasted::NoCode; return p; }
        p.code = *found;
    } else p.code = std::string(code);
    if (p.code.size() > kMaxCode) { p.code.clear(); p.what = Pasted::NotACode; return p; }
    for (const char c : p.code) if (!detail::codeChar(c)) { p.code.clear(); p.what = Pasted::NotACode; return p; }
    p.what = Pasted::Code;
    return p;
}

// The body of the request that trades the code for a token (form-encoded).
inline std::string tokenRequestBody(std::string_view code) {
    std::string body = "grant_type=authorization_code&code=";
    body += percent(code);
    body += "&scope="; body += percent(kScope);
    body += "&redirect_uri="; body += percent(kRedirect);
    return body;
}

// Sony answers the token's details with "user_id" as a decimal number in a string. Only digits, at most 20.
inline std::optional<std::string> userIdDigits(std::string_view text) {
    if (text.empty() || text.size() > 20) return std::nullopt;
    for (const char c : text) if (c < '0' || c > '9') return std::nullopt;
    return std::string(text);
}

}  // namespace veyra::remoteplay::psn
