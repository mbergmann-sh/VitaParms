// SPDX-License-Identifier: MIT
#include "documentspage.h"
#include "akteexport.h"
#include "database.h"
#include "documentdialog.h"
#include "pdftools.h"
#include "reportprinter.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

#ifdef VP_HAVE_QTPDF
#include <QPdfDocument>
#include <QPdfView>
#endif

namespace {

QStringList imageSuffixes()
{
    return { QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("tif"),
             QStringLiteral("tiff"), QStringLiteral("bmp"), QStringLiteral("webp") };
}

QString haystack(const MedicalDocument &d)
{
    return (d.title + QLatin1Char('\n') + d.category + QLatin1Char('\n') + d.issuer + QLatin1Char('\n')
            + d.notes + QLatin1Char('\n') + d.fileName + QLatin1Char('\n')
            + (d.docDate.isValid() ? d.docDate.toString(QStringLiteral("dd.MM.yyyy")) : QString())
            + QLatin1Char('\n') + d.fullText);
}

QString viewerTempDir()
{
    const QString d = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                          .filePath(QStringLiteral("VitaParms-Ansicht"));
    QDir().mkpath(d);
    return d;
}

} // namespace

// ================================================================ DocumentModel

void DocumentModel::setDocuments(const QList<MedicalDocument> &docs, const QStringList &terms)
{
    beginResetModel();
    m_docs = docs;
    m_terms = terms;
    endResetModel();
}

const MedicalDocument *DocumentModel::at(int row) const
{
    return row >= 0 && row < m_docs.size() ? &m_docs[row] : nullptr;
}

int DocumentModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_docs.size(); }
int DocumentModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColCount; }

QVariant DocumentModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColDate:     return tr("Datum");
    case ColCategory: return tr("Art");
    case ColTitle:    return tr("Titel");
    case ColIssuer:   return tr("Von");
    case ColPages:    return tr("Seiten");
    case ColText:     return tr("Text");
    case ColHit:      return tr("Fundstelle");
    }
    return {};
}

QVariant DocumentModel::data(const QModelIndex &index, int role) const
{
    const MedicalDocument *d = at(index.row());
    if (!d) return {};
    const QLocale loc;
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColDate:     return d->docDate.isValid() ? loc.toString(d->docDate, QStringLiteral("dd.MM.yyyy")) : QString();
        case ColCategory: return d->category;
        case ColTitle:    return d->title;
        case ColIssuer:   return d->issuer;
        case ColPages:    return d->pages > 0 ? QVariant(d->pages) : QVariant();
        case ColText:     return d->hasText() ? QStringLiteral("✔") : QString();
        case ColHit: {
            for (const QString &t : m_terms) {
                const QString s = PdfTools::snippet(d->fullText, t, 40);
                if (!s.isEmpty()) return s;
                const QString n = PdfTools::snippet(d->notes, t, 40);
                if (!n.isEmpty()) return n;
            }
            return {};
        }
        }
    }
    if (role == Qt::ToolTipRole) {
        QString t = QStringLiteral("<b>%1</b><br>%2 · %3<br>%4")
            .arg(d->title.toHtmlEscaped(), d->category.toHtmlEscaped(), d->issuer.toHtmlEscaped(),
                 tr("%1 Seite(n), %2, Datei: %3").arg(d->pages).arg(loc.formattedDataSize(d->size))
                     .arg(d->fileName.toHtmlEscaped()));
        if (!d->notes.isEmpty()) t += QStringLiteral("<br><i>%1</i>").arg(d->notes.toHtmlEscaped());
        if (!d->hasText()) t += tr("<br><small>Kein durchsuchbarer Text (Scan).</small>");
        return t;
    }
    if (role == Qt::TextAlignmentRole && (index.column() == ColPages || index.column() == ColText))
        return int(Qt::AlignCenter);
    if (role == Qt::UserRole) return d->id;
    return {};
}

void DocumentModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    auto key = [column](const MedicalDocument &d) -> QString {
        switch (column) {
        case ColDate:     return d.docDate.toString(Qt::ISODate) + QStringLiteral("%1").arg(d.id, 10, 10, QLatin1Char('0'));
        case ColCategory: return d.category.toLower();
        case ColTitle:    return d.title.toLower();
        case ColIssuer:   return d.issuer.toLower();
        case ColPages:    return QStringLiteral("%1").arg(d.pages, 6, 10, QLatin1Char('0'));
        default:          return d.docDate.toString(Qt::ISODate);
        }
    };
    std::stable_sort(m_docs.begin(), m_docs.end(), [&](const MedicalDocument &a, const MedicalDocument &b) {
        return order == Qt::AscendingOrder ? key(a) < key(b) : key(b) < key(a);
    });
    endResetModel();
}

