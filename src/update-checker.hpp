// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  update-checker.hpp  [Layer 2]  Asynchronous version monitor.
//
//  Fetches a version manifest (Cloudflare first, GitHub Releases as a mirror),
//  compares semantic versions, and raises a non-intrusive flag the UI turns
//  into an amber header ramp -- it never pops a modal and never blocks audio.
//  Fully async on the Qt event loop; a failed check is silent and simply
//  retried on the next interval.
//
//  Qt6::Core + Qt6::Network.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_UPDATE_CHECKER_HPP
#define SEVENTHWELL_MODEL600_UPDATE_CHECKER_HPP

#include <array>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace sw::model600::update {

// Semantic version triple; trailing text (e.g. "-beta") is ignored for order.
struct Version {
    int major = 0, minor = 0, patch = 0;
    QString raw;
    static Version parse(const QString& s) {
        Version v; v.raw = s.trimmed();
        QString core = v.raw;
        core.remove(QRegularExpression(QStringLiteral("^[vV]")));
        const int dash = core.indexOf(QLatin1Char('-'));
        if (dash >= 0) core = core.left(dash);
        const QStringList parts = core.split(QLatin1Char('.'));
        if (parts.size() > 0) v.major = parts[0].toInt();
        if (parts.size() > 1) v.minor = parts[1].toInt();
        if (parts.size() > 2) v.patch = parts[2].toInt();
        return v;
    }
    bool operator<(const Version& o) const {
        if (major != o.major) return major < o.major;
        if (minor != o.minor) return minor < o.minor;
        return patch < o.patch;
    }
    bool operator>(const Version& o) const { return o < *this; }
    QString toString() const { return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch); }
};

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QString current_version, QObject* parent = nullptr)
        : QObject(parent), current_(Version::parse(current_version)) {
        nam_ = new QNetworkAccessManager(this);
        timer_ = new QTimer(this);
        timer_->setInterval(6 * 60 * 60 * 1000); // Every 6 hours.
        connect(timer_, &QTimer::timeout, this, &UpdateChecker::check_now);
    }

    void start() { QTimer::singleShot(5000, this, &UpdateChecker::check_now); timer_->start(); }
    void stop() { timer_->stop(); if (active_reply_) { active_reply_->abort(); } }

    bool update_available() const noexcept { return available_; }
    QString latest_version() const { return latest_.toString(); }
    QString download_url() const { return download_url_; }
    QString release_notes() const { return notes_; }

signals:
    // Emitted on the UI thread when a newer version is found. The console lights
    // its amber "[UPDATE vX.Y READY]" ramp in response.
    void updateFound(QString version, QString url, QString notes);

public slots:
    void check_now() {
        if (active_reply_) return; // One in flight at a time.
        try_source(0);
    }

private:
    void try_source(int idx) {
        if (idx >= static_cast<int>(sources_.size())) return; // Exhausted; silent.
        QNetworkRequest req{QUrl(sources_[static_cast<std::size_t>(idx)])};
        req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
        req.setRawHeader(QByteArrayLiteral("User-Agent"), QByteArrayLiteral("MODEL600-UpdateChecker/1.0"));
        req.setTransferTimeout(7000);
        active_reply_ = nam_->get(req);
        connect(active_reply_, &QNetworkReply::finished, this, [this, idx]() {
            QNetworkReply* reply = active_reply_;
            active_reply_ = nullptr;
            const QByteArray body = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!ok || !parse_manifest(body)) { try_source(idx + 1); return; }
        });
    }

    bool parse_manifest(const QByteArray& body) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject() && !doc.isArray()) return false;
        QString ver, url, notes;
        if (doc.isObject()) {
            const QJsonObject o = doc.object();
            // Our own version.json schema.
            ver = o.value(QStringLiteral("latest")).toString(o.value(QStringLiteral("version")).toString());
            url = o.value(QStringLiteral("url")).toString(o.value(QStringLiteral("download")).toString());
            notes = o.value(QStringLiteral("notes")).toString();
            // GitHub single-release schema.
            if (ver.isEmpty()) ver = o.value(QStringLiteral("tag_name")).toString();
            if (url.isEmpty()) url = o.value(QStringLiteral("html_url")).toString();
            if (notes.isEmpty()) notes = o.value(QStringLiteral("body")).toString();
        } else {
            // GitHub releases array -> first non-draft, non-prerelease.
            for (const QJsonValue& v : doc.array()) {
                const QJsonObject o = v.toObject();
                if (o.value(QStringLiteral("draft")).toBool() || o.value(QStringLiteral("prerelease")).toBool()) continue;
                ver = o.value(QStringLiteral("tag_name")).toString();
                url = o.value(QStringLiteral("html_url")).toString();
                notes = o.value(QStringLiteral("body")).toString();
                break;
            }
        }
        if (ver.isEmpty()) return false;
        const Version latest = Version::parse(ver);
        if (latest > current_) {
            latest_ = latest; download_url_ = url; notes_ = notes; available_ = true;
            emit updateFound(latest.toString(), url, notes);
        }
        return true;
    }

    QNetworkAccessManager* nam_ = nullptr;
    QTimer* timer_ = nullptr;
    QNetworkReply* active_reply_ = nullptr;
    Version current_, latest_;
    QString download_url_, notes_;
    bool available_ = false;
    std::array<QString, 2> sources_{
        QStringLiteral("https://cdn.seventhwell.com/model600/version.json"),
        QStringLiteral("https://api.github.com/repos/seventhwell/model600/releases")
    };
};

} // namespace sw::model600::update

#endif // SEVENTHWELL_MODEL600_UPDATE_CHECKER_HPP
