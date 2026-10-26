// SPDX-License-Identifier: MIT
#include "leaflet.h"
#include "database.h"
#include "pdftools.h"

#include <QApplication>
#include <QBuffer>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

#ifdef VP_HAVE_QTPDF
#include <QPdfDocument>
#include <QPdfView>
#endif

namespace {
QString tr(const char *s) { return QCoreApplication::translate("Leaflet", s); }
constexpr qint64 kMaxBytes = 40 * 1024 * 1024;
}

namespace Leaflet {

QList<Source> sources()
{
    QList<Source> l;
    l << Source{ tr("Apotheken Umschau (PZN)"),
                 QStringLiteral("https://www.apotheken-umschau.de/medikamente/beipackzettel/{slug}-{pzn7}.html"),
                 Kind::Browser, true,
                 tr("Beipackzettel-Datenbank (ABDATA) mit Link zur Original-Gebrauchsinformation – benötigt die PZN") }
      << Source{ tr("Gelbe Liste"), QStringLiteral("https://www.gelbe-liste.de/suche?term={name}"), Kind::Browser, false,
                 tr("Suche nach Präparat oder Wirkstoff") }
      << Source{ tr("Websuche"),
                 QStringLiteral("https://duckduckgo.com/?q={name}+{strength}+Gebrauchsinformation+pdf"), Kind::Browser, false,
                 tr("Websuche nach der Original-Gebrauchsinformation (PDF des Herstellers)") }
      << Source{ tr("EMA-Produktinformation"), {}, Kind::EmaPdf, false,
                 tr("PDF der Europäischen Arzneimittel-Agentur mit Packungsbeilage – nur für zentral "
                    "zugelassene Arzneimittel (meist Originalpräparate), Suche nach Handelsname") }
      << Source{ tr("openFDA (USA, englisch)"), {}, Kind::OpenFda, false,
                 tr("US-Arzneimittelinformation der FDA (englisch, gemeinfrei) – nicht die deutsche "
                    "Packungsbeilage, Suche nach Wirkstoff") };
    l << customSources();
    return l;
}

QString customSourcesText()
{
    return QSettings().value(QStringLiteral("leaflet/customSources")).toString();
}

void setCustomSourcesText(const QString &text)
{
    QSettings().setValue(QStringLiteral("leaflet/customSources"), text.trimmed());
}

QList<Source> customSources()
{
    QList<Source> l;
    for (const QString &line : customSourcesText().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const int bar = line.indexOf(QLatin1Char('|'));
        if (bar <= 0) continue;
        const QString name = line.left(bar).trimmed();
        const QString url = line.mid(bar + 1).trimmed();
        if (name.isEmpty() || !url.startsWith(QLatin1String("http"))) continue;
        l << Source{ name, url, Kind::Browser, url.contains(QLatin1String("{pzn")), url };
    }
    return l;
}

bool onlineAllowed()
{
    return QSettings().value(QStringLiteral("leaflet/online"), true).toBool();
}

void setOnlineAllowed(bool on)
{
    QSettings().setValue(QStringLiteral("leaflet/online"), on);
}

QString normalizePzn(const QString &input)
{
    QString d;
    for (const QChar c : input)
        if (c.isDigit()) d += c;
        else if (!c.isSpace() && c != QLatin1Char('-') && c.toUpper() != QLatin1Char('P') && c.toUpper() != QLatin1Char('Z')
                 && c.toUpper() != QLatin1Char('N') && c != QLatin1Char(':'))
            return {};
    if (d.size() == 7) d.prepend(QLatin1Char('0'));   // alte PZN-7: Prüfziffer bleibt gültig
    return d.size() == 8 ? d : QString();
}

bool isValidPzn(const QString &p)
{
    if (p.size() != 8) return false;
    int sum = 0;
    for (int i = 0; i < 7; ++i) {
        if (!p[i].isDigit()) return false;
        sum += p[i].digitValue() * (i + 1);
    }
    const int check = sum % 11;
    return check != 10 && p[7].isDigit() && p[7].digitValue() == check;
}

QString slug(const QString &text)
{
    QString s = text.toLower();
    s.replace(QStringLiteral("ä"), QStringLiteral("ae")).replace(QStringLiteral("ö"), QStringLiteral("oe"))
     .replace(QStringLiteral("ü"), QStringLiteral("ue")).replace(QStringLiteral("ß"), QStringLiteral("ss"));
    s = s.normalized(QString::NormalizationForm_KD);
    QString out;
    for (const QChar c : s) {
        if (c.isLetterOrNumber() && c.unicode() < 128) out += c;
        else if (c.category() == QChar::Mark_NonSpacing) continue;
        else if (!out.endsWith(QLatin1Char('-'))) out += QLatin1Char('-');
    }
    while (out.startsWith(QLatin1Char('-'))) out.remove(0, 1);
    while (out.endsWith(QLatin1Char('-'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("x") : out;
}

QUrl expand(const QString &tpl, const Medication &m)
{
    const QString pzn = normalizePzn(m.pzn);
    QString pzn7 = pzn;
    while (pzn7.startsWith(QLatin1Char('0'))) pzn7.remove(0, 1);
    auto enc = [](const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(s.trimmed())); };
    QString u = tpl;
    u.replace(QStringLiteral("{pzn}"), pzn);
    u.replace(QStringLiteral("{pzn7}"), pzn7);
    u.replace(QStringLiteral("{name}"), enc(m.name));
    u.replace(QStringLiteral("{substance}"), enc(m.substance.isEmpty() ? m.name : m.substance));
    u.replace(QStringLiteral("{strength}"), enc(m.strength));
    u.replace(QStringLiteral("{slug}"), slug(m.name + QLatin1Char(' ') + m.strength));
    return QUrl(u, QUrl::TolerantMode);
}

static QString firstWord(const QString &s)
{
    static const QRegularExpression rx(QStringLiteral("[\\p{L}][\\p{L}\\-]*"));
    const auto m = rx.match(s);
    return m.hasMatch() ? m.captured(0) : s.trimmed();
}

QUrl emaUrl(const Medication &m, const QString &lang)
{
    const QString brand = slug(firstWord(m.name));
    return QUrl(QStringLiteral("https://www.ema.europa.eu/%1/documents/product-information/%2-epar-product-information_%1.pdf")
                    .arg(lang, brand));
}

QUrl openFdaUrl(const Medication &m)
{
    QString term = m.substance.isEmpty() ? firstWord(m.name) : m.substance;
    term = term.toLower().trimmed();
    // einige deutsche Wirkstoffnamen auf die US-Schreibweise bringen
    static const QList<QPair<QString, QString>> map = {
        { QStringLiteral("acetylsalicylsäure"), QStringLiteral("aspirin") },
        { QStringLiteral("insulin glargin"), QStringLiteral("insulin glargine") },
        { QStringLiteral("paracetamol"), QStringLiteral("acetaminophen") },
        { QStringLiteral("metamizol"), QStringLiteral("metamizole") },
        { QStringLiteral("levothyroxin"), QStringLiteral("levothyroxine") },
        { QStringLiteral("amlodipin"), QStringLiteral("amlodipine") },
        { QStringLiteral("simvastatin"), QStringLiteral("simvastatin") },
        { QStringLiteral("pantoprazol"), QStringLiteral("pantoprazole") },
        { QStringLiteral("omeprazol"), QStringLiteral("omeprazole") },
        { QStringLiteral("torasemid"), QStringLiteral("torsemide") },
        { QStringLiteral("furosemid"), QStringLiteral("furosemide") },
        { QStringLiteral("hydrochlorothiazid"), QStringLiteral("hydrochlorothiazide") },
    };
    for (const auto &[de, en] : map)
        if (term == de) { term = en; break; }
    const QString q = QString::fromLatin1(QUrl::toPercentEncoding(term));
    return QUrl(QStringLiteral("https://api.fda.gov/drug/label.json?search=openfda.generic_name:%22%1%22+OR+"
                               "openfda.brand_name:%22%1%22&limit=1").arg(q), QUrl::TolerantMode);
}

QString openFdaHtml(const QByteArray &json, QString *title)
{
    const QJsonArray results = QJsonDocument::fromJson(json).object().value(QStringLiteral("results")).toArray();
    if (results.isEmpty()) return {};
    const QJsonObject r = results.first().toObject();
    const QJsonObject of = r.value(QStringLiteral("openfda")).toObject();
    auto first = [&of](const char *k) { return of.value(QLatin1String(k)).toArray().first().toString(); };
    QString t = first("brand_name");
    const QString gen = first("generic_name");
    if (!gen.isEmpty() && gen.compare(t, Qt::CaseInsensitive) != 0) t += QStringLiteral(" (") + gen + QLatin1Char(')');
    if (t.isEmpty()) t = tr("US-Arzneimittelinformation");
    if (title) *title = t;

    struct Sec { const char *key; const char *head; };
    static const Sec secs[] = {
        { "spl_patient_package_insert", QT_TRANSLATE_NOOP("Leaflet", "Patienteninformation (Patient Package Insert)") },
        { "information_for_patients",   QT_TRANSLATE_NOOP("Leaflet", "Informationen für Patienten") },
        { "boxed_warning",              QT_TRANSLATE_NOOP("Leaflet", "Besonderer Warnhinweis (Boxed Warning)") },
        { "indications_and_usage",      QT_TRANSLATE_NOOP("Leaflet", "Anwendungsgebiete") },
        { "dosage_and_administration",  QT_TRANSLATE_NOOP("Leaflet", "Dosierung und Anwendung") },
        { "contraindications",          QT_TRANSLATE_NOOP("Leaflet", "Gegenanzeigen") },
        { "warnings_and_cautions",      QT_TRANSLATE_NOOP("Leaflet", "Warnhinweise und Vorsichtsmaßnahmen") },
        { "warnings",                   QT_TRANSLATE_NOOP("Leaflet", "Warnhinweise") },
        { "adverse_reactions",          QT_TRANSLATE_NOOP("Leaflet", "Nebenwirkungen") },
        { "drug_interactions",          QT_TRANSLATE_NOOP("Leaflet", "Wechselwirkungen") },
        { "overdosage",                 QT_TRANSLATE_NOOP("Leaflet", "Überdosierung") },
    };
    QString h = QStringLiteral("<h2>%1</h2>").arg(t.toHtmlEscaped());
    h += QStringLiteral("<p style=\"background:#fff4d6; color:#5a4500; padding:6px;\">%1</p>")
             .arg(tr("⚠ US-amerikanische Arzneimittelinformation der FDA (englisch). Sie ist <b>nicht</b> die "
                     "deutsche Packungsbeilage; Zulassung, Dosierungen und Hinweise können abweichen. "
                     "Maßgeblich ist der Beipackzettel in der Packung."));
    const QString manu = first("manufacturer_name");
    if (!manu.isEmpty()) h += QStringLiteral("<p><i>%1</i></p>").arg(manu.toHtmlEscaped());
    int n = 0;
    for (const Sec &s : secs) {
        const QJsonArray a = r.value(QLatin1String(s.key)).toArray();
        if (a.isEmpty()) continue;
        QStringList parts;
        for (const QJsonValue &v : a) parts << v.toString().toHtmlEscaped();
        h += QStringLiteral("<h3>%1</h3><p>%2</p>")
                 .arg(QCoreApplication::translate("Leaflet", s.head).toHtmlEscaped(),
                      parts.join(QStringLiteral("</p><p>")).replace(QLatin1Char('\n'), QStringLiteral("<br>")));
        ++n;
    }
    if (n == 0) return {};
    const QString eff = r.value(QStringLiteral("effective_time")).toString();
    h += QStringLiteral("<hr><p><small>%1</small></p>")
             .arg(tr("Quelle: openFDA (U.S. Food and Drug Administration), Stand %1, Set-ID %2. "
                     "Die Daten sind gemeinfrei; openFDA übernimmt keine Gewähr.")
                      .arg(eff.size() == 8 ? QDate::fromString(eff, QStringLiteral("yyyyMMdd")).toString(QStringLiteral("dd.MM.yyyy")) : eff,
                           r.value(QStringLiteral("set_id")).toString().toHtmlEscaped()));
    return h;
}

QByteArray htmlToPdf(const QString &html, const QString &title)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QPdfWriter w(&buf);
    w.setTitle(title);
    w.setCreator(QStringLiteral("VitaParms"));
    w.setPageSize(QPageSize(QPageSize::A4));
    w.setPageMargins(QMarginsF(18, 15, 18, 15), QPageLayout::Millimeter);
    w.setResolution(150);
    QTextDocument doc;
    QFont f = doc.defaultFont();
    f.setPointSizeF(9.5);
    doc.setDefaultFont(f);
    doc.setHtml(html);
    doc.setPageSize(QSizeF(w.width(), w.height()));
    doc.print(&w);
    buf.close();
    return out;
}

} // namespace Leaflet

// ============================================================ Dialog

LeafletDialog::LeafletDialog(const Medication &m, qint64 patientId, QWidget *parent)
    : QDialog(parent), m_med(m), m_patientId(patientId)
{
    setWindowTitle(tr("Beipackzettel – %1").arg(m.name));
    m_net = new QNetworkAccessManager(this);
    auto *lay = new QVBoxLayout(this);

    // Kopf: Präparat und PZN
    auto *head = new QHBoxLayout;
    QStringList d;
    if (!m.strength.isEmpty()) d << m.strength.toHtmlEscaped();
    if (!m.form.isEmpty()) d << m.form.toHtmlEscaped();
    if (!m.substance.isEmpty()) d << tr("Wirkstoff: %1").arg(m.substance.toHtmlEscaped());
    auto *title = new QLabel(QStringLiteral("<span style=\"font-size:14pt;\"><b>%1</b></span> &nbsp;%2")
                                 .arg(m.name.toHtmlEscaped(), d.join(QStringLiteral(" · "))), this);
    head->addWidget(title, 1);
    auto *pznLabel = new QLabel(tr("&PZN:"), this);
    head->addWidget(pznLabel);
    m_pzn = new QLineEdit(m.pzn, this);
    pznLabel->setBuddy(m_pzn);
    m_pzn->setMaximumWidth(120);
    m_pzn->setPlaceholderText(tr("8 Ziffern"));
    m_pzn->setToolTip(tr("Pharmazentralnummer von der Packung – damit wird der Beipackzettel eindeutig gefunden"));
    head->addWidget(m_pzn);
    m_pznState = new QLabel(this);
    head->addWidget(m_pznState);
    lay->addLayout(head);
    connect(m_pzn, &QLineEdit::editingFinished, this, &LeafletDialog::updatePzn);

    // Quellen
    auto *src = new QHBoxLayout;
    src->addWidget(new QLabel(tr("Nachschlagen:"), this));
    for (const Leaflet::Source &s : Leaflet::sources()) {
        auto *b = new QPushButton(s.kind == Leaflet::Kind::Browser ? s.name + QStringLiteral(" ↗") : s.name, this);
        b->setToolTip(s.hint + (s.kind == Leaflet::Kind::Browser ? tr("\n(öffnet im Webbrowser)") : tr("\n(Anzeige hier im Fenster)")));
        b->setAutoDefault(false);
        b->setProperty("needsPzn", s.needsPzn);
        connect(b, &QPushButton::clicked, this, [this, s] { openSource(s); });
        src->addWidget(b);
        m_onlineButtons << b;
    }
    src->addStretch();
    lay->addLayout(src);
    auto *own = new QHBoxLayout;
    own->addWidget(new QLabel(tr("Original-PDF:"), this));
    auto *urlBtn = new QPushButton(tr("&Adresse laden…"), this);
    urlBtn->setToolTip(tr("Link zur Gebrauchsinformation (PDF) einfügen, z. B. vom Hersteller oder von der "
                          "Beipackzettel-Seite der Apotheken Umschau"));
    auto *fileBtn = new QPushButton(tr("Aus &Datei…"), this);
    for (auto *b : { urlBtn, fileBtn }) { b->setAutoDefault(false); own->addWidget(b); }
    m_onlineButtons << urlBtn;
    connect(urlBtn, &QPushButton::clicked, this, &LeafletDialog::askUrl);
    connect(fileBtn, &QPushButton::clicked, this, &LeafletDialog::loadFile);
    own->addStretch();
    lay->addLayout(own);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_status->setOpenExternalLinks(true);
    lay->addWidget(m_status);

    m_stack = new QStackedWidget(this);
    m_text = new QTextBrowser(m_stack);
    m_text->setOpenExternalLinks(true);
    m_stack->addWidget(m_text);
#ifdef VP_HAVE_QTPDF
    m_view = new QPdfView(m_stack);
    m_view->setPageMode(QPdfView::PageMode::MultiPage);
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    m_stack->addWidget(m_view);
#endif
    lay->addWidget(m_stack, 1);

    auto *bottom = new QHBoxLayout;
    m_save = new QPushButton(tr("In der &Krankenakte speichern"), this);
    m_save->setToolTip(tr("Verschlüsselt speichern und mit dem Medikament verknüpfen – dann auch ohne Internet verfügbar"));
    m_print = new QPushButton(tr("&Drucken…"), this);
    m_external = new QPushButton(tr("&Extern öffnen"), this);
    auto *close = new QPushButton(tr("&Schließen"), this);
    for (auto *b : { m_save, m_print, m_external }) { b->setAutoDefault(false); bottom->addWidget(b); }
    bottom->addStretch();
    bottom->addWidget(close);
    lay->addLayout(bottom);
    connect(m_save, &QPushButton::clicked, this, &LeafletDialog::saveToRecord);
    connect(m_print, &QPushButton::clicked, this, &LeafletDialog::printPdf);
    connect(m_external, &QPushButton::clicked, this, &LeafletDialog::openExternally);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    const bool online = Leaflet::onlineAllowed();
    m_text->setHtml(tr("<h3>Beipackzettel abrufen</h3>"
                       "<p>Maßgeblich ist immer der <b>Beipackzettel in der Packung</b>. Online-Angaben können "
                       "abweichen oder zu einem anderen Hersteller gehören – die <b>PZN</b> von der Packung "
                       "sorgt für den richtigen Treffer.</p>"
                       "<ul><li><b>Apotheken Umschau</b> zeigt den Beipackzettel zur PZN und verlinkt die "
                       "Original-Gebrauchsinformation als PDF. Diesen Link kopieren und hier mit "
                       "<i>Adresse laden…</i> abrufen – dann lässt sich das PDF in der Krankenakte speichern.</li>"
                       "<li><b>EMA</b> und <b>openFDA</b> werden direkt hier angezeigt.</li></ul>"
                       "<p><small>Beim Nachschlagen werden nur Name, Stärke, Wirkstoff bzw. PZN des Medikaments "
                       "an den gewählten Anbieter übertragen – keine Angaben zur Person.</small></p>")
                    + (online ? QString()
                              : tr("<p><b>Das Nachschlagen im Internet ist in den Einstellungen ausgeschaltet.</b></p>")));
    updatePzn();
    showStored();
    updateButtons();
    resize(980, 760);
}

LeafletDialog::~LeafletDialog()
{
    if (m_reply) m_reply->abort();
}

bool LeafletDialog::busy() const
{
    return m_reply && m_reply->isRunning();
}

QString LeafletDialog::statusText() const
{
    return m_status->text();
}

void LeafletDialog::setStatus(const QString &html)
{
    m_status->setText(html);
}

void LeafletDialog::updateButtons()
{
    const bool online = Leaflet::onlineAllowed();
    const bool pznOk = Leaflet::isValidPzn(Leaflet::normalizePzn(m_pzn->text()));
    for (QPushButton *b : m_onlineButtons) {
        b->setEnabled(online && !busy() && (!b->property("needsPzn").toBool() || pznOk));
        if (b->property("needsPzn").toBool() && !pznOk)
            b->setToolTip(tr("Bitte zuerst die PZN von der Packung eintragen."));
    }
    m_save->setEnabled(!m_pdf.isEmpty() && !m_stored && m_patientId != 0);
    if (m_patientId == 0)
        m_save->setToolTip(tr("Erst möglich, wenn die Person gespeichert ist."));
    m_print->setEnabled(!m_pdf.isEmpty());
    m_external->setEnabled(!m_pdf.isEmpty());
}

void LeafletDialog::updatePzn()
{
    const QString raw = m_pzn->text().trimmed();
    const QString p = Leaflet::normalizePzn(raw);
    if (raw.isEmpty()) {
        m_pznState->setText(QString());
    } else if (p.isEmpty() || !Leaflet::isValidPzn(p)) {
        m_pznState->setText(tr("<span style=\"color:#D52B1E;\">⚠ Prüfziffer falsch</span>"));
    } else {
        m_pznState->setText(QStringLiteral("<span style=\"color:#2e9d4f;\">✔</span>"));
        if (p != raw) m_pzn->setText(p);
    }
    const QString val = (p.isEmpty() || !Leaflet::isValidPzn(p)) ? raw : p;
    if (val != m_med.pzn) {
        m_med.pzn = val;
        m_changed = true;
    }
    updateButtons();
}

void LeafletDialog::showStored()
{
    const qint64 docId = m_med.extra.value(QLatin1String(Leaflet::kLeafletDocKey)).toVariant().toLongLong();
    if (!docId) return;
    const auto doc = Database::instance().document(docId);
    if (!doc) {   // Dokument wurde in der Krankenakte gelöscht
        m_med.extra.remove(QLatin1String(Leaflet::kLeafletDocKey));
        m_changed = true;
        return;
    }
    if (showPdf(Database::instance().documentData(docId), m_med.extra.value(QLatin1String(Leaflet::kLeafletUrlKey)).toString())) {
        m_stored = true;
        setStatus(tr("Gespeicherter Beipackzettel aus der Krankenakte (übernommen am %1)%2")
                      .arg(QLocale().toString(doc->addedAt.isValid() ? doc->addedAt.date() : doc->docDate, QStringLiteral("dd.MM.yyyy")),
                           m_origin.startsWith(QLatin1String("http"))
                               ? tr(" – Quelle: <a href=\"%1\">%2</a>").arg(m_origin.toHtmlEscaped(), QUrl(m_origin).host().toHtmlEscaped())
                               : QString()));
        updateButtons();
    }
}

void LeafletDialog::openSource(const Leaflet::Source &s)
{
    if (!Leaflet::onlineAllowed()) return;
    updatePzn();
    switch (s.kind) {
    case Leaflet::Kind::Browser: {
        const QUrl url = Leaflet::expand(s.urlTemplate, m_med);
        setStatus(tr("Im Webbrowser geöffnet: <a href=\"%1\">%2</a>. Dort gefundene PDF-Links lassen sich mit "
                     "<i>Adresse laden…</i> hier anzeigen und speichern.")
                      .arg(url.toString().toHtmlEscaped(), url.host().toHtmlEscaped()));
        QDesktopServices::openUrl(url);
        break;
    }
    case Leaflet::Kind::EmaPdf:
        loadUrl(Leaflet::emaUrl(m_med, QLocale().language() == QLocale::German ? QStringLiteral("de") : QStringLiteral("en")),
                Leaflet::Kind::EmaPdf);
        break;
    case Leaflet::Kind::OpenFda:
        loadUrl(Leaflet::openFdaUrl(m_med), Leaflet::Kind::OpenFda);
        break;
    }
}

void LeafletDialog::loadUrl(const QUrl &url, Leaflet::Kind kind)
{
    if (!url.isValid() || busy()) return;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("VitaParms/%1 (Qt)").arg(QCoreApplication::applicationVersion()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    m_reply = m_net->get(req);
    setStatus(tr("Lade %1 …").arg(url.host().toHtmlEscaped()));
    QApplication::setOverrideCursor(Qt::BusyCursor);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64) {
        if (got > kMaxBytes && m_reply) m_reply->abort();
    });
    QNetworkReply *r = m_reply;
    connect(r, &QNetworkReply::finished, this, [this, r, kind] { onReply(r, kind); });
    updateButtons();
}

