// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  license-manager.hpp  [Layer 2]  Commercial licence activation.
//
//  A normal paid-software activation flow. Nothing here attacks the host OS or
//  another process; it only validates a licence the customer purchased and
//  binds it to the machine they activated it on.
//
//    * HIDE_STR : compile-time XOR string obfuscation (constexpr), decoded into
//      a scope-local buffer that is zeroed on destruction, so the shipped
//      binary never carries the salt / endpoint as plain text for `strings`.
//    * Machine binding : a salted hash of QSysInfo::machineUniqueId().
//    * Dual auth : online (Cloudflare Worker, 2-device limit) with an offline
//      signed-token fallback (SHA-256 / FNV-1a) so a net outage never stops a
//      live stream.
//    * Input sanitising : full-width chars, spaces and full-width hyphens are
//      folded before the key is checked.
//    * Persistence : the signed token is stored in both QSettings and
//      obs_module_config_path so either survives the other being cleared.
//    * Deactivation : releases the seat on this PC for transfer to a new one.
//    * A clock-skew check turns the common "SSL handshake failed" into a clear
//      "your PC clock is wrong" message.
//
//  Qt6::Core + Qt6::Network. No OBS types are referenced directly; the config
//  path is injected by the caller so this stays portable to the VC Edition.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_LICENSE_MANAGER_HPP
#define SEVENTHWELL_MODEL600_LICENSE_MANAGER_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

namespace sw::model600::license {

// -----------------------------------------------------------------------------
//  Compile-time XOR string hiding.
//
//  HIDE_STR("literal") yields a HiddenString whose .get() returns the decoded
//  text. The ciphertext embedded in the binary is keyed per call site, so no
//  readable copy of the literal appears in the data section. The decoded
//  buffer lives on the stack and is wiped in the destructor.
// -----------------------------------------------------------------------------
namespace detail {

constexpr std::uint8_t xor_key(std::size_t i, std::uint32_t seed) noexcept {
    // A small LCG mixed with the index so each byte uses a different key.
    std::uint32_t x = seed + static_cast<std::uint32_t>(i) * 2654435761u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    return static_cast<std::uint8_t>(x & 0xFFu);
}

template <std::size_t N>
struct Obfuscated {
    std::array<std::uint8_t, N> data{};
    std::uint32_t seed = 0;
    constexpr Obfuscated(const char (&s)[N], std::uint32_t seed_in) : seed(seed_in) {
        for (std::size_t i = 0; i < N; ++i)
            data[i] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(s[i]) ^ xor_key(i, seed_in));
    }
};

} // namespace detail

template <std::size_t N>
class HiddenString {
public:
    explicit HiddenString(const detail::Obfuscated<N>& o) noexcept {
        for (std::size_t i = 0; i < N; ++i)
            buf_[i] = static_cast<char>(o.data[i] ^ detail::xor_key(i, o.seed));
    }
    ~HiddenString() { secure_wipe(buf_.data(), N); }
    HiddenString(const HiddenString&) = delete;
    HiddenString& operator=(const HiddenString&) = delete;

    const char* get() const noexcept { return buf_.data(); }
    QString qstr() const { return QString::fromUtf8(buf_.data()); }
    QByteArray bytes() const { return QByteArray(buf_.data(), static_cast<int>(N) - 1); }

    static void secure_wipe(char* p, std::size_t n) noexcept {
        volatile char* vp = p;
        while (n--) *vp++ = 0;
    }
private:
    std::array<char, N> buf_{};
};

// __COUNTER__ gives each call site a distinct key so identical literals differ.
#define M600_HIDE_SEED (0xA5B6C7D0u ^ (static_cast<std::uint32_t>(__LINE__) * 0x9E3779B1u) ^ (static_cast<std::uint32_t>(__COUNTER__) * 0x85EBCA77u))
#define HIDE_STR(str) ::sw::model600::license::HiddenString<sizeof(str)>( \
    []{ constexpr ::sw::model600::license::detail::Obfuscated<sizeof(str)> o{str, M600_HIDE_SEED}; return o; }())