// ================================================================ DocumentsPage

DocumentsPage::DocumentsPage(QWidget *parent) : QWidget(parent)
{
    setAcceptDrops(true);
    auto *v = new QVBoxLayout(this);

    // ---- Suche / Filter
    auto *top = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(PdfTools::available()
        ? tr("Suchen in Titel, Inhalt, Absender, Notizen … (mehrere Begriffe = alle müssen vorkommen)")
        : tr("Suchen in Titel, Absender, Notizen …"));
    m_search->setClearButtonEnabled(true);
    top->addWidget(new QLabel(tr("Suche:"), this));
    top->addWidget(m_search, 1);
    m_category = new QComboBox(this);
    m_category->setMinimumContentsLength(14);
    top->addWidget(m_category);
    m_year = new QComboBox(this);
    top->addWidget(m_year);
    m_count = new QLabel(this);
    top->addWidget(m_count);
    v->addLayout(top);

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(200);
    connect(m_search, &QLineEdit::textChanged, timer, qOverload<>(&QTimer::start));
    connect(timer, &QTimer::timeout, this, &DocumentsPage::applyFilter);
    connect(m_category, &QComboBox::activated, this, &DocumentsPage::applyFilter);
    connect(m_year, &QComboBox::activated, this, &DocumentsPage::applyFilter);

    // ---- Liste + Vorschau
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_model = new DocumentModel(this);
    m_table = new QTableView(m_splitter);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 8);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(DocumentModel::ColDate, Qt::DescendingOrder);
    m_table->setWordWrap(false);
    connect(m_table, &QTableView::doubleClicked, this, &DocumentsPage::editDocument);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        updatePreview();
        updateButtons();
        emit selectionChanged();
    });

    auto *previewBox = new QWidget(m_splitter);
    auto *pv = new QVBoxLayout(previewBox);
    pv->setContentsMargins(0, 0, 0, 0);
    m_previewInfo = new QLabel(previewBox);
    m_previewInfo->setWordWrap(true);
    pv->addWidget(m_previewInfo);
    m_previewStack = new QStackedWidget(previewBox);
    m_noPreview = new QLabel(previewBox);
    m_noPreview->setAlignment(Qt::AlignCenter);
    m_noPreview->setWordWrap(true);
    m_previewStack->addWidget(m_noPreview);
#ifdef VP_HAVE_QTPDF
    m_view = new QPdfView(previewBox);
    m_view->setPageMode(QPdfView::PageMode::MultiPage);
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    m_previewStack->addWidget(m_view);
#endif
    pv->addWidget(m_previewStack, 1);
    m_splitter->addWidget(m_table);
    m_splitter->addWidget(previewBox);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    v->addWidget(m_splitter, 1);

    // ---- Schaltflächen
    auto *row = new QHBoxLayout;
    auto button = [this, row](const QString &text, void (DocumentsPage::*slot)(), const QString &tip = {}) {
        auto *b = new QPushButton(text, this);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, slot);
        row->addWidget(b);
        return b;
    };
    button(tr("&Hinzufügen…"), &DocumentsPage::addDocuments,
           tr("PDF-Dateien oder Fotos/Scans (JPG, PNG, TIFF) übernehmen – auch per Drag & Drop"));
    m_needOne << button(tr("&Bearbeiten…"), &DocumentsPage::editDocument);
    m_needSel << button(tr("&Löschen…"), &DocumentsPage::deleteDocuments);
    row->addSpacing(16);
    m_needSel << button(tr("Ö&ffnen"), &DocumentsPage::openExternal, tr("Im Standard-PDF-Programm öffnen"));
    m_needSel << button(tr("Speichern &unter…"), &DocumentsPage::saveCopies);
    row->addStretch();
    m_needSel << button(tr("&Drucken…"), &DocumentsPage::printSelected, tr("Ausgewählte Dokumente drucken (mit Vorschau)"));
    button(tr("Liste drucken…"), &DocumentsPage::printList, tr("Inhaltsverzeichnis der angezeigten Dokumente"));
    button(tr("Akte &packen / senden…"), &DocumentsPage::packAndSend,
           tr("Dokumente und Berichte als (verschlüsseltes) ZIP speichern oder per E-Mail senden"));
    v->addLayout(row);

    connect(&Database::instance(), &Database::documentsChanged, this, [this](qint64 pid) {
        if (m_patient && m_patient->id == pid) reload();
    });
    setPatient(std::nullopt);
}

