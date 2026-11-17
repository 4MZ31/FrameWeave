// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS signed updates: a release's SHA256SUMS.txt is signed in CI with the project's RSA key
// (RSA-PSS, SHA-256, 32-byte salt; scripts/release/publish.sh) and the app carries the public key
// (Brand.h kUpdatePublicKeyPem). An update is installed only when the signature verifies, the signed
// list names the downloaded zip with that exact SHA-256, and the zip's version is the newer release.
#pragma once
#include <QByteArray>
#include <QString>
#include <QVersionNumber>
#include <optional>

namespace veyra::ui {

// Verifies an RSA-PSS SHA-256 signature (salt 32) over data with a PEM "PUBLIC KEY" (SubjectPublicKeyInfo).
// Uses Windows CNG. False with a reason for a malformed key or signature, or a mismatch.
bool verifyUpdateSignature(const QByteArray& publicKeyPem, const QByteArray& data, const QByteArray& signature, QString* reason = nullptr);

// The SHA-256 (lower-case hex) listed for fileName in a sha256sum-style list ("<hex>  <name>" or "<hex> *<name>").
std::optional<QByteArray> updateSumFor(const QByteArray& sums, const QString& fileName);

// The PushFPS release zip named in a signed list: "PushFPS-X.Y.Z-win64.zip" (stable versions only).
struct UpdatePackage {
    QString fileName;
    QVersionNumber version;
    QByteArray sha256;
};
std::optional<UpdatePackage> updatePackageIn(const QByteArray& sums);

// What the installer needs to repeat an existing install: the GPU package and whether the experimental
// extras were installed (from PushFPS-VERSION.txt, written by Install-PushFPS.ps1).
struct InstalledPushFps {
    QString gpu;            // "NVIDIA" or "AMD"
    bool extras = true;     // false when the file lists "Not installed:" items
};
std::optional<InstalledPushFps> parseInstalledVersionFile(const QByteArray& text);

} // namespace veyra::ui