// -----------------------------------------------------------------------------
//  FNV-1a (64-bit) used for the fast offline integrity tag.
// -----------------------------------------------------------------------------
inline std::uint64_t fnv1a64(const QByteArray& in, std::uint64_t seed = 1469598103934665603ull) noexcept {
    std::uint64_t h = seed;
    for (char c : in) { h ^= static_cast<std::uint8_t>(c); h *= 1099511628211ull; }
    return h;
}

enum class Status : int {
    Unknown = 0,
    Licensed,          // Valid (online or offline token).
    Trial,             // No key; Standard features only.
    Invalid,           // Key failed validation.
    SeatLimitReached,  // 2 devices already activated.
    NetworkError,      // Could not reach the server (offline token may cover).
    ClockSkew          // TLS failed because the system clock is wrong.
};

struct Result {
    Status  status = Status::Unknown;
    bool    pro = false;
    QString message;
    QString message_ja;
    int     seats_used = 0;
    int     seats_max = 2;
};

// -----------------------------------------------------------------------------
//  The activation endpoint + salt. These are the only secrets; both are hidden.
// -----------------------------------------------------------------------------
inline QString activation_base_url() {
    auto h = HIDE_STR("https://auth.seventhwell.workers.dev");
    return h.qstr();
}
inline QByteArray product_salt() {
    auto h = HIDE_STR("SEVENTHWELL::MODEL600::v1::6f3a9c2e8b14d7a0");
    return h.bytes();
}
inline QByteArray signing_pepper() {
    auto h = HIDE_STR("M600-OFFLINE-PEPPER-{a1b2c3d4e5f60718}");
    return h.bytes();
}

// -----------------------------------------------------------------------------
//  Input sanitiser: fold full-width ASCII, drop spaces / hyphen variants,
//  upper-case. "ＡＢＣ-1234" and "abc 1234" collapse to "ABC1234".
// -----------------------------------------------------------------------------
inline QString sanitize_key(const QString& raw) {
    QString out;
    out.reserve(raw.size());
    for (QChar qc : raw) {
        char32_t c = qc.unicode();
        if (c >= 0xFF01 && c <= 0xFF5E) c = c - 0xFF01 + 0x21;   // Full-width -> ASCII.
        if (c == 0x3000) continue;                              // Ideographic space.
        if (c == 0x2010 || c == 0x2011 || c == 0x2012 || c == 0x2013 ||
            c == 0x2014 || c == 0x2015 || c == 0xFF0D || c == '-') continue; // Hyphens.
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        QChar fc(static_cast<char16_t>(c));
        out.append(fc.toUpper());
    }
    return out;
}

// -----------------------------------------------------------------------------
//  Machine fingerprint: salted SHA-256 of the OS unique id, hex, 32 chars.
// -----------------------------------------------------------------------------
inline QString machine_id() {
    QByteArray uid = QSysInfo::machineUniqueId();
    if (uid.isEmpty()) uid = QSysInfo::machineHostName().toUtf8() + QSysInfo::productType().toUtf8();
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(product_salt());
    h.addData(uid);
    return QString::fromLatin1(h.result().toHex()).left(32);
}

