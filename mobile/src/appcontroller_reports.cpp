// VitaParms Tablet – Ausgaben (Reiter „Berichte“): alle Berichte als PDF bzw. Excel öffnen oder
// über das Teilen-Menü von Android weitergeben (einzeln, mehrere, auf Wunsch als verschlüsseltes
// ZIP-Archiv), dazu die Website mit den aktuellen Daten aktualisieren (Teil von AppController).
// SPDX-License-Identifier: MIT
#include "appcontroller.h"

#include "database.h"
#include "dbsync.h"
#include "medstock.h"
#include "websync.h"
#include "wounds.h"
#include "zipwriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#endif

namespace {
QString mimeFor(const QStringList &files)
{
    QString mime;
    for (const QString &f : files) {
        const QString suffix = QFileInfo(f).suffix().toLower();
        const QString m = suffix == QLatin1String("pdf") ? QStringLiteral("application/pdf")
                        : suffix == QLatin1String("zip") ? QStringLiteral("application/zip")
                        : suffix == QLatin1String("xlsx") ? QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet")
                                                          : QStringLiteral("application/octet-stream");
        if (mime.isEmpty()) mime = m;
        else if (mime != m) return QStringLiteral("*/*");
    }
    return mime.isEmpty() ? QStringLiteral("*/*") : mime;
}
} // namespace

// ---------------------------------------------------------------- Teilen (Android-Teilen-Menü)

bool AppController::shareFiles(const QStringList &files, const QString &subject)
{
    if (files.isEmpty()) { setError(tr("Es gibt nichts zu teilen.")); return false; }
    for (const QString &f : files)
        if (!QFileInfo::exists(f)) { setError(tr("Die Datei %1 fehlt.").arg(QFileInfo(f).fileName())); return false; }
#ifdef Q_OS_ANDROID
    const QJniObject ctx(QNativeInterface::QAndroidApplication::context());
    const jboolean ok = QJniObject::callStaticMethod<jboolean>("de/mbergmann/vitaparms/Capture", "share",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
        ctx.object(), QJniObject::fromString(files.join(QLatin1Char('\n'))).object<jstring>(),
        QJniObject::fromString(mimeFor(files)).object<jstring>(), QJniObject::fromString(subject).object<jstring>(),
        QJniObject::fromString(tr("Teilen mit …")).object<jstring>());
    if (!ok) { setError(tr("Teilen ist auf diesem Gerät nicht möglich.")); return false; }
#else
    m_lastShared = files;            // Desktop-Test/Autopilot: nur merken
    Q_UNUSED(subject)
#endif
    return true;
}

// ---------------------------------------------------------------- Berichte