void LeafletDialog::onReply(QNetworkReply *r, Leaflet::Kind kind)
{
    QApplication::restoreOverrideCursor();
    r->deleteLater();
    m_reply = nullptr;
    const QUrl url = r->url();
    const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = r->error() == QNetworkReply::NoError ? r->readAll() : QByteArray();
    updateButtons();

    if (kind == Leaflet::Kind::OpenFda) {
        QString title;
        const QString html = data.isEmpty() ? QString() : Leaflet::openFdaHtml(data, &title);
        if (html.isEmpty()) {
            setStatus(http == 404 || r->error() == QNetworkReply::ContentNotFoundError
                          ? tr("openFDA kennt „%1“ nicht. Die Suche nutzt den (englischen) Wirkstoffnamen.")
                                .arg((m_med.substance.isEmpty() ? m_med.name : m_med.substance).toHtmlEscaped())
                          : tr("Abruf fehlgeschlagen: %1").arg(r->errorString().toHtmlEscaped()));
            return;
        }
        showHtml(html, url.toString(), title);
        return;
    }

    if (r->error() != QNetworkReply::NoError || data.isEmpty()) {
        if (kind == Leaflet::Kind::EmaPdf && url.host().contains(QLatin1String("ema.europa.eu")) && (http == 404 || http == 0 || http >= 400))
            setStatus(tr("Bei der EMA gibt es zu „%1“ keine Produktinformation. Die EMA veröffentlicht nur zentral "
                         "zugelassene Arzneimittel (meist Originalpräparate) – für Generika bitte die PZN-Suche verwenden.")
                          .arg(m_med.name.toHtmlEscaped()));
        else
            setStatus(tr("Abruf fehlgeschlagen: %1").arg(r->errorString().toHtmlEscaped()));
        return;
    }
    if (!PdfTools::isPdf(data)) {
        setStatus(tr("Unter <a href=\"%1\">dieser Adresse</a> liegt keine PDF-Datei, sondern eine Webseite. "
                     "Sie wurde im Webbrowser geöffnet – dort den Link zum PDF suchen.").arg(url.toString().toHtmlEscaped()));
        QDesktopServices::openUrl(url);
        return;
    }
    if (showPdf(data, url.toString())) {
        m_stored = false;
        setStatus(tr("Abgerufen von <a href=\"%1\">%2</a> (%3 KB).%4")
                      .arg(url.toString().toHtmlEscaped(), url.host().toHtmlEscaped()).arg(data.size() / 1024)
                      .arg(url.host().contains(QLatin1String("ema.europa.eu"))
                               ? tr(" Die Packungsbeilage steht im hinteren Teil (Anhang III B).") : QString()));
    }
    updateButtons();
}