// -----------------------------------------------------------------------------
//  Offline signed token: SHA-256(key | machine | pepper) + FNV tag, base64url.
//  The Cloudflare Worker issues exactly this string so online and offline paths
//  validate identically. Any tamper flips the hash and the token is rejected.
// -----------------------------------------------------------------------------
inline QByteArray make_offline_signature(const QString& key, const QString& mid) {
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(key.toUtf8());
    h.addData("|");
    h.addData(mid.toUtf8());
    h.addData("|");
    h.addData(signing_pepper());
    const QByteArray sha = h.result();
    // Mix the product salt into the FNV seed rather than XORing byte arrays
    // (QByteArray has no operator^); this keeps the tag salt-dependent.
    const std::uint64_t seed = fnv1a64(product_salt());
    const std::uint64_t tag = fnv1a64(sha, seed);
    QByteArray sig = sha;
    sig.append(reinterpret_cast<const char*>(&tag), static_cast<int>(sizeof(tag)));
    return sig.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

inline bool verify_offline_signature(const QString& key, const QString& mid, const QByteArray& token) {
    return !token.isEmpty() && token == make_offline_signature(key, mid);
}

// -----------------------------------------------------------------------------
//  LicenseManager
// -----------------------------------------------------------------------------
class LicenseManager : public QObject {
public:
    explicit LicenseManager(QString config_path, QObject* parent = nullptr)
        : QObject(parent), config_path_(std::move(config_path)) {
        load_persisted();
    }

    Status status() const noexcept { return status_; }
    bool is_pro() const noexcept { return pro_; }
    const QString& key() const noexcept { return key_; }
    QString machine() const { return machine_id(); }

    // The Pro DSP scale: 1.0f when licensed, 0.0f otherwise. The engine reads
    // this so an unlicensed binary cannot be patched to "true" to unlock Pro
    // processing -- the scale literally multiplies the ducking depth.
    float pro_scale() const noexcept { return pro_ ? 1.0f : 0.0f; }

    // Validate a stored token (offline). Called at startup; returns fast so a
    // live stream never stalls waiting on the network.
    Result validate_offline() {
        Result r; r.seats_max = 2;
        if (key_.isEmpty()) { status_ = Status::Trial; pro_ = false; r.status = Status::Trial; r.pro = false;
            r.message = QStringLiteral("Trial (Standard edition). Enter a key to unlock Pro.");
            r.message_ja = QStringLiteral("トライアル（Standard版）。Pro機能はキーを入力すると解除されます。"); return r; }
        if (verify_offline_signature(key_, machine_id(), token_)) {
            status_ = Status::Licensed; pro_ = true; r.status = Status::Licensed; r.pro = true;
            r.message = QStringLiteral("Licensed (offline token verified).");
            r.message_ja = QStringLiteral("認証済み（オフライン・トークン検証成功）。");
        } else {
            status_ = Status::Invalid; pro_ = false; r.status = Status::Invalid; r.pro = false;
            r.message = QStringLiteral("Stored licence does not match this PC. Re-activate online.");
            r.message_ja = QStringLiteral("保存済みライセンスがこのPCと一致しません。オンラインで再認証してください。");
        }
        return r;
    }

    // Activate `raw_key` online. Blocks up to `timeout_ms` on its own event
    // loop (safe to call from a worker thread). On success the signed token is
    // persisted; on a network failure a valid existing offline token is kept.
    Result activate_online(const QString& raw_key, int timeout_ms = 8000) {
        Result r; r.seats_max = 2;
        const QString key = sanitize_key(raw_key);
        if (key.size() < 8) {
            status_ = Status::Invalid; r.status = Status::Invalid;
            r.message = QStringLiteral("Key is too short.");
            r.message_ja = QStringLiteral("キーが短すぎます。"); return r;
        }
        const QString mid = machine_id();

        QNetworkAccessManager nam;
        QUrl url(activation_base_url() + QStringLiteral("/activate"));
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        req.setRawHeader(QByteArrayLiteral("X-M600-Client"), QByteArrayLiteral("MODEL600/1.0"));
        req.setTransferTimeout(timeout_ms);

        QJsonObject body;
        body.insert(QStringLiteral("key"), key);
        body.insert(QStringLiteral("machine"), mid);
        body.insert(QStringLiteral("product"), QStringLiteral("MODEL600"));
        body.insert(QStringLiteral("client_time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

        QEventLoop loop;
        QNetworkReply* reply = nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QTimer guard; guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        guard.start(timeout_ms + 1000);
        loop.exec();

        if (!reply->isFinished()) { reply->abort(); }
        const QNetworkReply::NetworkError err = reply->error();
        const QByteArray payload = reply->readAll();
        const QString err_str = reply->errorString();
        reply->deleteLater();

        if (err != QNetworkReply::NoError) {
            // Distinguish a wrong-clock TLS failure from a plain outage.
            if (is_clock_skew_error(err, err_str)) {
                status_ = Status::ClockSkew; r.status = Status::ClockSkew;
                r.message = QStringLiteral("TLS failed: your PC clock looks wrong. Fix the date/time and retry.");
                r.message_ja = QStringLiteral("SSLエラー：PCの時計がずれている可能性があります。日付と時刻を修正して再試行してください。");
                return keep_existing_offline_or(r, key, mid);
            }
            status_ = Status::NetworkError; r.status = Status::NetworkError;
            r.message = QStringLiteral("Could not reach the activation server. Offline token (if any) stays active.");
            r.message_ja = QStringLiteral("認証サーバーに接続できませんでした。既存のオフライン・トークンがあれば有効のままです。");
            return keep_existing_offline_or(r, key, mid);
        }

        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        const bool ok = obj.value(QStringLiteral("ok")).toBool(false);
        r.seats_used = obj.value(QStringLiteral("seats_used")).toInt(0);
        r.seats_max = obj.value(QStringLiteral("seats_max")).toInt(2);
        if (!ok) {
            const QString reason = obj.value(QStringLiteral("reason")).toString();
            if (reason == QStringLiteral("seat_limit")) {
                status_ = Status::SeatLimitReached; r.status = Status::SeatLimitReached;
                r.message = QStringLiteral("This key is already active on %1 of %2 PCs. Deactivate one first.")
                                .arg(r.seats_used).arg(r.seats_max);
                r.message_ja = QStringLiteral("このキーは既に%1台/%2台で有効です。どれか1台を認証解除してください。")
                                .arg(r.seats_used).arg(r.seats_max);
            } else {
                status_ = Status::Invalid; r.status = Status::Invalid;
                r.message = QStringLiteral("Key rejected: %1").arg(reason.isEmpty() ? QStringLiteral("invalid") : reason);
                r.message_ja = QStringLiteral("キーが拒否されました：%1").arg(reason.isEmpty() ? QStringLiteral("無効") : reason);
            }
            pro_ = false; return r;
        }

        // Server returns its own signed token; we also recompute locally and
        // require a match, so a spoofed "ok:true" with no valid token fails.
        const QByteArray server_token = obj.value(QStringLiteral("token")).toString().toUtf8();
        const QByteArray local_token = make_offline_signature(key, mid);
        if (!server_token.isEmpty() && server_token != local_token) {
            status_ = Status::Invalid; r.status = Status::Invalid; pro_ = false;
            r.message = QStringLiteral("Server token mismatch. Activation refused.");
            r.message_ja = QStringLiteral("サーバー・トークンが一致しません。認証を中止しました。");
            return r;
        }

        key_ = key; token_ = local_token; status_ = Status::Licensed; pro_ = true;
        persist();
        r.status = Status::Licensed; r.pro = true;
        r.message = QStringLiteral("Activation successful. Pro features unlocked.");
        r.message_ja = QStringLiteral("認証に成功しました。Pro機能が有効になりました。");
        return r;
    }

    // Release this PC's seat so the key can move to a new machine.
    Result deactivate(int timeout_ms = 8000) {
        Result r;
        if (key_.isEmpty()) { r.status = Status::Trial; r.message = QStringLiteral("Nothing to deactivate.");
            r.message_ja = QStringLiteral("解除する認証がありません。"); return r; }
        const QString mid = machine_id();
        QNetworkAccessManager nam;
        QNetworkRequest req(QUrl(activation_base_url() + QStringLiteral("/deactivate")));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        req.setTransferTimeout(timeout_ms);
        QJsonObject body; body.insert(QStringLiteral("key"), key_); body.insert(QStringLiteral("machine"), mid);
        QEventLoop loop;
        QNetworkReply* reply = nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(timeout_ms + 1000, &loop, &QEventLoop::quit);
        loop.exec();
        const QNetworkReply::NetworkError err = reply->error();
        reply->deleteLater();
        // Clear local state regardless so the user can walk away from a dead PC.
        clear_persisted();
        key_.clear(); token_.clear(); pro_ = false; status_ = Status::Trial;
        if (err != QNetworkReply::NoError) {
            r.status = Status::NetworkError;
            r.message = QStringLiteral("Deactivated locally. The server seat will free on next online check.");
            r.message_ja = QStringLiteral("ローカルで認証解除しました。サーバー側の枠は次回オンライン時に解放されます。");
        } else {
            r.status = Status::Trial;
            r.message = QStringLiteral("Deactivated. This key is now free to activate on another PC.");
            r.message_ja = QStringLiteral("認証を解除しました。このキーは別のPCで認証できます。");
        }
        return r;
    }

private:
    static bool is_clock_skew_error(QNetworkReply::NetworkError err, const QString& text) {
        if (err == QNetworkReply::SslHandshakeFailedError) return true;
        const QString t = text.toLower();
        return t.contains(QStringLiteral("certificate")) &&
               (t.contains(QStringLiteral("expired")) || t.contains(QStringLiteral("not yet valid")) ||
                t.contains(QStringLiteral("date")));
    }

    Result keep_existing_offline_or(Result net_result, const QString& key, const QString& mid) {
        if (verify_offline_signature(key_.isEmpty() ? key : key_, mid, token_)) {
            pro_ = true; status_ = Status::Licensed;
            net_result.pro = true; net_result.status = Status::Licensed;
            net_result.message += QStringLiteral(" (Existing offline licence remains valid.)");
            net_result.message_ja += QStringLiteral("（既存のオフライン・ライセンスは有効です。）");
        } else {
            pro_ = false;
        }
        return net_result;
    }

    QString settings_org() const { auto h = HIDE_STR("SEVENTHWELL"); return h.qstr(); }
    QString settings_app() const { auto h = HIDE_STR("MODEL600"); return h.qstr(); }

    void persist() {
        QSettings s(QSettings::IniFormat, QSettings::UserScope, settings_org(), settings_app());
        s.setValue(QStringLiteral("lic/key"), key_);
        s.setValue(QStringLiteral("lic/token"), QString::fromLatin1(token_));
        s.setValue(QStringLiteral("lic/machine"), machine_id());
        s.sync();
        write_config_file();
    }
    void write_config_file() {
        if (config_path_.isEmpty()) return;
        QFileInfo fi(config_path_);
        QDir().mkpath(fi.absolutePath());
        QSettings s(config_path_, QSettings::IniFormat);
        s.setValue(QStringLiteral("lic/key"), key_);
        s.setValue(QStringLiteral("lic/token"), QString::fromLatin1(token_));
        s.setValue(QStringLiteral("lic/machine"), machine_id());
        s.sync();
    }
    void load_persisted() {
        // Prefer QSettings; fall back to the OBS config file.
        QSettings s(QSettings::IniFormat, QSettings::UserScope, settings_org(), settings_app());
        key_ = s.value(QStringLiteral("lic/key")).toString();
        token_ = s.value(QStringLiteral("lic/token")).toString().toLatin1();
        if ((key_.isEmpty() || token_.isEmpty()) && !config_path_.isEmpty()) {
            QSettings c(config_path_, QSettings::IniFormat);
            if (key_.isEmpty()) key_ = c.value(QStringLiteral("lic/key")).toString();
            if (token_.isEmpty()) token_ = c.value(QStringLiteral("lic/token")).toString().toLatin1();
        }
        validate_offline();
    }
    void clear_persisted() {
        QSettings s(QSettings::IniFormat, QSettings::UserScope, settings_org(), settings_app());
        s.remove(QStringLiteral("lic"));
        s.sync();
        if (!config_path_.isEmpty()) {
            QSettings c(config_path_, QSettings::IniFormat);
            c.remove(QStringLiteral("lic"));
            c.sync();
        }
    }

    QString config_path_;
    QString key_;
    QByteArray token_;
    Status status_ = Status::Unknown;
    bool pro_ = false;
};

} // namespace sw::model600::license

#endif // SEVENTHWELL_MODEL600_LICENSE_MANAGER_HPP
