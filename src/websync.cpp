// SPDX-License-Identifier: MIT
#include "websync.h"
#include "contacts.h"
#include "careinfo.h"
#include "aes256.h"
#include "appsettings.h"
#include "bmi.h"
#include "database.h"
#include "formloader.h"
#include "journalmodel.h"
#include "medschedule.h"
#include "reportprinter.h"
#include "session.h"
#include "vitaldefs.h"
#include "stool.h"

#include <QCryptographicHash>
#include <QDir>
#include <QMutex>
#include <QThread>
#include <memory>
#include <QEventLoop>
#include <QFile>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMessageAuthenticationCode>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTimer>
#include <QRegularExpression>
#include <QUrl>
#include <cmath>
#include <algorithm>

// ====================================================================== Kryptografie

namespace WebCrypto {

static const QByteArray kMagic("VPW1");

QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    QRandomGenerator::system()->generate(reinterpret_cast<quint32 *>(b.data()),
                                         reinterpret_cast<quint32 *>(b.data()) + n / 4);
    for (int i = n - n % 4; i < n; ++i)
        b[i] = char(QRandomGenerator::system()->bounded(256));
    return b;
}

QByteArray deriveKeys(const QString &password, const QByteArray &salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(), salt,
                                              iterations, 64);
}

static QByteArray ctr(const QByteArray &data, const QByteArray &key, const QByteArray &iv)
{
    Aes256 aes(reinterpret_cast<const uint8_t *>(key.constData()));
    QByteArray out(data.size(), Qt::Uninitialized);
    uint8_t counter[16], stream[16];
    memcpy(counter, iv.constData(), 16);
    const auto *in = reinterpret_cast<const uint8_t *>(data.constData());
    auto *o = reinterpret_cast<uint8_t *>(out.data());
    for (qsizetype pos = 0; pos < data.size(); pos += 16) {
        aes.encryptBlock(counter, stream);
        const qsizetype n = std::min<qsizetype>(16, data.size() - pos);
        for (qsizetype i = 0; i < n; ++i) o[pos + i] = in[pos + i] ^ stream[i];
        for (int i = 15; i >= 8; --i)        // untere 64 Bit hochzählen (wie Web Crypto, length 64)
            if (++counter[i] != 0) break;
    }
    return out;
}

QByteArray encrypt(const QByteArray &plain, const QByteArray &keys)
{
    const QByteArray iv = randomBytes(16);
    QByteArray blob = kMagic + iv + ctr(plain, keys.left(32), iv);
    blob += QMessageAuthenticationCode::hash(blob, keys.mid(32, 32), QCryptographicHash::Sha256);
    return blob;
}

QByteArray decrypt(const QByteArray &blob, const QByteArray &keys, bool *ok)
{
    if (ok) *ok = false;
    if (blob.size() < 4 + 16 + 32 || !blob.startsWith(kMagic)) return {};
    const QByteArray body = blob.left(blob.size() - 32);
    const QByteArray mac = QMessageAuthenticationCode::hash(body, keys.mid(32, 32), QCryptographicHash::Sha256);
    if (mac != blob.right(32)) return {};
    if (ok) *ok = true;
    return ctr(body.mid(20), keys.left(32), body.mid(4, 16));
}

QByteArray checkValue(const QByteArray &keys)
{
    return QMessageAuthenticationCode::hash(QByteArrayLiteral("vitaparms-password-check"), keys.mid(32, 32),
                                            QCryptographicHash::Sha256);
}

} // namespace WebCrypto

// ====================================================================== Einstellungen

namespace {
const char *kUrl = "web/url", *kPassword = "web/password", *kKey = "web/uploadKey",
           *kEnabled = "web/enabled", *kAuto = "web/auto", *kLast = "web/lastUpload",
           *kResult = "web/lastResult", *kSalt = "web/salt";

QString s(const char *k) { return QString::fromLatin1(k); }

QString markerName(MarkerShape m)
{
    switch (m) {
    case MarkerShape::Circle: return QStringLiteral("circle");
    case MarkerShape::Square: return QStringLiteral("square");
    case MarkerShape::Triangle: return QStringLiteral("triangle");
    case MarkerShape::Diamond: return QStringLiteral("diamond");
    case MarkerShape::Cross: return QStringLiteral("cross");
    case MarkerShape::TriangleDown: return QStringLiteral("triangleDown");
    }
    return QStringLiteral("circle");
}

QString iso(const QDate &d) { return d.isValid() ? d.toString(Qt::ISODate) : QString(); }
QString iso(const QDateTime &t) { return t.isValid() ? t.toString(QStringLiteral("yyyy-MM-ddTHH:mm")) : QString(); }

QString safeFileName(QString s)
{
    s.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")), QStringLiteral("_"));
    return s.simplified();
}
}