DocumentsPage::~DocumentsPage() = default;

void DocumentsPage::cleanupTempFiles()
{
    QDir(viewerTempDir()).removeRecursively();
}

void DocumentsPage::setPatient(const std::optional<Patient> &p)
{
    const bool changed = !m_patient || !p || m_patient->id != p->id;
    m_patient = p;
    if (changed) {
        m_search->clear();
        m_previewId = 0;
    }
    reload();
}

void DocumentsPage::reload()
{
    m_all = m_patient ? Database::instance().documents(m_patient->id) : QList<MedicalDocument>();

    const QString cat = m_category->currentData().toString();
    m_category->clear();
    m_category->addItem(tr("Alle Arten"), QString());
    QStringList cats;
    for (const MedicalDocument &d : m_all)
        if (!d.category.isEmpty() && !cats.contains(d.category)) cats << d.category;
    cats.sort(Qt::CaseInsensitive);
    for (const QString &c : cats) m_category->addItem(c, c);
    m_category->setCurrentIndex(std::max(0, m_category->findData(cat)));

    const QVariant year = m_year->currentData();
    m_year->clear();
    m_year->addItem(tr("Alle Jahre"), 0);
    QList<int> years;
    for (const MedicalDocument &d : m_all)
        if (d.docDate.isValid() && !years.contains(d.docDate.year())) years << d.docDate.year();
    std::sort(years.begin(), years.end(), std::greater<int>());
    for (int y : years) m_year->addItem(QString::number(y), y);
    m_year->setCurrentIndex(std::max(0, m_year->findData(year)));

    setEnabled(m_patient.has_value());
    applyFilter();
}

void DocumentsPage::setSearchText(const QString &t)
{
    m_search->setText(t);
    applyFilter();
}

void DocumentsPage::applyFilter()
{
    const QStringList terms = m_search->text().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    const QString cat = m_category->currentData().toString();
    const int year = m_year->currentData().toInt();
    QList<MedicalDocument> out;
    for (const MedicalDocument &d : m_all) {
        if (!cat.isEmpty() && d.category != cat) continue;
        if (year && d.docDate.year() != year) continue;
        if (!terms.isEmpty()) {
            const QString h = haystack(d);
            bool all = true;
            for (const QString &t : terms)
                if (!h.contains(t, Qt::CaseInsensitive)) { all = false; break; }
            if (!all) continue;
        }
        out << d;
    }
    const QList<qint64> keep = selectedIds();
    m_model->setDocuments(out, terms);
    m_model->sort(m_table->horizontalHeader()->sortIndicatorSection(), m_table->horizontalHeader()->sortIndicatorOrder());
    m_table->setColumnHidden(DocumentModel::ColHit, terms.isEmpty());
    m_table->resizeColumnsToContents();
    m_table->setColumnWidth(DocumentModel::ColTitle, std::min(m_table->columnWidth(DocumentModel::ColTitle), 320));
    for (int r = 0; r < m_model->rowCount(); ++r)
        if (keep.contains(m_model->at(r)->id))
            m_table->selectionModel()->select(m_model->index(r, 0),
                                              QItemSelectionModel::Select | QItemSelectionModel::Rows);
    m_count->setText(out.size() == m_all.size() ? tr("%1 Dokument(e)").arg(m_all.size())
                                                : tr("%1 von %2").arg(out.size()).arg(m_all.size()));
    updatePreview();
    updateButtons();
}

QList<MedicalDocument> DocumentsPage::selectedDocs() const
{
    QList<MedicalDocument> l;
    QList<int> rows;
    for (const QModelIndex &i : m_table->selectionModel()->selectedRows()) rows << i.row();
    std::sort(rows.begin(), rows.end());
    for (int r : rows) if (const auto *d = m_model->at(r)) l << *d;
    return l;
}

QList<qint64> DocumentsPage::selectedIds() const
{
    QList<qint64> l;
    for (const MedicalDocument &d : selectedDocs()) l << d.id;
    return l;
}

QList<qint64> DocumentsPage::filteredIds() const
{
    QList<qint64> l;
    for (const MedicalDocument &d : m_model->documents()) l << d.id;
    return l;
}

QList<qint64> DocumentsPage::allIds() const
{
    QList<MedicalDocument> sorted = m_all;   // bereits nach Datum absteigend
    QList<qint64> l;
    for (const MedicalDocument &d : sorted) l << d.id;
    return l;
}

