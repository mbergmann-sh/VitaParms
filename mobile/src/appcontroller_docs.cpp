// VitaParms Tablet – Krankenakte: Dokumente ansehen, fotografieren, aus Dateien übernehmen,
// bearbeiten, teilen (Teil von AppController)
// SPDX-License-Identifier: MIT
#include "appcontroller.h"

#include "akteexportcore.h"
#include "careinfo.h"
#include "contacts.h"
#include "database.h"
#include "dbtransfer.h"
#include "pdftools.h"
#include "session.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QStandardPaths>
#include <QTransform>
#include <QUrl>
#include <algorithm>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#endif

namespace {
QString workDir(const QString &sub)
{
    // Unter AppDataLocation: über den FileProvider der App freigegeben (Anzeige, Teilen, Kamera)
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + sub;
    QDir().mkpath(d);
    for (const QFileInfo &fi : QDir(d).entryInfoList(QDir::Files))       // Reste älterer Vorgänge
        if (fi.lastModified().secsTo(QDateTime::currentDateTime()) > 6 * 3600) QFile::remove(fi.absoluteFilePath());
    return d;
}

QImage readImage(const QByteArray &data)
{
    QBuffer b;
    b.setData(data);
    b.open(QIODevice::ReadOnly);
    QImageReader r(&b);
    r.setAutoTransform(true);              // Ausrichtung laut Kamera (EXIF)
    QImage img = r.read();
    // sehr große Fotos gleich verkleinern (Speicher); 200 dpi auf A4 genügen
    if (!img.isNull() && std::max(img.width(), img.height()) > 2400)
        img = img.scaled(2400, 2400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}
} // namespace

// ---------------------------------------------------------------- Liste

QVariantList AppController::documents(qint64 pid, const QString &query, const QString &category)
{
    QVariantList l;
    if (!loggedIn() || !pid) return l;
    const QLocale loc;
    const QStringList terms = query.simplified().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const MedicalDocument &d : Database::instance().documents(pid)) {
        if (!category.isEmpty() && d.category != category) continue;
        QString snippet;
        if (!terms.isEmpty()) {
            const QString hay = (d.title + QLatin1Char(' ') + d.category + QLatin1Char(' ') + d.issuer + QLatin1Char(' ')
                                 + d.notes + QLatin1Char(' ') + d.fullText).toLower();
            if (!std::all_of(terms.cbegin(), terms.cend(), [&](const QString &t) { return hay.contains(t); })) continue;
            snippet = PdfTools::snippet(d.fullText, terms.first(), 50);
            if (snippet.isEmpty()) snippet = PdfTools::snippet(d.notes, terms.first(), 50);
        }
        l << QVariantMap{
            { QStringLiteral("id"), d.id }, { QStringLiteral("title"), d.title }, { QStringLiteral("category"), d.category },
            { QStringLiteral("issuer"), d.issuer }, { QStringLiteral("notes"), d.notes },
            { QStringLiteral("date"), d.docDate.isValid() ? d.docDate.toString(Qt::ISODate) : QString() },
            { QStringLiteral("dateText"), d.docDate.isValid() ? loc.toString(d.docDate, QStringLiteral("dd.MM.yyyy")) : QString() },
            { QStringLiteral("year"), d.docDate.isValid() ? d.docDate.year() : 0 },
            { QStringLiteral("pages"), d.pages }, { QStringLiteral("size"), loc.formattedDataSize(d.size) },
            { QStringLiteral("hasData"), d.hasData }, { QStringLiteral("hasText"), d.hasText() },
            { QStringLiteral("snippet"), snippet },
        };
    }
    return l;
}

QStringList AppController::documentCategories(qint64 pid)
{
    QStringList l = PdfTools::defaultCategories();
    for (const QString &c : Database::instance().documentCategories(pid))
        if (!l.contains(c)) l << c;
    return l;
}

QStringList AppController::documentIssuers(qint64 pid)
{
    QStringList l;
    if (const auto p = Database::instance().patient(pid))
        for (const Contacts::Contact &c : Contacts::list(*p))
            if (c.kind != Contacts::Insurance && c.kind != Contacts::Emergency && !c.name.isEmpty() && !l.contains(c.name)) l << c.name;
    for (const MedicalDocument &d : Database::instance().documents(pid))
        if (!d.issuer.isEmpty() && !l.contains(d.issuer)) l << d.issuer;
    return l;
}