WebSync::Config WebSync::config()
{
    Database &db = Database::instance();
    Config c;
    c.enabled = db.setting(s(kEnabled)) == QLatin1String("1");
    c.url = db.setting(s(kUrl));
    c.password = db.setting(s(kPassword));
    c.uploadKey = db.setting(s(kKey));
    c.autoPublish = db.setting(s(kAuto), QStringLiteral("1")) == QLatin1String("1");
    c.lastUpload = QDateTime::fromString(db.setting(s(kLast)), Qt::ISODate);
    c.lastResult = db.setting(s(kResult));
    return c;
}

void WebSync::saveConfig(const Config &c)
{
    Database &db = Database::instance();
    if (db.setting(s(kPassword)) != c.password)
        db.setSetting(s(kSalt), QString());          // neues Passwort → neues Salz
    db.setSetting(s(kEnabled), c.enabled ? QStringLiteral("1") : QStringLiteral("0"));
    db.setSetting(s(kUrl), normalizedUrl(c.url));
    db.setSetting(s(kPassword), c.password);
    db.setSetting(s(kKey), c.uploadKey);
    db.setSetting(s(kAuto), c.autoPublish ? QStringLiteral("1") : QStringLiteral("0"));
}

QString WebSync::generateUploadKey()
{
    return QString::fromLatin1(WebCrypto::randomBytes(24).toBase64(QByteArray::Base64UrlEncoding
                                                                   | QByteArray::OmitTrailingEquals));
}

QString WebSync::normalizedUrl(const QString &url)
{
    QString u = url.trimmed();
    if (u.isEmpty()) return u;
    if (!u.contains(QLatin1String("://"))) u.prepend(QLatin1String("https://"));
    if (!u.endsWith(QLatin1Char('/'))) u += QLatin1Char('/');
    return u;
}

bool WebSync::isReady(const Config &c, QString *why)
{
    QString w;
    const QUrl u(normalizedUrl(c.url));
    if (!c.enabled) w = tr("Die Website ist nicht eingeschaltet.");
    else if (!u.isValid() || u.host().isEmpty()) w = tr("Die Adresse der Website fehlt oder ist ungültig.");
    else if (u.scheme() != QLatin1String("https") && u.host() != QLatin1String("localhost")
             && u.host() != QLatin1String("127.0.0.1"))
        w = tr("Die Website muss über https:// erreichbar sein.");
    else if (c.password.size() < 10) w = tr("Das Master-Passwort muss mindestens 10 Zeichen lang sein.");
    else if (c.uploadKey.size() < 16) w = tr("Der Upload-Schlüssel fehlt.");
    if (why) *why = w;
    return w.isEmpty();
}

// ====================================================================== Datenpaket

// Momentaufnahme der Datenbank (im Hauptthread, schnell) – das Bauen des Pakets mit Berichten
// und Verschlüsselung läuft danach in einem Hintergrund-Thread.
struct WebSnapshot {
    struct Person {
        Patient p;
        QList<VitalRecord> recs;
        QStringList extraKeys;
        QList<Medication> all, current;
        QList<Appointment> appts;
        std::optional<VitalRecord> admission;
    };
    QString password;
    QByteArray salt;
    QJsonObject root;           // allgemeine Angaben (Messgrößen, Bereiche, Sprache …)
    QList<Person> persons;
};

