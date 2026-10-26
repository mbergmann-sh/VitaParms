// SPDX-License-Identifier: MIT
#include "documentdialog.h"
#include "database.h"
#include "formloader.h"
#include "pdftools.h"

#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

DocumentDialog::DocumentDialog(const MedicalDocument &doc, const QByteArray &pdf, Mode mode,
                               const QString &progressText, QWidget *parent)
    : QDialog(parent), m_doc(doc)
{
    setWindowTitle(mode == Mode::Import ? tr("Dokument übernehmen %1").arg(progressText).trimmed()
                                        : tr("Dokument bearbeiten"));
    auto *lay = new QVBoxLayout(this);
    m_form = FormLoader::load(QStringLiteral("documentform"), this);
    lay->addWidget(m_form);

    auto *buttons = new QDialogButtonBox(this);
    if (mode == Mode::Import) {
        buttons->addButton(tr("&Übernehmen"), QDialogButtonBox::AcceptRole);
        QPushButton *skip = buttons->addButton(tr("Ü&berspringen"), QDialogButtonBox::DestructiveRole);
        connect(skip, &QPushButton::clicked, this, [this] { m_skipped = true; QDialog::reject(); });
        buttons->addButton(tr("Import abbrechen"), QDialogButtonBox::RejectRole);
    } else {
        buttons->addButton(QDialogButtonBox::Save)->setText(tr("&Speichern"));
        buttons->addButton(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &DocumentDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &DocumentDialog::reject);
    lay->addWidget(buttons);

    // Kategorien: Standardliste + bereits verwendete
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("categoryCombo"))) {
        QStringList cats = PdfTools::defaultCategories();
        for (const QString &x : Database::instance().documentCategories())
            if (!cats.contains(x, Qt::CaseInsensitive)) cats << x;
        c->clear();
        c->addItems(cats);
    }
    // Absender-Vorschläge aus vorhandenen Dokumenten
    if (auto *e = m_form->findChild<QLineEdit *>(QStringLiteral("issuerEdit"))) {
        QSet<QString> seen;
        QStringList issuers;
        for (const MedicalDocument &d : Database::instance().documents(doc.patientId))
            if (!d.issuer.isEmpty() && !seen.contains(d.issuer)) { seen.insert(d.issuer); issuers << d.issuer; }
        auto *comp = new QCompleter(issuers, e);
        comp->setCaseSensitivity(Qt::CaseInsensitive);
        comp->setFilterMode(Qt::MatchContains);
        e->setCompleter(comp);
    }

    FormIO::write(m_form, QStringLiteral("dateEdit"), doc.docDate.isValid() ? doc.docDate : QDate::currentDate());
    FormIO::write(m_form, QStringLiteral("categoryCombo"), doc.category);
    FormIO::write(m_form, QStringLiteral("titleEdit"), doc.title);
    FormIO::write(m_form, QStringLiteral("issuerEdit"), doc.issuer);
    FormIO::write(m_form, QStringLiteral("notesEdit"), doc.notes);
    FormIO::applyExtra(m_form, doc.extra);

    const QLocale loc;
    FormIO::write(m_form, QStringLiteral("fileInfoLabel"),
                  tr("<small>Datei: %1<br>%2 Seite(n), %3</small>")
                      .arg(doc.fileName.toHtmlEscaped())
                      .arg(doc.pages)
                      .arg(loc.formattedDataSize(doc.size)));
    QString textInfo;
    if (!PdfTools::available())
        textInfo = tr("<small>Volltextsuche nicht verfügbar (ohne Qt PDF übersetzt).</small>");
    else if (doc.hasText())
        textInfo = tr("<small>✔ Text erkannt – der Inhalt ist durchsuchbar.</small>");
    else
        textInfo = tr("<small>⚠ Kein Text gefunden (vermutlich ein Scan). Nur Titel, Art, "
                      "Absender und Notiz sind durchsuchbar – tragen Sie ggf. Stichworte in "
                      "die Notiz ein.</small>");
    FormIO::write(m_form, QStringLiteral("textInfoLabel"), textInfo);

    if (auto *pv = m_form->findChild<QLabel *>(QStringLiteral("previewLabel"))) {
        const QImage img = pdf.isEmpty() ? QImage() : PdfTools::renderPage(pdf, 0, 520);
        if (!img.isNull())
            pv->setPixmap(QPixmap::fromImage(img).scaled(pv->minimumSize().expandedTo(QSize(260, 360)),
                                                         Qt::KeepAspectRatio, Qt::SmoothTransformation));
        else
            pv->setText(tr("(keine Vorschau)"));
    }

    if (auto *t = m_form->findChild<QWidget *>(QStringLiteral("titleEdit"))) t->setFocus();
}

void DocumentDialog::accept()
{
    MedicalDocument d = m_doc;
    d.title = FormIO::readString(m_form, QStringLiteral("titleEdit"), d.title).trimmed();
    if (d.title.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Bitte einen Titel angeben."));
        return;
    }
    if (const auto v = FormIO::read(m_form, QStringLiteral("dateEdit")))
        d.docDate = v->toDate();
    d.category = FormIO::readString(m_form, QStringLiteral("categoryCombo"), d.category).trimmed();
    d.issuer   = FormIO::readString(m_form, QStringLiteral("issuerEdit"), d.issuer).trimmed();
    d.notes    = FormIO::readString(m_form, QStringLiteral("notesEdit"), d.notes).trimmed();
    d.extra    = FormIO::collectExtra(m_form, d.extra);
    m_doc = d;
    QDialog::accept();
}