QVariantList AppController::reportCatalog(qint64 pid)
{
    QVariantList l;
    if (!loggedIn() || !pid) return l;
    Database &db = Database::instance();
    const QList<VitalRecord> recs = db.vitals(pid, {}, {});
    bool stool = false, glucose = false, fluid = false;
    for (const VitalRecord &r : recs) {
        stool = stool || r.stool;
        glucose = glucose || r.glucose;
        fluid = fluid || r.fluidIn || r.fluidOut;
    }
    const QList<MedStock::Order> orders = MedStock::orders(pid);
    const bool preps = !db.preparations(pid, {}, {}).isEmpty();
    auto add = [&](const QString &kind, const QString &group, const QString &title, const QString &desc, bool xlsx, bool period,
                   bool available = true, const QVariantMap &opts = {}) {
        l << QVariantMap{ { QStringLiteral("kind"), kind }, { QStringLiteral("group"), group }, { QStringLiteral("title"), title },
                          { QStringLiteral("description"), desc }, { QStringLiteral("xlsx"), xlsx }, { QStringLiteral("period"), period },
                          { QStringLiteral("available"), available }, { QStringLiteral("options"), opts } };
    };
    const QString gPerson = tr("Person und Medikamente"), gValues = tr("Messwerte"), gProt = tr("Protokolle");
    add(QStringLiteral("sheet"), gPerson, tr("Stammblatt"), tr("Angaben zur Person, Diagnosen, Kontakte, Vorsorge, Medikamente"), false, false);
    add(QStringLiteral("medplan"), gPerson, tr("Medikamentenplan"), tr("aktueller Plan mit Einnahmezeiten, zum Ausdrucken"), false, false);
    add(QStringLiteral("pharmacy"), gPerson, tr("Liste für die Apotheke"),
        tr("alle Arzneimittel mit Wirkstoff und PZN – für die Prüfung auf Wechselwirkungen"), false, false);
    if (!orders.isEmpty()) {
        const MedStock::Order &o = orders.first();
        add(QStringLiteral("order"), gPerson, tr("Bestellliste"), tr("vom %1 – %2").arg(QLocale().toString(o.createdAt, QStringLiteral("dd.MM.yyyy")), o.statusText()),
            false, false, true, { { QStringLiteral("orderId"), o.id } });
    }
    add(QStringLiteral("journal"), gValues, tr("Verlauf (alle Messwerte)"), tr("Tabelle mit allen Werten, neueste zuerst"), true, true, !recs.isEmpty());
    add(QStringLiteral("chart"), gValues, tr("Verlaufskurven"), tr("Kurven mit Legende und Kennwerten (DIN A4)"), true, true, !recs.isEmpty());
    add(QStringLiteral("glucose"), gProt, tr("BZ-Tagesprofile"), tr("Blutzuckerwerte je Tag, nach Uhrzeit geordnet"), true, true, glucose);
    add(QStringLiteral("stool"), gProt, tr("Stuhlprotokoll"), tr("Stuhlgänge mit Bristol-Typ und Angaben, Tage ohne Stuhlgang"), true, true, stool);
    add(QStringLiteral("fluid"), gProt, tr("Ein- und Ausfuhr (Bilanz)"), tr("Einträge mit Tagessummen und Bilanz"), true, true, fluid);
    add(QStringLiteral("preplog"), gProt, tr("Protokoll Medikamente stellen"), tr("wann von wem was gestellt wurde"), false, true, preps);
    // Wunddokumentation: je Wunde (offene zuerst)
    for (const Wounds::Wound &w : Wounds::wounds(pid))
        add(QStringLiteral("wound-%1").arg(w.id), tr("Wunddokumentation"), w.title(),
            w.status == Wounds::Open ? tr("Wundanamnese, Chronik mit Fotos und Flächenverlauf")
                                     : tr("%1 am %2").arg(Wounds::statusNames().value(w.status), QLocale().toString(w.closedAt, QStringLiteral("dd.MM.yyyy"))),
            true, false);
    return l;
}

// Zeitraum (Tage, 0 = alles) in die Optionen der jeweiligen Ausgabe übersetzen
static QVariantMap optionsFor(const QString &kind, int days, const QVariantMap &base)
{
    QVariantMap o = base;
    if (kind.startsWith(QLatin1String("chart"))) {
        o.insert(QStringLiteral("scale"), days > 0 && days <= 1 ? 0 : days > 0 && days <= 7 ? 1 : days > 0 && days <= 31 ? 2 : 3);
    } else if (kind == QLatin1String("glucose") || kind == QLatin1String("glucoseXlsx")) {
        o.insert(QStringLiteral("days"), days);
        if (!o.contains(QStringLiteral("minValues"))) o.insert(QStringLiteral("minValues"), 1);
    } else {
        o.insert(QStringLiteral("days"), days);
    }
    return o;
}

QString AppController::createReport(qint64 pid, const QString &kind, bool xlsx, int days, const QVariantMap &base)
{
    const QString k = xlsx ? (kind == QLatin1String("journal") ? QStringLiteral("journalXlsx") : kind + QStringLiteral("Xlsx")) : kind;
    return exportReport(k, pid, optionsFor(k, days, base));
}

QVariantMap AppController::shareReports(qint64 pid, const QVariantList &items, const QVariantMap &opts)
{
    QVariantMap r{ { QStringLiteral("ok"), false } };
    const auto p = Database::instance().patient(pid);
    if (!p) return r;
    if (items.isEmpty()) { setError(tr("Bitte mindestens einen Bericht wählen.")); return r; }
    const int days = opts.value(QStringLiteral("days")).toInt();
    QStringList files;
    for (const QVariant &v : items) {
        const QVariantMap it = v.toMap();
        const QString f = createReport(pid, it.value(QStringLiteral("kind")).toString(), it.value(QStringLiteral("xlsx")).toBool(), days,
                                       it.value(QStringLiteral("options")).toMap());
        if (f.isEmpty()) return r;      // Fehlertext steht in lastError
        files << f;
    }
    const bool zip = opts.value(QStringLiteral("zip")).toBool();
    if (zip) {
        const QString password = opts.value(QStringLiteral("password")).toString();
        if (!password.isEmpty() && password.size() < 8) { setError(tr("Das Passwort sollte mindestens 8 Zeichen lang sein.")); return r; }
        ZipWriter z;
        if (!password.isEmpty()) z.setPassword(password);
        for (const QString &f : std::as_const(files)) {
            QFile in(f);
            if (!in.open(QIODevice::ReadOnly)) { setError(in.errorString()); return r; }
            z.addFile(QFileInfo(f).fileName(), in.readAll());
        }
        QString base = p->displayName();
        base.replace(QRegularExpression(QStringLiteral("[^\\w\\-]+")), QStringLiteral("_"));
        const QString path = QFileInfo(files.first()).absolutePath()
                             + QStringLiteral("/VitaParms_%1_%2.zip").arg(base, QDate::currentDate().toString(Qt::ISODate));
        QString err;
        if (!z.save(path, &err)) { setError(err); return r; }
        files = { path };
    }
    r.insert(QStringLiteral("files"), files);
    if (!shareFiles(files, tr("VitaParms – %1").arg(p->displayName()))) return r;
    r.insert(QStringLiteral("ok"), true);
    return r;
}