static WebSnapshot takeSnapshot(const WebSync::Config &c)
{
    WebSnapshot snap;
    Database &db = Database::instance();
    const QLocale loc;
    snap.password = c.password;
    snap.salt = QByteArray::fromBase64(db.setting(s(kSalt)).toLatin1());
    if (snap.salt.size() != 16) {               // ein Salz je Passwort
        snap.salt = WebCrypto::randomBytes(16);
        db.setSetting(s(kSalt), QString::fromLatin1(snap.salt.toBase64()));
    }
    QJsonObject &root = snap.root;
    root.insert(QStringLiteral("app"), QStringLiteral("VitaParms"));
    root.insert(QStringLiteral("version"), QCoreApplication::applicationVersion());
    root.insert(QStringLiteral("generated"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("lang"), loc.language() == QLocale::German ? QStringLiteral("de") : QStringLiteral("en"));
    root.insert(QStringLiteral("disclaimer"), ReportPrinter::disclaimerShort());
    QJsonArray vitals;
    for (VitalKey k : VitalDefs::all()) {
        QJsonObject v;
        v.insert(QStringLiteral("key"), VitalDefs::key(k));
        v.insert(QStringLiteral("label"), VitalDefs::label(k));
        v.insert(QStringLiteral("short"), VitalDefs::shortLabel(k));
        v.insert(QStringLiteral("unit"), VitalDefs::unit(k));
        v.insert(QStringLiteral("decimals"), VitalDefs::decimals(k));
        v.insert(QStringLiteral("color"), VitalDefs::color(k).name());
        v.insert(QStringLiteral("marker"), markerName(VitalDefs::marker(k)));
        vitals.append(v);
    }
    root.insert(QStringLiteral("vitals"), vitals);
    QJsonArray panels;
    for (const auto &pd : VitalDefs::panels()) {
        QJsonArray keysArr;
        for (VitalKey k : pd.keys) keysArr.append(VitalDefs::key(k));
        panels.append(QJsonObject{ { QStringLiteral("id"), pd.id }, { QStringLiteral("title"), pd.title },
                                   { QStringLiteral("keys"), keysArr } });
    }
    root.insert(QStringLiteral("panels"), panels);
    {   // Stuhlgang: Texte der Bristol-Skala und des Hinweises (übersetzt), Darstellung im Browser
        QJsonArray names, descr, groupOf;
        for (int t = 0; t <= Stool::kTypes; ++t) {
            names.append(Stool::shortText(t));
            descr.append(Stool::choiceText(t));
            groupOf.append(int(Stool::group(t)));
        }
        QJsonArray groups;
        const QStringList gl = { QObject::tr("ohne Angabe"), QObject::tr("Typ 1–2 hart"), QObject::tr("Typ 3–4 normal"), QObject::tr("Typ 5–7 weich/flüssig") };
        for (int g = 0; g < 4; ++g)
            groups.append(QJsonObject{ { QStringLiteral("label"), gl[g] },
                                       { QStringLiteral("color"), Stool::groupColor(Stool::Group(g)).name() },
                                       { QStringLiteral("dark"), Stool::groupColor(Stool::Group(g), true).name() } });
        root.insert(QStringLiteral("stool"), QJsonObject{
            { QStringLiteral("names"), names }, { QStringLiteral("descriptions"), descr }, { QStringLiteral("groupOf"), groupOf },
            { QStringLiteral("groups"), groups }, { QStringLiteral("warnDays"), Stool::kWarnDays },
            { QStringLiteral("trackedDays"), Stool::kTrackedDays },
            { QStringLiteral("gapLegend"), QObject::tr("≥ %1 Tage ohne Stuhlgang").arg(Stool::kWarnDays) },
            { QStringLiteral("gapText"), QObject::tr("%1 Tage ohne") },
            { QStringLiteral("gapTip"), QObject::tr("%1 Tage ohne Stuhlgang (%2 – %3)") },
            { QStringLiteral("dayTip"), QObject::tr("%1× Stuhlgang") },
            { QStringLiteral("none"), QObject::tr("Kein Stuhlgang im Zeitraum eingetragen") },
            { QStringLiteral("warn"), QCoreApplication::translate("Stool", "Seit %1 Tagen kein Stuhlgang eingetragen (zuletzt am %2). Bitte auf Beschwerden achten, "
                                                                           "ausreichend trinken und bei anhaltender Verstopfung ärztlichen Rat einholen.") },
            { QStringLiteral("today"), QCoreApplication::translate("Stool", "letzter Stuhlgang heute") },
            { QStringLiteral("yesterday"), QCoreApplication::translate("Stool", "letzter Stuhlgang gestern") },
            { QStringLiteral("ago"), QCoreApplication::translate("Stool", "letzter Stuhlgang vor %1 Tagen") },
            { QStringLiteral("detailLabels"), QJsonArray{ QObject::tr("Menge"), QObject::tr("Farbe"), QObject::tr("Beimengungen / Geruch"),
                                                          QObject::tr("Begleitsymptome / Besonderheiten") } },
        });
    }
    QJsonArray cats;
    for (int i = 0; i < Medication::CategoryCount; ++i) cats.append(MedSchedule::categoryName(i));
    root.insert(QStringLiteral("medCategories"), cats);
    QJsonObject marks;
    for (const CareInfo::Mark &m : CareInfo::marks()) marks.insert(m.code, m.meaning);
    root.insert(QStringLiteral("disabilityMarks"), marks);

    for (const Patient &p0 : db.patients()) {
        WebSnapshot::Person sp;
        sp.p = db.patient(p0.id).value_or(p0);
        sp.recs = db.vitals(sp.p.id);
        sp.extraKeys = db.vitalExtraKeys(sp.p.id);
        sp.all = db.medications(sp.p.id, true);
        sp.current = db.medications(sp.p.id, false);
        sp.appts = db.appointments({}, {}, {}, sp.p.id, true);
        sp.admission = db.admissionVitals(sp.p.id);
        snap.persons << sp;
    }
    return snap;
}

namespace {
// Berichte einer Person werden nur neu erzeugt, wenn sich ihre Daten geändert haben
struct ReportCache {
    QByteArray hash;
    QList<QPair<QJsonObject, QByteArray>> reports;   // Beschreibung (ohne Dateiname) + Chiffrat
};
QMutex g_cacheMutex;
QHash<qint64, ReportCache> g_reportCache;
QByteArray g_keysFor, g_keys;
}

static WebSync::Bundle buildFromSnapshot(const WebSnapshot &snap)
{
    WebSync::Bundle b;
    QMutexLocker lock(&g_cacheMutex);
    const QByteArray keyId = QCryptographicHash::hash(snap.password.toUtf8() + snap.salt, QCryptographicHash::Sha256);
    if (g_keysFor != keyId) {                       // PBKDF2 nur nach Passwortwechsel
        g_keys = WebCrypto::deriveKeys(snap.password, snap.salt);
        g_keysFor = keyId;
        g_reportCache.clear();
    }
    const QByteArray keys = g_keys;
    b.id = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmss")) + QLatin1Char('-')
         + QString::fromLatin1(WebCrypto::randomBytes(4).toHex());
    QJsonObject root = snap.root;
    QTemporaryDir tmp;
    int fileNo = 0;

    QJsonArray persons;
    for (const WebSnapshot::Person &sp : snap.persons) {
        const Patient &p = sp.p;
        QJsonObject pj;
        pj.insert(QStringLiteral("id"), QString::number(p.id));
        pj.insert(QStringLiteral("name"), p.displayName());
        pj.insert(QStringLiteral("firstName"), p.firstName);
        pj.insert(QStringLiteral("lastName"), p.lastName);
        pj.insert(QStringLiteral("birthDate"), iso(p.birthDate));
        pj.insert(QStringLiteral("sex"), p.sex);
        pj.insert(QStringLiteral("address"), QStringList{ p.street, (p.zip + QLatin1Char(' ') + p.city).trimmed() }
                                                 .join(QStringLiteral(", ")).trimmed());
        pj.insert(QStringLiteral("phone"), p.phone);
        pj.insert(QStringLiteral("mobile"), p.mobile);
        pj.insert(QStringLiteral("diagnoses"), p.diagnoses);
        pj.insert(QStringLiteral("notes"), p.notes);
        if (p.amputations.any()) pj.insert(QStringLiteral("amputations"), Bmi::describe(p.amputations));
        if (p.careLevel > 0) {
            pj.insert(QStringLiteral("careLevel"), p.careLevel);
            if (p.careReview.isValid()) pj.insert(QStringLiteral("careReview"), iso(p.careReview));
        }
        if (p.disabilityDegree > 0) {
            pj.insert(QStringLiteral("disabilityDegree"), p.disabilityDegree);
            pj.insert(QStringLiteral("disabilityMarks"), QJsonArray::fromStringList(p.disabilityMarks));
            if (p.disabilityValidUntil.isValid()) pj.insert(QStringLiteral("disabilityValid"), iso(p.disabilityValidUntil));
        }
        QJsonObject extra;
        for (auto it = p.extra.begin(); it != p.extra.end(); ++it) {
            if (CareInfo::isInternalExtraKey(it.key())) continue;
            const QString v = it.value().toVariant().toString().trimmed();
            if (!v.isEmpty()) extra.insert(FormIO::fieldLabel(it.key()), v);
        }
        pj.insert(QStringLiteral("fields"), extra);
        {   // Vorsorgedokumente (ohne PDF – die Krankenakte bleibt auf den Geräten)
            QJsonArray adv;
            for (int k = 0; k < CareInfo::AdvanceCount; ++k)
                if (const QString l = CareInfo::advanceLine(p, k); !l.isEmpty()) adv.append(l);
            if (!adv.isEmpty()) pj.insert(QStringLiteral("advance"), adv);
        }
        {   // Kontakte (Krankenkasse, Ärzte, Kliniken, Notfallkontakt …)
            QJsonArray cj;
            for (const Contacts::Contact &c : Contacts::list(p)) {
                QJsonObject o{ { QStringLiteral("kind"), c.kindName() }, { QStringLiteral("name"), c.title() } };
                auto put = [&](const char *k, const QString &v) { if (!v.trimmed().isEmpty()) o.insert(QLatin1String(k), v.trimmed()); };
                put("person", c.person); put("insuranceNo", c.insuranceNo); put("address", c.address());
                put("phone", c.phone); put("mobile", c.mobile); put("email", c.email); put("website", c.website); put("note", c.note);
                cj.append(o);
            }
            if (!cj.isEmpty()) pj.insert(QStringLiteral("contacts"), cj);
        }
        if (p.extra.contains(QStringLiteral("Allergien")))
            pj.insert(QStringLiteral("allergies"), p.extra.value(QStringLiteral("Allergien")).toString());

        // Messwerte: gesamter Verlauf, älteste zuerst
        QList<VitalRecord> sorted = sp.recs;
        std::sort(sorted.begin(), sorted.end(), [](const VitalRecord &a, const VitalRecord &x) { return a.measuredAt < x.measuredAt; });
        QJsonArray readings;
        for (const VitalRecord &r : sorted) {
            QJsonObject rj;
            rj.insert(QStringLiteral("t"), iso(r.measuredAt));
            if (!r.context.isEmpty()) rj.insert(QStringLiteral("ctx"), r.context);
            if (r.isAdmission) rj.insert(QStringLiteral("adm"), true);
            QJsonObject v;
            for (VitalKey k : VitalDefs::all())
                if (const auto x = VitalDefs::value(r, k)) {
                    const double f = std::pow(10.0, VitalDefs::decimals(k));
                    v.insert(VitalDefs::key(k), std::round(*x * f) / f);
                }
            rj.insert(QStringLiteral("v"), v);
            if (r.stool && !r.stoolDetail.isEmpty())
                rj.insert(QStringLiteral("sd"), QJsonArray{ r.stoolDetail.amount, r.stoolDetail.color,
                                                            r.stoolDetail.admixture, r.stoolDetail.symptoms });
            if (!r.observations.trimmed().isEmpty()) rj.insert(QStringLiteral("obs"), r.observations.trimmed());
            QJsonObject ex;
            for (auto it = r.extra.begin(); it != r.extra.end(); ++it)
                ex.insert(FormIO::fieldLabel(it.key()), it.value());
            if (!ex.isEmpty()) rj.insert(QStringLiteral("x"), ex);
            readings.append(rj);
        }
        pj.insert(QStringLiteral("readings"), readings);
        b.readings += readings.size();

        QJsonArray mj;
        for (const Medication &m : sp.all)
            mj.append(QJsonObject{
                { QStringLiteral("name"), m.name }, { QStringLiteral("substance"), m.substance },
                { QStringLiteral("strength"), m.strength }, { QStringLiteral("form"), m.form },
                { QStringLiteral("dose"), m.dosageScheme() }, { QStringLiteral("unit"), m.unit },
                { QStringLiteral("category"), m.category }, { QStringLiteral("schedule"), MedSchedule::fullText(m) },
                { QStringLiteral("reason"), m.reason }, { QStringLiteral("notes"), m.notes },
                { QStringLiteral("start"), iso(m.startDate) }, { QStringLiteral("end"), iso(m.endDate) },
                { QStringLiteral("archived"), m.archived } });
        pj.insert(QStringLiteral("medications"), mj);

        QList<Appointment> appts = sp.appts;
        std::sort(appts.begin(), appts.end(), [](const Appointment &a, const Appointment &x) { return a.start < x.start; });
        QJsonArray aj;
        for (const Appointment &a : appts)
            aj.append(QJsonObject{
                { QStringLiteral("start"), iso(a.start) }, { QStringLiteral("end"), iso(a.end) },
                { QStringLiteral("allDay"), a.allDay }, { QStringLiteral("title"), a.title },
                { QStringLiteral("location"), a.location }, { QStringLiteral("priority"), a.priority },
                { QStringLiteral("notes"), a.notes }, { QStringLiteral("done"), a.done } });
        pj.insert(QStringLiteral("appointments"), aj);

        // Berichte, wie VitaParms sie druckt – nur bei geänderten Daten neu erzeugen
        const QByteArray hash = QCryptographicHash::hash(
            QJsonDocument(pj).toJson(QJsonDocument::Compact) + root.value(QStringLiteral("lang")).toString().toUtf8()
                + QByteArray::number(qHash(sp.extraKeys.join(QLatin1Char('|')))),
            QCryptographicHash::Sha256);
        ReportCache &cache = g_reportCache[p.id];
        if (cache.hash != hash) {
            cache.hash = hash;
            cache.reports.clear();
            auto add = [&](const QString &title, const QString &fileName, const QString &mime, const QString &path) {
                QFile f(path);
                if (!f.open(QIODevice::ReadOnly)) return;
                const QByteArray data = f.readAll();
                cache.reports.append({ QJsonObject{ { QStringLiteral("title"), title }, { QStringLiteral("name"), fileName },
                                                    { QStringLiteral("mime"), mime }, { QStringLiteral("size"), qint64(data.size()) } },
                                       WebCrypto::encrypt(data, keys) });
            };
            const QString base = safeFileName(p.displayName());
            const QString sheet = tmp.filePath(QStringLiteral("s%1.pdf").arg(p.id));
            if (ReportPrinter::patientSheetToPdf(sheet, p, sp.current, sp.admission))
                add(WebSync::tr("Stammblatt"), WebSync::tr("Stammblatt %1.pdf").arg(base), QStringLiteral("application/pdf"), sheet);
            const QString plan = tmp.filePath(QStringLiteral("m%1.pdf").arg(p.id));
            if (ReportPrinter::medicationPlanToPdf(plan, p, sp.current))
                add(WebSync::tr("Medikamentenplan"), WebSync::tr("Medikamentenplan %1.pdf").arg(base), QStringLiteral("application/pdf"), plan);
            const QString pharm = tmp.filePath(QStringLiteral("a%1.pdf").arg(p.id));
            if (ReportPrinter::pharmacyListToPdf(pharm, p, sp.current))
                add(WebSync::tr("Medikationsliste für die Apotheke"), WebSync::tr("Medikationsliste Apotheke %1.pdf").arg(base),
                    QStringLiteral("application/pdf"), pharm);
            if (!sp.recs.isEmpty()) {
                JournalModel model;
                model.setRecords(sp.recs, sp.extraKeys);
                const QString period = WebSync::tr("gesamter Verlauf");
                const QString jp = tmp.filePath(QStringLiteral("j%1.pdf").arg(p.id));
                if (ReportPrinter::journalToPdf(jp, p, model, period))
                    add(WebSync::tr("Verlauf (PDF)"), WebSync::tr("Verlauf %1.pdf").arg(base), QStringLiteral("application/pdf"), jp);
                const QString jx = tmp.filePath(QStringLiteral("j%1.xlsx").arg(p.id));
                if (ReportPrinter::journalToXlsx(jx, p, model, period))
                    add(WebSync::tr("Verlauf (Excel)"), WebSync::tr("Verlauf %1.xlsx").arg(base),
                        QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"), jx);
            }
        }
        QJsonArray reports;
        for (const auto &[meta, blob] : cache.reports) {
            const QString name = QStringLiteral("r%1.vpw").arg(++fileNo);
            b.files.insert(name, blob);
            QJsonObject m = meta;
            m.insert(QStringLiteral("file"), name);
            reports.append(m);
        }
        pj.insert(QStringLiteral("reports"), reports);
        persons.append(pj);
        ++b.persons;
    }
    root.insert(QStringLiteral("persons"), persons);

    b.plainJson = QJsonDocument(root).toJson(QJsonDocument::Compact);
    // qCompress = 4 Byte Länge + zlib-Strom; der Browser entpackt den zlib-Strom ("deflate")
    b.files.insert(QStringLiteral("data.vpw"), WebCrypto::encrypt(qCompress(b.plainJson, 9).mid(4), keys));
    const QJsonObject manifest{
        { QStringLiteral("format"), QStringLiteral("VPW1") },
        { QStringLiteral("bundle"), b.id },
        { QStringLiteral("kdf"), QJsonObject{ { QStringLiteral("name"), QStringLiteral("PBKDF2-SHA256") },
                                              { QStringLiteral("salt"), QString::fromLatin1(snap.salt.toBase64()) },
                                              { QStringLiteral("iterations"), WebCrypto::kIterations } } },
        { QStringLiteral("check"), QString::fromLatin1(WebCrypto::checkValue(keys).toBase64()) },
        { QStringLiteral("data"), QStringLiteral("data.vpw") },
        { QStringLiteral("lang"), root.value(QStringLiteral("lang")) },
    };
    b.manifest = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    return b;
}

WebSync::Bundle WebSync::buildBundle(const Config &c, QString *error)
{
    Q_UNUSED(error)
    return buildFromSnapshot(takeSnapshot(c));
}

// ====================================================================== Hochladen

WebSync &WebSync::instance()
{
    static WebSync s;
    return s;
}

WebSync::WebSync()
{
    m_nam = new QNetworkAccessManager(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(60 * 1000);       // eine Minute nach der letzten Änderung
    connect(m_debounce, &QTimer::timeout, this, [this] {
        const Config c = config();
        if (c.autoPublish && isReady(c) && !isBusy()) publish();
    });
}

void WebSync::watchDatabase()
{
    if (m_watching) return;
    m_watching = true;
    Database &db = Database::instance();
    connect(&db, &Database::vitalsChanged, this, &WebSync::markDirty);
    connect(&db, &Database::patientsChanged, this, &WebSync::markDirty);
    connect(&db, &Database::appointmentsChanged, this, &WebSync::markDirty);
}

void WebSync::markDirty()
{
    m_dirty = true;
    const Config c = config();
    if (c.autoPublish && isReady(c)) m_debounce->start();
}

QNetworkReply *WebSync::startUpload(const Config &c, const Bundle &b)
{
    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto addPart = [multi](const QString &name, const QString &fileName, const QByteArray &data) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"").arg(name, fileName));
        part.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        part.setBody(data);
        multi->append(part);
    };
    QHttpPart idPart;
    idPart.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"bundle\""));
    idPart.setBody(b.id.toLatin1());
    multi->append(idPart);
    addPart(QStringLiteral("manifest"), QStringLiteral("current.json"), b.manifest);
    for (auto it = b.files.begin(); it != b.files.end(); ++it)
        addPart(QStringLiteral("files[]"), it.key(), it.value());

    QNetworkRequest req(QUrl(normalizedUrl(c.url) + QStringLiteral("api/upload.php")));
    req.setRawHeader("X-VitaParms-Key", c.uploadKey.toUtf8());
    req.setRawHeader("Accept-Language", QLocale().bcp47Name().toLatin1());
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VitaParms/%1").arg(QCoreApplication::applicationVersion()));
    req.setTransferTimeout(120 * 1000);
    QNetworkReply *r = m_nam->post(req, multi);
    multi->setParent(r);
    return r;
}