QVariantMap AppController::documentInfo(qint64 id)
{
    const auto d = Database::instance().document(id);
    if (!d) return {};
    const QLocale loc;
    return QVariantMap{
        { QStringLiteral("id"), d->id }, { QStringLiteral("patientId"), d->patientId },
        { QStringLiteral("title"), d->title }, { QStringLiteral("category"), d->category },
        { QStringLiteral("issuer"), d->issuer }, { QStringLiteral("notes"), d->notes },
        { QStringLiteral("date"), d->docDate.isValid() ? d->docDate.toString(Qt::ISODate) : QString() },
        { QStringLiteral("dateText"), d->docDate.isValid() ? loc.toString(d->docDate, QStringLiteral("dd.MM.yyyy")) : QString() },
        { QStringLiteral("pages"), std::max(1, d->pages) }, { QStringLiteral("size"), loc.formattedDataSize(d->size) },
        { QStringLiteral("fileName"), d->fileName }, { QStringLiteral("hasData"), d->hasData },
        { QStringLiteral("hasText"), d->hasText() },
        { QStringLiteral("added"), loc.toString(d->addedAt, QStringLiteral("dd.MM.yyyy HH:mm")) },
    };
}

bool AppController::pdfPreviewAvailable() const { return PdfTools::available(); }

// ---------------------------------------------------------------- Neues Dokument (Entwurf)

QString AppController::startCapture()
{
    const QString path = workDir(QStringLiteral("capture"))
                         + QStringLiteral("/foto-%1.jpg").arg(QDateTime::currentMSecsSinceEpoch());
#ifdef Q_OS_ANDROID
    const QJniObject ctx(QNativeInterface::QAndroidApplication::context());
    const jboolean ok = QJniObject::callStaticMethod<jboolean>("de/mbergmann/vitaparms/Capture", "start",
        "(Landroid/content/Context;Ljava/lang/String;)Z", ctx.object(), QJniObject::fromString(path).object<jstring>());
    if (!ok) { setError(tr("Die Kamera lässt sich nicht öffnen.")); return {}; }
    return path;
#else
    Q_UNUSED(path)
    setError(tr("Fotografieren ist nur auf dem Tablet möglich – bitte „Datei wählen“ nehmen."));
    return {};
#endif
}

QString AppController::captureState(const QString &path)
{
    QFile marker(path + QStringLiteral(".done"));
    if (!marker.exists()) return QStringLiteral("pending");
    QString r;
    if (marker.open(QIODevice::ReadOnly)) r = QString::fromUtf8(marker.readAll()).trimmed();
    marker.close();
    marker.remove();
    if (r == QLatin1String("ok") && QFileInfo(path).size() > 0) return QStringLiteral("ok");
    if (r == QLatin1String("cancel")) return QStringLiteral("cancel");
    // Ursache für die Meldung: keine Kamera-App, Berechtigung verweigert, kein Foto geliefert …
    if (r == QLatin1String("ok")) setError(tr("Die Kamera-App hat kein Foto geliefert."));
    else if (r.contains(QLatin1String("keine Kamera-App"))) setError(tr("Auf diesem Gerät ist keine Kamera-App verfügbar."));
    else if (r.contains(QLatin1String("Berechtigung"))) setError(tr("Ohne Kamera-Berechtigung kann VitaParms kein Foto aufnehmen. Bitte in den Android-Einstellungen unter Apps → VitaParms → Berechtigungen erlauben."));
    else setError(tr("Die Kamera ließ sich nicht starten (%1).").arg(r.mid(6)));
    return QStringLiteral("error");
}

bool AppController::draftAddImage(const QString &pathOrUrl)
{
    QByteArray data;
    QString err;
    if (!DbTransfer::readFile(pathOrUrl, &data, &err)) { setError(err); return false; }
    const QImage img = readImage(data);
    if (img.isNull()) { setError(tr("Das Bild lässt sich nicht lesen.")); return false; }
    if (!m_draftPdf.isEmpty()) { m_draftPdf.clear(); m_draftFileName.clear(); }
    m_draftPages << img;
    if (m_draftFileName.isEmpty())
        m_draftFileName = tr("Foto_%1.pdf").arg(QDate::currentDate().toString(Qt::ISODate));
    // aufgenommenes Foto wird nicht mehr gebraucht
    const QUrl u(pathOrUrl);
    const QString local = u.isLocalFile() ? u.toLocalFile() : pathOrUrl;
    if (local.startsWith(workDir(QStringLiteral("capture")))) QFile::remove(local);
    ++m_draftRevision;
    return true;
}