bool LeafletDialog::showPdf(const QByteArray &pdf, const QString &origin)
{
    if (!PdfTools::isPdf(pdf)) return false;
    m_pdf = pdf;
    m_origin = origin;
    m_title = tr("Beipackzettel %1").arg(m_med.name + (m_med.strength.isEmpty() ? QString() : QLatin1Char(' ') + m_med.strength));
#ifdef VP_HAVE_QTPDF
    auto file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/vitaparms-XXXXXX.pdf"));
    auto *doc = new QPdfDocument(this);
    if (file->open()) {
        file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file->write(pdf);
        file->flush();
        if (doc->load(file->fileName()) == QPdfDocument::Error::None) {
            m_view->setDocument(doc);
            delete m_doc;
            m_doc = doc;
            m_file = std::move(file);
            m_stack->setCurrentWidget(m_view);
            updateButtons();
            return true;
        }
    }
    delete doc;
    m_text->setHtml(tr("<p>Das PDF kann hier nicht angezeigt werden (evtl. kennwortgeschützt). "
                       "„Extern öffnen“ verwendet das Standardprogramm.</p>"));
#else
    m_text->setHtml(tr("<p>PDF geladen. Ohne Qt PDF übersetzt – „Extern öffnen“ zeigt es im Standardprogramm.</p>"));
#endif
    m_stack->setCurrentWidget(m_text);
    updateButtons();
    return true;
}