QString WebSync::replyMessage(QNetworkReply *r, const QByteArray &body, bool *ok)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    *ok = r->error() == QNetworkReply::NoError && o.value(QStringLiteral("ok")).toBool();
    if (*ok) return o.value(QStringLiteral("message")).toString();
    if (!o.value(QStringLiteral("error")).toString().isEmpty())
        return o.value(QStringLiteral("error")).toString();
    if (status == 404)
        return tr("Auf der Website fehlt api/upload.php (HTTP 404). Ist der Website-Ordner vollständig hochgeladen?");
    if (r->error() != QNetworkReply::NoError) return r->errorString();
    return tr("Unerwartete Antwort der Website (HTTP %1).").arg(status);
}

void WebSync::handleUploadReply(QNetworkReply *r)
{
    const QByteArray body = r->readAll();
    bool ok = false;
    QString msg = replyMessage(r, body, &ok);
    if (ok) {
        m_dirty = false;
        msg = tr("Website aktualisiert (%1).").arg(QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat));
        Database::instance().setSetting(s(kLast), QDateTime::currentDateTime().toString(Qt::ISODate));
    }
    Database::instance().setSetting(s(kResult), msg);
    m_reply = nullptr;
    r->deleteLater();
    emit finished(ok, msg);
}

void WebSync::publish()
{
    if (isBusy()) return;
    const Config c = config();
    QString why;
    if (!isReady(c, &why)) { emit finished(false, why); return; }
    emit started();
    m_building = true;
    auto snap = std::make_shared<WebSnapshot>(takeSnapshot(c));
    auto result = std::make_shared<Bundle>();
    QThread *worker = QThread::create([snap, result] { *result = buildFromSnapshot(*snap); });
    connect(worker, &QThread::finished, this, [this, worker, result, c] {
        worker->deleteLater();
        m_building = false;
        m_reply = startUpload(c, *result);
        connect(m_reply, &QNetworkReply::finished, this, [this, r = m_reply] { handleUploadReply(r); });
    });
    worker->start(QThread::LowPriority);
}