QVariantMap AppController::draftAddFiles(const QVariantList &urls)
{
    QVariantMap r;
    int added = 0;
    for (const QVariant &v : urls) {
        const QString url = v.toUrl().isValid() && !v.toUrl().isEmpty() ? v.toUrl().toString() : v.toString();
        QByteArray data;
        QString err;
        if (!DbTransfer::readFile(url, &data, &err)) { setError(err); r[QStringLiteral("ok")] = false; return r; }
        if (PdfTools::isPdf(data)) {
            if (!m_draftPages.isEmpty() || !m_draftPdf.isEmpty() || urls.size() > 1) {
                setError(tr("Bitte eine PDF-Datei einzeln übernehmen (Fotos lassen sich zu einem Dokument zusammenfassen)."));
                r[QStringLiteral("ok")] = false;
                return r;
            }
            m_draftPdf = data;
            QString name = QUrl(url).fileName();
            if (name.isEmpty() || !name.contains(QLatin1Char('.'))) name = QFileInfo(QUrl(url).path()).fileName();
            m_draftFileName = name.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive) ? name : tr("Dokument.pdf");
            ++added;
        } else {
            const QImage img = readImage(data);
            if (img.isNull()) {
                setError(tr("Nur PDF-Dateien und Bilder (JPEG, PNG) lassen sich übernehmen."));
                r[QStringLiteral("ok")] = false;
                return r;
            }
            m_draftPdf.clear();
            m_draftPages << img;
            if (m_draftFileName.isEmpty()) {
                const QString n = QFileInfo(QUrl(url).path()).completeBaseName();
                m_draftFileName = (n.isEmpty() ? tr("Scan") : n) + QStringLiteral(".pdf");
            }
            ++added;
        }
    }
    ++m_draftRevision;
    r[QStringLiteral("ok")] = added > 0;
    return r;
}

void AppController::draftRemovePage(int i)
{
    if (i >= 0 && i < m_draftPages.size()) m_draftPages.removeAt(i);
    ++m_draftRevision;
}

void AppController::draftRotatePage(int i)
{
    if (i >= 0 && i < m_draftPages.size()) m_draftPages[i] = m_draftPages[i].transformed(QTransform().rotate(90));
    ++m_draftRevision;
}

void AppController::draftClear()
{
    m_draftPages.clear();
    m_draftPdf.clear();
    m_draftFileName.clear();
    ++m_draftRevision;
}

QVariantMap AppController::draftInfo(qint64 pid)
{
    QVariantMap r;
    r[QStringLiteral("revision")] = m_draftRevision;
    r[QStringLiteral("kind")] = !m_draftPdf.isEmpty() ? QStringLiteral("pdf") : m_draftPages.isEmpty() ? QString() : QStringLiteral("images");
    r[QStringLiteral("fileName")] = m_draftFileName;
    PdfTools::Info info;
    if (!m_draftPdf.isEmpty()) info = PdfTools::analyze(m_draftPdf);
    r[QStringLiteral("pages")] = !m_draftPdf.isEmpty() ? std::max(1, info.pages) : int(m_draftPages.size());
    // Vorschläge (Titel, Datum, Art) wie am Computer
    const auto p = Database::instance().patient(pid);
    const QDate date = PdfTools::guessDate(m_draftFileName, info, p ? p->birthDate : QDate());
    r[QStringLiteral("date")] = (date.isValid() ? date : QDate::currentDate()).toString(Qt::ISODate);
    r[QStringLiteral("title")] = m_draftPdf.isEmpty() ? QString() : PdfTools::guessTitle(m_draftFileName, info);
    r[QStringLiteral("category")] = PdfTools::guessCategory(m_draftFileName, info.text);
    return r;
}