// ---------------------------------------------------------------- Website aktualisieren

QVariantMap AppController::websiteConfig()
{
    QVariantMap m;
    if (!loggedIn()) return m;
    WebSync::Config c = WebSync::config();
    const DbSync::Config sc = DbSync::config();
    // Adresse und Upload-Schlüssel vom Abgleich vorschlagen (dieselbe Website)
    m.insert(QStringLiteral("enabled"), c.enabled);
    m.insert(QStringLiteral("url"), c.url.isEmpty() ? sc.url : c.url);
    m.insert(QStringLiteral("uploadKey"), c.uploadKey.isEmpty() ? sc.uploadKey : c.uploadKey);
    m.insert(QStringLiteral("hasPassword"), !c.password.isEmpty());
    m.insert(QStringLiteral("afterSync"), c.autoPublish);
    m.insert(QStringLiteral("last"), c.lastUpload.isValid() ? QLocale().toString(c.lastUpload.toLocalTime(), QLocale::ShortFormat) : QString());
    m.insert(QStringLiteral("lastResult"), c.lastResult);
    QString why;
    m.insert(QStringLiteral("ready"), WebSync::isReady(c, &why));
    m.insert(QStringLiteral("why"), why);
    m.insert(QStringLiteral("busy"), WebSync::instance().isBusy());
    return m;
}

bool AppController::saveWebsiteConfig(const QVariantMap &m)
{
    if (!loggedIn()) return false;
    WebSync::Config c = WebSync::config();
    c.enabled = m.value(QStringLiteral("enabled")).toBool();
    c.url = WebSync::normalizedUrl(m.value(QStringLiteral("url")).toString());
    c.uploadKey = m.value(QStringLiteral("uploadKey")).toString().trimmed();
    c.autoPublish = m.value(QStringLiteral("afterSync")).toBool();
    const QString pw = m.value(QStringLiteral("password")).toString();
    if (!pw.isEmpty()) {
        if (pw != m.value(QStringLiteral("repeat")).toString()) { setError(tr("Die beiden Passwörter stimmen nicht überein.")); return false; }
        c.password = pw;
    }
    QString why;
    if (c.enabled && !WebSync::isReady(c, &why)) { setError(why); return false; }
    WebSync::saveConfig(c);
    emit websiteChanged();
    return true;
}

bool AppController::publishWebsite()
{
    QString why;
    if (!WebSync::isReady(WebSync::config(), &why)) { setError(why); return false; }
    WebSync::instance().publish();
    emit websiteChanged();
    return true;
}

bool AppController::publishWebsiteBlocking(int timeoutMs)
{
    QString msg;
    const bool ok = WebSync::instance().publishBlocking(timeoutMs, &msg);
    if (!ok) setError(msg);
    emit websiteChanged();
    return ok;
}

void AppController::setupWebsiteSignals()
{
    WebSync &ws = WebSync::instance();
    connect(&ws, &WebSync::started, this, [this] { emit websiteChanged(); });
    connect(&ws, &WebSync::finished, this, [this](bool ok, const QString &msg) {
        emit websiteChanged();
        emit websiteFinished(ok, msg);
    });
    // nach einem erfolgreichen Abgleich die Website mit dem zusammengeführten Stand aktualisieren
    connect(&DbSync::instance(), &DbSync::finished, this, [](bool ok, const QString &) {
        if (!ok) return;
        const WebSync::Config c = WebSync::config();
        if (c.autoPublish && WebSync::isReady(c) && !WebSync::instance().isBusy()) WebSync::instance().publish();
    });
}