bool WebSync::publishBlocking(int timeoutMs, QString *message)
{
    if (isBusy()) {
        QEventLoop wait;
        connect(this, &WebSync::finished, &wait, &QEventLoop::quit);
        QTimer::singleShot(timeoutMs, &wait, &QEventLoop::quit);
        wait.exec();
    }
    bool result = false;
    QEventLoop loop;
    auto conn = connect(this, &WebSync::finished, &loop, [&](bool ok, const QString &msg) {
        result = ok;
        if (message) *message = msg;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    QTimer::singleShot(0, this, &WebSync::publish);
    loop.exec();
    disconnect(conn);
    if (m_building) {               // Zeit abgelaufen, Paket noch im Bau
        if (message) *message = tr("Zeitüberschreitung beim Hochladen.");
        return false;
    }
    if (m_reply) {                  // Zeit abgelaufen
        m_reply->abort();
        if (message) *message = tr("Zeitüberschreitung beim Hochladen.");
        return false;
    }
    return result;
}

void WebSync::testConnection()
{
    const Config c = config();
    QNetworkRequest req(QUrl(normalizedUrl(c.url) + QStringLiteral("api/upload.php?status=1")));
    req.setRawHeader("X-VitaParms-Key", c.uploadKey.toUtf8());
    req.setRawHeader("Accept-Language", QLocale().bcp47Name().toLatin1());
    req.setTransferTimeout(20 * 1000);
    QNetworkReply *r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        const QByteArray body = r->readAll();
        bool ok = false;
        QString msg = replyMessage(r, body, &ok);
        if (ok) {
            const QJsonObject o = QJsonDocument::fromJson(body).object();
            msg = tr("Verbindung in Ordnung – PHP %1, %2.")
                      .arg(o.value(QStringLiteral("php")).toString(),
                           o.value(QStringLiteral("claimed")).toBool()
                               ? (o.value(QStringLiteral("keyValid")).toBool() ? tr("Upload-Schlüssel gültig")
                                                                              : tr("Upload-Schlüssel passt NICHT"))
                               : tr("die Website wird beim ersten Hochladen mit diesem Schlüssel eingerichtet"));
            ok = !o.value(QStringLiteral("claimed")).toBool() || o.value(QStringLiteral("keyValid")).toBool();
        }
        r->deleteLater();
        emit connectionTested(ok, msg);
    });
}