void LeafletDialog::showHtml(const QString &html, const QString &origin, const QString &title)
{
    m_text->setHtml(html);
    m_stack->setCurrentWidget(m_text);
    // Für Speichern/Drucken als PDF bereithalten
    m_pdf = Leaflet::htmlToPdf(html, title);
    m_origin = origin;
    m_title = tr("Arzneimittelinformation (FDA) %1").arg(title);
    m_stored = false;
    setStatus(tr("US-Arzneimittelinformation von openFDA – englisch, nicht die deutsche Packungsbeilage."));
    updateButtons();
}

void LeafletDialog::askUrl()
{
    bool ok = false;
    const QString s = QInputDialog::getText(this, tr("PDF-Adresse laden"),
        tr("Adresse (Link) der Gebrauchsinformation als PDF:"), QLineEdit::Normal,
        QString(), &ok).trimmed();
    if (!ok || s.isEmpty()) return;
    const QUrl url = QUrl::fromUserInput(s);
    if (!url.isValid() || !url.scheme().startsWith(QLatin1String("http"))) {
        setStatus(tr("Ungültige Adresse."));
        return;
    }
    loadUrl(url, Leaflet::Kind::EmaPdf);
}

void LeafletDialog::loadFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Beipackzettel (PDF) öffnen"),
        QSettings().value(QStringLiteral("leaflet/lastDir"), QDir::homePath()).toString(), tr("PDF-Dateien (*.pdf)"));
    if (path.isEmpty()) return;
    QSettings().setValue(QStringLiteral("leaflet/lastDir"), QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !showPdf(f.readAll(), QFileInfo(path).fileName())) {
        setStatus(tr("Die Datei ist keine lesbare PDF-Datei."));
        return;
    }
    m_stored = false;
    setStatus(tr("Datei geladen: %1").arg(QFileInfo(path).fileName().toHtmlEscaped()));
    updateButtons();
}