void DocumentsPage::updateButtons()
{
    const int n = int(m_table->selectionModel()->selectedRows().size());
    for (QWidget *w : m_needSel) w->setEnabled(n > 0);
    for (QWidget *w : m_needOne) w->setEnabled(n == 1);
}

void DocumentsPage::updatePreview()
{
    const auto sel = selectedDocs();
    if (sel.size() != 1) {
        m_previewId = 0;
        m_previewInfo->clear();
        m_noPreview->setText(m_all.isEmpty() && m_patient
            ? tr("Noch keine Dokumente.\n\nÜber „Hinzufügen…“ oder per Drag & Drop können Sie "
                 "PDF-Dateien (Befunde, Laborwerte, Entlassbriefe …) sowie Fotos/Scans übernehmen.")
            : sel.isEmpty() ? tr("Dokument auswählen, um es hier anzuzeigen.")
                            : tr("%1 Dokumente ausgewählt.").arg(sel.size()));
        m_previewStack->setCurrentIndex(0);
        return;
    }
    const MedicalDocument &d = sel.first();
    const QLocale loc;
    QStringList info;
    if (d.docDate.isValid()) info << loc.toString(d.docDate, QStringLiteral("dd.MM.yyyy"));
    if (!d.category.isEmpty()) info << d.category.toHtmlEscaped();
    if (!d.issuer.isEmpty()) info << d.issuer.toHtmlEscaped();
    info << tr("%n Seite(n)").replace(QStringLiteral("%n"), QString::number(d.pages));
    m_previewInfo->setText(QStringLiteral("<b>%1</b><br><small>%2</small>")
        .arg(d.title.toHtmlEscaped(), info.join(QStringLiteral(" · "))));
    if (d.id == m_previewId) return;
    m_previewId = d.id;
#ifdef VP_HAVE_QTPDF
    // Für jedes Dokument ein neues QPdfDocument – QPdfView verwirft sonst
    // zwischengespeicherte Seitenbilder des vorherigen Dokuments nicht zuverlässig.
    auto file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/vitaparms-XXXXXX.pdf"));
    auto *doc = new QPdfDocument(this);
    bool ok = false;
    if (file->open()) {
        file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file->write(Database::instance().documentData(d.id));
        file->flush();
        ok = doc->load(file->fileName()) == QPdfDocument::Error::None;
    }
    if (ok) {
        m_view->setDocument(doc);
        delete m_pdf;
        m_pdf = doc;
        m_previewFile = std::move(file);
        m_previewStack->setCurrentIndex(1);
        return;
    }
    delete doc;
    m_noPreview->setText(tr("Vorschau nicht möglich (evtl. kennwortgeschützte PDF).\n„Öffnen“ verwendet das Standardprogramm."));
#else
    m_noPreview->setText(tr("Eingebaute Vorschau nicht verfügbar (ohne Qt PDF übersetzt).\n"
                            "„Öffnen“ zeigt das Dokument im Standard-PDF-Programm."));
#endif
    m_previewStack->setCurrentIndex(0);
}

// ---------------------------------------------------------------- Import

void DocumentsPage::addDocuments()
{
    if (!m_patient) return;
    QSettings s;
    const QString dir = s.value(QStringLiteral("paths/import"),
                                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    QStringList patterns = { QStringLiteral("*.pdf") };
    for (const QString &x : imageSuffixes()) patterns << QStringLiteral("*.") + x;
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Dokumente hinzufügen"), dir,
        tr("PDF und Bilder (%1);;PDF-Dateien (*.pdf);;Alle Dateien (*)").arg(patterns.join(QLatin1Char(' '))));
    if (files.isEmpty()) return;
    s.setValue(QStringLiteral("paths/import"), QFileInfo(files.first()).absolutePath());
    importFiles(files);
}

