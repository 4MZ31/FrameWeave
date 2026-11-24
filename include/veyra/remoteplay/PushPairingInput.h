// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// What was typed into the PS5 pairing fields, read the way a person types it, and which field to fix.
//
// Why (screenshots from people who could not pair, 24 November 2026): one message, "Enter a valid host
// address, Account ID and 8-digit pairing code", answered three different mistakes, and two of them are
// what the screen in front of the person suggests:
//   * the PS5 shows the pairing number as "1234 5678", and only eight digits with nothing between them
//     were taken;
//   * the field is called "PSN Account ID", and a PSN name was typed into it.
// So blanks and dashes in the number are not part of it, and the answer names the one field that is
// wrong, in the order the fields stand in the dialog.
//
// An account ID stays what it was: the decimal number, or the 12 characters ending in '='. A PSN name
// can be 11 letters, and 11 letters with a '=' behind them decode as somebody's account ID: nothing is
// guessed, and a name is answered as "not an account ID".
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include "veyra/remoteplay/Types.h"

namespace veyra::remoteplay::pairing {

// Blanks are never part of what was typed; with `separators`, neither are the dashes and dots someone
// puts between two groups of digits.
inline std::string withoutBlanks(std::string_view text, bool separators) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (separators && (c == '-' || c == '.')) continue;
        out.push_back(c);
    }
    return out;
}

// "1234 5678", "1234-5678" and "12345678" are one number.
inline std::string cleanPin(std::string_view text) { return withoutBlanks(text, true); }

// The account ID in either form it is handed out in. Nothing else: see the top of this file.
inline std::optional<AccountId> account(std::string_view text) {
    const auto typed = withoutBlanks(text, false);
    if (auto id = accountIdFromBase64(typed)) return id;
    return accountIdFromDecimal(typed);
}

enum class Problem { None, Host, Account, Pin };

struct Checked {
    Problem problem = Problem::None;
    std::string host;      // as it is used
    std::string pin;       // eight digits when there is no problem
    AccountId account{};
};

// The first field that has to be fixed, top to bottom; None means every value in the result is usable.
inline Checked check(std::string_view host, std::string_view accountText, std::string_view pinText) {
    Checked out;
    out.host = withoutBlanks(host, false);
    out.pin = cleanPin(pinText);
    if (!validHost(out.host)) { out.problem = Problem::Host; return out; }
    const auto id = account(accountText);
    if (!id) { out.problem = Problem::Account; return out; }
    out.account = *id;
    if (!parsePairingPin(out.pin)) out.problem = Problem::Pin;
    return out;
}

} // namespace veyra::remoteplay::pairing