bool LeafletDialog::saveToRecord()
{
    if (m_pdf.isEmpty() || m_patientId == 0) return false;
    Database &db = Database::instance();
    const QByteArray sha = QCryptographicHash::hash(m_pdf, QCryptographicHash::Sha256);
    qint64 docId = 0;
    if (const auto existing = db.findDocumentByHash(m_patientId, sha)) {
        docId = existing->id;   // bereits in der Akte – nur verknüpfen
    } else {
        const PdfTools::Info info = PdfTools::analyze(m_pdf);
        MedicalDocument d;
        d.patientId = m_patientId;
        d.docDate = QDate::currentDate();
        d.category = tr("Beipackzettel");
        d.title = m_title;
        d.issuer = m_origin.startsWith(QLatin1String("http")) ? QUrl(m_origin).host() : QString();
        d.notes = tr("Quelle: %1\nAbgerufen am %2. Maßgeblich ist der Beipackzettel in der Packung.")
                      .arg(m_origin, QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("dd.MM.yyyy HH:mm")));
        if (!m_med.pzn.isEmpty()) d.notes += tr("\nPZN: %1").arg(m_med.pzn);
        d.fileName = QStringLiteral("Beipackzettel_%1.pdf").arg(Leaflet::slug(m_med.name));
        d.sha256 = sha;
        d.pages = info.pages;
        d.fullText = info.text;
        d.extra[QStringLiteral("medicationName")] = m_med.name;
        QString err;
        if (!db.addDocument(d, m_pdf, &err)) {
            QMessageBox::critical(this, windowTitle(), tr("Speichern fehlgeschlagen:\n%1").arg(err));
            return false;
        }
        docId = d.id;
    }
    m_med.extra[QLatin1String(Leaflet::kLeafletDocKey)] = docId;
    m_med.extra[QLatin1String(Leaflet::kLeafletUrlKey)] = m_origin;
    m_changed = true;
    m_stored = true;
    setStatus(tr("In der Krankenakte gespeichert (Art „Beipackzettel“) und mit dem Medikament verknüpft."));
    updateButtons();
    return true;
}

void LeafletDialog::printPdf()
{
    if (m_pdf.isEmpty()) return;
#ifdef VP_HAVE_QTPDF
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(m_title);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    if (!PdfTools::printDocuments(&printer, { m_pdf }, &err))
        QMessageBox::warning(this, windowTitle(), err);
#else
    openExternally();
#endif
}

void LeafletDialog::openExternally()
{
    if (m_pdf.isEmpty()) return;
    // gleicher Ordner wie die Ansichtskopien der Krankenakte – wird beim Abmelden geleert
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("VitaParms-Ansicht"));
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/Beipackzettel_%1.pdf").arg(Leaflet::slug(m_med.name));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(m_pdf);
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}
