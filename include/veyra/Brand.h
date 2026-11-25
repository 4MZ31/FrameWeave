#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS product identity: the one place that names the product and its GitHub home.
//
// PushFPS is built on Veyra (https://github.com/Likely7/Veyra-NRVideo, GPL-3.0). Internal
// identifiers (namespaces, target names, the qml/Veyra module, window classes, data folders)
// keep Veyra's names on purpose so upstream fixes keep merging cleanly; only what a user sees
// uses these constants.

namespace veyra::brand {
inline constexpr const char* kProductName = "PushFPS";
inline constexpr const wchar_t* kProductNameW = L"PushFPS";

// GitHub repository that publishes PushFPS releases. The update checker only accepts a release
// page under kReleaseTagPathPrefix, so these must match the repository's exact owner/name case.
inline constexpr const char* kRepositoryUrl = "https://github.com/4MZ31/PushFPS";
inline constexpr const char* kIssuesUrl = "https://github.com/4MZ31/PushFPS/issues";
inline constexpr const char* kReleasesUrl = "https://github.com/4MZ31/PushFPS/releases";
inline constexpr const char* kLatestReleaseApi = "https://api.github.com/repos/4MZ31/PushFPS/releases/latest";
inline constexpr const char* kReleaseTagPathPrefix = "/4MZ31/PushFPS/releases/tag/";

// The fork has no update signing key. Its releases are installed manually.
inline constexpr const char* kUpdatePublicKeyPem = "";
inline constexpr const char* kReleaseDownloadPathPrefix = "/4MZ31/PushFPS/releases/download/";

// The project PushFPS is built on; shown in About and the README credits.
inline constexpr const char* kUpstreamName = "Veyra";
inline constexpr const char* kUpstreamUrl = "https://github.com/Likely7/Veyra-NRVideo";

// Veyra's ANR (Apex Neural Rendering) frame generation, offered exactly as Veyra 2.1.6 does: the
// host is in the program, but ANR's own runtime may not be redistributed yet, so no ANR file ships.
// The choice is usable once the user places ANR's files in runtime\experimental\anr. False would
// hide it from every menu and report it unavailable.
inline constexpr bool kAnrFrameGeneration = true;
// ANR is made by Apex Compute (credited in About, the README and the components page). The
// app links to their releases and installs the package a user downloaded from there into
// runtime\experimental\anr on that user's PC; PushFPS itself never ships or mirrors it.
inline constexpr const char* kAnrName = "Apex Neural Rendering (ANR)";
inline constexpr const char* kAnrMaker = "Apex Compute";
inline constexpr const char* kAnrUrl = "https://github.com/apex-compute/ANR";
inline constexpr const char* kAnrReleasesUrl = "https://github.com/apex-compute/ANR/releases";
// PushFlow (PushFPS's own frame generation) runs RIFE (MIT, hzwer); credited in About.
inline constexpr const char* kRifeUrl = "https://github.com/hzwer/Practical-RIFE";
inline constexpr const char* kApexUrl = "https://apexcompute.com";
// PushFPS is made by DosukaSOL; credited in About with a link to X.
inline constexpr const char* kCreatorName = "DosukaSOL";
inline constexpr const char* kCreatorUrl = "https://x.com/DosukaSOL";
// The PushFPS community (the same invitation the README and the website show); About row about-open-discord.
inline constexpr const char* kDiscordUrl = "https://discord.gg/N88znNpSgC";
}
