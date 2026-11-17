// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS signed updates: signature check (Windows CNG) and the signed checksum list.
#include "veyra/ui/UpdateVerify.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#endif

namespace veyra::ui {

bool verifyUpdateSignature(const QByteArray& publicKeyPem, const QByteArray& data, const QByteArray& signature, QString* reason) {
    const auto fail = [&](const char* why) { if (reason) *reason = QString::fromLatin1(why); return false; };
    if (publicKeyPem.isEmpty()) return fail("no-public-key");
    if (signature.isEmpty() || signature.size() > 1024) return fail("bad-signature-size");
#ifdef _WIN32
    // PEM -> DER -> CERT_PUBLIC_KEY_INFO -> CNG key.
    DWORD derSize = 0;
    if (!CryptStringToBinaryA(publicKeyPem.constData(), DWORD(publicKeyPem.size()), CRYPT_STRING_BASE64HEADER, nullptr, &derSize, nullptr, nullptr) || !derSize)
        return fail("bad-pem");
    QByteArray der(int(derSize), '\0');
    if (!CryptStringToBinaryA(publicKeyPem.constData(), DWORD(publicKeyPem.size()), CRYPT_STRING_BASE64HEADER,
                              reinterpret_cast<BYTE*>(der.data()), &derSize, nullptr, nullptr))
        return fail("bad-pem");
    CERT_PUBLIC_KEY_INFO* info = nullptr;
    DWORD infoSize = 0;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, reinterpret_cast<const BYTE*>(der.constData()), derSize,
                             CRYPT_DECODE_ALLOC_FLAG, nullptr, &info, &infoSize) || !info)
        return fail("bad-key-info");
    if (std::strcmp(info->Algorithm.pszObjId, szOID_RSA_RSA) != 0) { LocalFree(info); return fail("not-rsa"); }
    BCRYPT_KEY_HANDLE key = nullptr;
    const BOOL imported = CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING, info, 0, nullptr, &key);
    LocalFree(info);
    if (!imported || !key) return fail("key-import");
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    BCRYPT_PSS_PADDING_INFO pss{BCRYPT_SHA256_ALGORITHM, 32};
    const NTSTATUS status = BCryptVerifySignature(key, &pss, reinterpret_cast<PUCHAR>(const_cast<char*>(hash.constData())), ULONG(hash.size()),
                                                  reinterpret_cast<PUCHAR>(const_cast<char*>(signature.constData())), ULONG(signature.size()),
                                                  BCRYPT_PAD_PSS);
    BCryptDestroyKey(key);
    if (status != 0) return fail("signature-mismatch");
    return true;
#else
    (void)data;
    return fail("unsupported-platform");
#endif
}

std::optional<QByteArray> updateSumFor(const QByteArray& sums, const QString& fileName) {
    if (sums.size() > 256 * 1024) return std::nullopt;
    static const QRegularExpression line(QStringLiteral("^([0-9a-fA-F]{64}) [ *](.+?)\\r?$"));
    for (const QByteArray& raw : sums.split('\n')) {
        const auto m = line.match(QString::fromUtf8(raw));
        if (m.hasMatch() && m.captured(2) == fileName) return m.captured(1).toLower().toLatin1();
    }
    return std::nullopt;
}

std::optional<UpdatePackage> updatePackageIn(const QByteArray& sums) {
    if (sums.size() > 256 * 1024) return std::nullopt;
    static const QRegularExpression line(QStringLiteral("^([0-9a-fA-F]{64}) [ *](PushFPS-([0-9]+\\.[0-9]+\\.[0-9]+)-win64\\.zip)\\r?$"));
    std::optional<UpdatePackage> found;
    for (const QByteArray& raw : sums.split('\n')) {
        const auto m = line.match(QString::fromUtf8(raw));
        if (!m.hasMatch()) continue;
        if (found) return std::nullopt;   // two packages: ambiguous, refuse
        found = UpdatePackage{m.captured(2), QVersionNumber::fromString(m.captured(3)), m.captured(1).toLower().toLatin1()};
    }
    return found;
}

std::optional<InstalledPushFps> parseInstalledVersionFile(const QByteArray& text) {
    InstalledPushFps out;
    bool runtime = false;
    static const QRegularExpression gpu(QStringLiteral("^Runtime: Veyra-[^ ]+-(NVIDIA|AMD)-win64-portable\\.zip"));
    for (const QByteArray& raw : text.split('\n')) {
        const QString l = QString::fromUtf8(raw).trimmed();
        if (const auto m = gpu.match(l); m.hasMatch()) { out.gpu = m.captured(1); runtime = true; }
        if (l.startsWith(QStringLiteral("Not installed:"))) out.extras = false;
    }
    if (!runtime) return std::nullopt;
    return out;
}

} // namespace veyra::ui