qint64 AppController::saveDraft(qint64 pid, const QVariantMap &meta)
{
    if (!loggedIn() || !pid) return 0;
    const QString title = meta.value(QStringLiteral("title")).toString().trimmed();
    if (title.isEmpty()) { setError(tr("Bitte einen Titel eingeben.")); return 0; }
    QByteArray pdf = m_draftPdf;
    if (pdf.isEmpty()) {
        if (m_draftPages.isEmpty()) { setError(tr("Bitte zuerst ein Foto aufnehmen oder eine Datei wählen.")); return 0; }
        pdf = PdfTools::imagesToPdf(m_draftPages, title);
        if (pdf.isEmpty()) { setError(tr("Das PDF ließ sich nicht erstellen.")); return 0; }
    }
    Database &db = Database::instance();
    const QByteArray sha = QCryptographicHash::hash(pdf, QCryptographicHash::Sha256);
    if (const auto dup = db.findDocumentByHash(pid, sha)) {
        setError(tr("Dieses Dokument ist bereits in der Akte: „%1“.").arg(dup->title));
        return 0;
    }
    const PdfTools::Info info = PdfTools::analyze(pdf);
    MedicalDocument d;
    d.patientId = pid;
    d.title = title;
    d.category = meta.value(QStringLiteral("category")).toString().trimmed();
    d.issuer = meta.value(QStringLiteral("issuer")).toString().trimmed();
    d.notes = meta.value(QStringLiteral("notes")).toString().trimmed();
    d.docDate = QDate::fromString(meta.value(QStringLiteral("date")).toString(), Qt::ISODate);
    d.fileName = m_draftFileName.isEmpty() ? title + QStringLiteral(".pdf") : m_draftFileName;
    d.sha256 = sha;
    d.pages = info.pages > 0 ? info.pages : std::max<int>(1, int(m_draftPages.size()));
    d.fullText = info.text;
    QString err;
    if (!db.addDocument(d, pdf, &err)) { setError(err); return 0; }
    draftClear();
    return d.id;
}

// ---------------------------------------------------------------- Bearbeiten, Löschen

bool AppController::saveDocument(const QVariantMap &meta)
{
    auto d = Database::instance().document(meta.value(QStringLiteral("id")).toLongLong());
    if (!d) { setError(tr("Das Dokument gibt es nicht mehr.")); return false; }
    const QString title = meta.value(QStringLiteral("title")).toString().trimmed();
    if (title.isEmpty()) { setError(tr("Bitte einen Titel eingeben.")); return false; }
    d->title = title;
    d->category = meta.value(QStringLiteral("category")).toString().trimmed();
    d->issuer = meta.value(QStringLiteral("issuer")).toString().trimmed();
    d->notes = meta.value(QStringLiteral("notes")).toString().trimmed();
    d->docDate = QDate::fromString(meta.value(QStringLiteral("date")).toString(), Qt::ISODate);
    QString err;
    if (!Database::instance().updateDocument(*d, &err)) { setError(err); return false; }
    return true;
}

bool AppController::deleteDocument(qint64 id)
{
    QString err;
    if (!Database::instance().deleteDocument(id, &err)) { setError(err); return false; }
    return true;
}

// ---------------------------------------------------------------- Ansehen, Teilen

static QString docFile(int index, const MedicalDocument &d, const QString &dir)
{
    const QString path = dir + QLatin1Char('/') + AkteExport::archiveFileName(index, d);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(Database::instance().documentData(d.id));
    return path;
}

bool AppController::openDocument(qint64 id)
{
    const auto d = Database::instance().document(id);
    if (!d) return false;
    if (!d->hasData) { setError(tr("Der Inhalt dieses Dokuments fehlt noch – er kommt mit dem nächsten Abgleich.")); return false; }
    const QString path = docFile(1, *d, workDir(QStringLiteral("view")));
    if (path.isEmpty() || !openFile(path)) { setError(tr("Keine App zum Anzeigen von PDF-Dateien gefunden.")); return false; }
    return true;
}