void DocumentsPage::importFiles(const QStringList &files)
{
    if (!m_patient) return;
    Database &db = Database::instance();
    int added = 0, skipped = 0;
    for (int i = 0; i < files.size(); ++i) {
        const QString &path = files[i];
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Dokument hinzufügen"), tr("%1 kann nicht gelesen werden.").arg(path));
            ++skipped;
            continue;
        }
        QByteArray data = f.readAll();
        QString fileName = QFileInfo(path).fileName();
        bool fromImage = false;
        if (imageSuffixes().contains(QFileInfo(path).suffix().toLower())) {
            QImageReader reader(path);
            reader.setAutoTransform(true);           // Foto-Ausrichtung (EXIF) beachten
            const QImage img = reader.read();
            if (img.isNull()) {
                QMessageBox::warning(this, tr("Dokument hinzufügen"), tr("%1 ist kein lesbares Bild.").arg(fileName));
                ++skipped;
                continue;
            }
            data = PdfTools::imageToPdf(img, QFileInfo(path).completeBaseName());
            fromImage = true;
            fileName = QFileInfo(path).completeBaseName() + QStringLiteral(".pdf");
        }
        if (!PdfTools::isPdf(data)) {
            QMessageBox::warning(this, tr("Dokument hinzufügen"),
                                 tr("%1 ist weder eine PDF-Datei noch ein Bild und wird übersprungen.").arg(fileName));
            ++skipped;
            continue;
        }
        if (data.size() > 100 * 1024 * 1024
            && QMessageBox::question(this, tr("Dokument hinzufügen"),
                   tr("%1 ist sehr groß (%2). Trotzdem übernehmen?")
                       .arg(fileName, QLocale().formattedDataSize(data.size()))) != QMessageBox::Yes) {
            ++skipped;
            continue;
        }
        const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
        if (const auto dup = db.findDocumentByHash(m_patient->id, hash)) {
            if (QMessageBox::question(this, tr("Dokument hinzufügen"),
                    tr("„%1“ ist bereits in der Akte vorhanden (als „%2“ vom %3).\nTrotzdem erneut übernehmen?")
                        .arg(fileName, dup->title, QLocale().toString(dup->docDate, QStringLiteral("dd.MM.yyyy"))),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                ++skipped;
                continue;
            }
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        PdfTools::Info info = PdfTools::analyze(data);
        QApplication::restoreOverrideCursor();
        if (fromImage)   // bei Fotos/Scans: Dateidatum statt Umwandlungszeitpunkt
            info.metaDate = QFileInfo(path).lastModified().date();

        MedicalDocument d;
        d.patientId = m_patient->id;
        d.fileName  = fileName;
        d.sha256    = hash;
        d.size      = data.size();
        d.pages     = info.pages;
        d.fullText  = info.text;
        d.docDate   = PdfTools::guessDate(fileName, info, m_patient->birthDate);
        d.category  = PdfTools::guessCategory(fileName, info.text);
        d.title     = PdfTools::guessTitle(fileName, info);

        DocumentDialog dlg(d, data, DocumentDialog::Mode::Import,
                           files.size() > 1 ? QStringLiteral("(%1/%2)").arg(i + 1).arg(files.size()) : QString(), this);
        if (dlg.exec() != QDialog::Accepted) {
            if (dlg.skipped()) { ++skipped; continue; }
            break;                                   // Import abgebrochen
        }
        d = dlg.document();
        QString err;
        if (!db.addDocument(d, data, &err)) {
            QMessageBox::critical(this, tr("Dokument hinzufügen"), tr("Speichern fehlgeschlagen:\n%1").arg(err));
            break;
        }
        ++added;
    }
    if (files.size() > 1)
        m_count->setToolTip(tr("Zuletzt: %1 übernommen, %2 übersprungen").arg(added).arg(skipped));
}

void DocumentsPage::dragEnterEvent(QDragEnterEvent *e)
{
    if (m_patient && e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void DocumentsPage::dropEvent(QDropEvent *e)
{
    QStringList files;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile()) files << u.toLocalFile();
    if (!files.isEmpty()) {
        e->acceptProposedAction();
        // Dialoge nicht innerhalb des Drop-Ereignisses öffnen
        QTimer::singleShot(0, this, [this, files] { importFiles(files); });
    }
}

// ---------------------------------------------------------------- Bearbeiten

void DocumentsPage::editDocument()
{
    const auto sel = selectedDocs();
    if (sel.size() != 1) return;
    DocumentDialog dlg(sel.first(), Database::instance().documentData(sel.first().id),
                       DocumentDialog::Mode::Edit, {}, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    if (!Database::instance().updateDocument(dlg.document(), &err))
        QMessageBox::critical(this, tr("Dokument bearbeiten"), err);
    m_previewId = 0;
}

void DocumentsPage::deleteDocuments()
{
    const auto sel = selectedDocs();
    if (sel.isEmpty()) return;
    QStringList names;
    for (int i = 0; i < std::min<qsizetype>(sel.size(), 8); ++i)
        names << QStringLiteral("• ") + sel[i].title;
    if (sel.size() > 8) names << tr("… und %1 weitere").arg(sel.size() - 8);
    QMessageBox box(QMessageBox::Warning, tr("Dokumente löschen"),
                    tr("%1 Dokument(e) endgültig aus der Akte löschen?\n\n%2")
                        .arg(sel.size()).arg(names.join(QLatin1Char('\n'))),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;
    for (const MedicalDocument &d : sel) {
        QString err;
        if (!Database::instance().deleteDocument(d.id, &err)) {
            QMessageBox::critical(this, tr("Dokumente löschen"), err);
            break;
        }
    }
}

void DocumentsPage::openExternal()
{
    const auto sel = selectedDocs();
    if (sel.size() > 5 && QMessageBox::question(this, tr("Öffnen"),
            tr("%1 Dokumente gleichzeitig öffnen?").arg(sel.size())) != QMessageBox::Yes)
        return;
    for (int i = 0; i < sel.size(); ++i) {
        // Lesbarer Dateiname im privaten Temp-Ordner; wird beim Beenden gelöscht
        const QString path = QDir(viewerTempDir()).filePath(AkteExport::archiveFileName(i + 1, sel[i]));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) continue;
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        f.write(Database::instance().documentData(sel[i].id));
        f.close();
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void DocumentsPage::saveCopies()
{
    const auto sel = selectedDocs();
    if (sel.isEmpty()) return;
    QSettings s;
    const QString dir = s.value(QStringLiteral("paths/export"),
                                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    if (sel.size() == 1) {
        const QString path = QFileDialog::getSaveFileName(this, tr("Dokument speichern"),
            QDir(dir).filePath(AkteExport::archiveFileName(1, sel.first()).mid(4)), tr("PDF-Dateien (*.pdf)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(Database::instance().documentData(sel.first().id)) < 0)
            QMessageBox::critical(this, tr("Dokument speichern"), f.errorString());
        s.setValue(QStringLiteral("paths/export"), QFileInfo(path).absolutePath());
        return;
    }
    const QString target = QFileDialog::getExistingDirectory(this, tr("Zielordner für %1 Dokumente").arg(sel.size()), dir);
    if (target.isEmpty()) return;
    s.setValue(QStringLiteral("paths/export"), target);
    int ok = 0;
    for (int i = 0; i < sel.size(); ++i) {
        QFile f(QDir(target).filePath(AkteExport::archiveFileName(i + 1, sel[i])));
        if (f.open(QIODevice::WriteOnly) && f.write(Database::instance().documentData(sel[i].id)) >= 0) ++ok;
    }
    QMessageBox::information(this, tr("Dokumente speichern"),
                             tr("%1 von %2 Dokument(en) gespeichert in\n%3").arg(ok).arg(sel.size()).arg(QDir::toNativeSeparators(target)));
}

// ---------------------------------------------------------------- Ausgabe

void DocumentsPage::printSelected()
{
    const auto sel = selectedDocs();
    if (sel.isEmpty()) return;
    if (!PdfTools::available()) {
        QMessageBox::information(this, tr("Drucken"),
            tr("Diese Programmversion kann PDFs nicht selbst drucken. Die Dokumente werden im "
               "Standard-PDF-Programm geöffnet und können dort gedruckt werden."));
        openExternal();
        return;
    }
    QList<QByteArray> data;
    int pages = 0;
    for (const MedicalDocument &d : sel) { data << Database::instance().documentData(d.id); pages += d.pages; }

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(5, 5, 5, 5),
                                      QPageLayout::Millimeter));
    printer.setDocName(sel.size() == 1 ? sel.first().title : tr("Krankenakte – %1 Dokumente").arg(sel.size()));
    QPrintPreviewDialog dlg(&printer, this);
    dlg.setWindowTitle(tr("Drucken – %1 Dokument(e), %2 Seite(n)").arg(sel.size()).arg(pages));
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, [&](QPrinter *p) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString err;
        if (!PdfTools::printDocuments(p, data, &err))
            QMessageBox::warning(this, tr("Drucken"), err);
        QApplication::restoreOverrideCursor();
    });
    dlg.resize(900, 900);
    dlg.exec();
}

void DocumentsPage::printList()
{
    if (!m_patient) return;
    ReportPrinter::documentIndex(this, *m_patient, m_model->documents(), ReportPrinter::Target::Preview);
}

void DocumentsPage::packAndSend()
{
    if (!m_patient) return;
    AkteExportDialog d(*m_patient, selectedIds(), filteredIds(), allIds(), this);
    d.exec();
}