QVariantMap AppController::shareDocuments(qint64 pid, const QVariantList &ids, const QVariantMap &opts)
{
    QVariantMap r;
    r[QStringLiteral("ok")] = false;
    const auto p = Database::instance().patient(pid);
    if (!p) return r;
    QList<MedicalDocument> docs;
    for (const QVariant &v : ids)
        if (const auto d = Database::instance().document(v.toLongLong()); d && d->hasData) docs << *d;
    const bool zip = opts.value(QStringLiteral("zip")).toBool();
    if (docs.isEmpty() && !zip) { setError(tr("Bitte mindestens ein Dokument mit Inhalt wählen.")); return r; }
    const QString dir = workDir(QStringLiteral("share"));
    QStringList files;
    if (zip) {
        AkteExport::Options o;
        for (const MedicalDocument &d : docs) o.documentIds << d.id;
        o.includeSheet = opts.value(QStringLiteral("sheet"), true).toBool();
        o.includeMedPlan = opts.value(QStringLiteral("medPlan"), true).toBool();
        o.includeVitals = opts.value(QStringLiteral("vitals"), false).toBool();
        o.password = opts.value(QStringLiteral("password")).toString();
        if (!o.password.isEmpty() && o.password.size() < 8) { setError(tr("Das Passwort sollte mindestens 8 Zeichen lang sein.")); return r; }
        const QString path = dir + QLatin1Char('/') + AkteExport::defaultArchiveName(*p);
        QString err;
        if (!AkteExport::build(path, *p, o, &err)) { setError(err); return r; }
        files << path;
    } else {
        for (int i = 0; i < docs.size(); ++i)
            if (const QString f = docFile(i + 1, docs[i], dir); !f.isEmpty()) files << f;
    }
    r[QStringLiteral("files")] = files;
    if (!shareFiles(files, tr("Krankenakte – %1").arg(p->displayName()))) return r;
    r[QStringLiteral("ok")] = true;
    return r;
}

// ---------------------------------------------------------------- Seitenbilder (image://docpage/…)

QImage AppController::documentPageImage(const QString &id, int page, int width)
{
    if (id.startsWith(QLatin1String("draft"))) {
        if (!m_draftPdf.isEmpty()) return PdfTools::renderPage(m_draftPdf, page, width);
        if (page < 0 || page >= m_draftPages.size()) return {};
        const QImage &img = m_draftPages.at(page);
        return img.width() > width ? img.scaledToWidth(width, Qt::SmoothTransformation) : img;
    }
    const qint64 docId = id.toLongLong();
    if (!docId || !loggedIn()) return {};
    const QString key = QStringLiteral("%1/%2/%3").arg(docId).arg(page).arg(width);
    if (const auto it = m_pageCache.constFind(key); it != m_pageCache.constEnd()) return it.value();
    const QImage img = PdfTools::renderPage(Database::instance().documentData(docId), page, width);
    if (m_pageCache.size() > 120) m_pageCache.clear();    // Speicher begrenzen
    if (!img.isNull()) m_pageCache.insert(key, img);
    return img;
}

// ---------------------------------------------------------------- Vorsorgedokumente

// Dokumente der Krankenakte, die sich als Vorsorgedokument verknüpfen lassen (Kategorie „Vorsorge“ zuerst)
QVariantList AppController::advanceCandidates(qint64 pid)
{
    QVariantList first, rest;
    if (!loggedIn() || !pid) return first;
    const QLocale loc;
    const QString cat = QCoreApplication::translate("PatientDialog", "Vorsorge");
    for (const MedicalDocument &d : Database::instance().documents(pid)) {
        const QVariantMap m{ { QStringLiteral("id"), d.id }, { QStringLiteral("title"), d.title },
                             { QStringLiteral("category"), d.category },
                             { QStringLiteral("dateText"), d.docDate.isValid() ? loc.toString(d.docDate, QStringLiteral("dd.MM.yyyy")) : QString() } };
        (d.category.compare(cat, Qt::CaseInsensitive) == 0 ? first : rest) << m;
    }
    return first + rest;
}

bool AppController::linkAdvanceDocument(qint64 pid, int kind, qint64 docId)
{
    if (!loggedIn() || kind < 0 || kind >= CareInfo::AdvanceCount) return false;
    auto p = Database::instance().patient(pid);
    if (!p) return false;
    CareInfo::AdvanceDoc a = CareInfo::advanceDoc(*p, kind);
    a.docId = docId;
    if (docId) a.present = true;                 // ein hinterlegtes Dokument liegt vor
    CareInfo::setAdvanceDoc(*p, kind, a);
    QString err;
    if (!Database::instance().savePatient(*p, &err)) { setError(err); return false; }
    bump();
    return true;
}
