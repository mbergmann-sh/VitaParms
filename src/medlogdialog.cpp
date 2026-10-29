// SPDX-License-Identifier: MIT
#include "medlogdialog.h"
#include "medprep.h"
#include "database.h"
#include "reportprinter.h"
#include "session.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {
enum Period { Days30, Days90, Months12, AllTime };
}

MedLogDialog::MedLogDialog(const Patient &p, QWidget *parent) : QDialog(parent), m_patient(p)
{
    setWindowTitle(tr("Protokoll Medikamente stellen – %1").arg(p.displayName()));
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Zeitraum:"), this));
    m_period = new QComboBox(this);
    m_period->addItem(tr("letzte 30 Tage"), Days30);
    m_period->addItem(tr("letzte 90 Tage"), Days90);
    m_period->addItem(tr("letzte 12 Monate"), Months12);
    m_period->addItem(tr("gesamter Zeitraum"), AllTime);
    top->addWidget(m_period);
    top->addStretch();
    m_count = new QLabel(this);
    top->addWidget(m_count);
    lay->addLayout(top);

    auto *split = new QSplitter(Qt::Vertical, this);
    m_table = new QTableWidget(0, 6, split);
    m_table->setHorizontalHeaderLabels({ tr("Gestellt am"), tr("Für"), tr("Gestellt von"), tr("Status"),
                                         tr("Medikamente"), tr("Bemerkung") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_details = new QTextBrowser(split);
    split->addWidget(m_table);
    split->addWidget(m_details);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);

    auto *bb = new QDialogButtonBox(this);
    auto *print = bb->addButton(tr("&Drucken…"), QDialogButtonBox::ActionRole);
    auto *pdf = bb->addButton(tr("Als &PDF…"), QDialogButtonBox::ActionRole);
    m_delete = bb->addButton(tr("Eintrag &löschen…"), QDialogButtonBox::ActionRole);
    m_delete->setVisible(Session::isAdmin());    // Protokoll: nur Administratoren dürfen löschen
    bb->addButton(tr("&Schließen"), QDialogButtonBox::RejectRole);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(print, &QPushButton::clicked, this, [this] {
        ReportPrinter::preparationLog(this, m_patient, m_log, periodText(), ReportPrinter::Target::Preview);
    });
    connect(pdf, &QPushButton::clicked, this, [this] {
        ReportPrinter::preparationLog(this, m_patient, m_log, periodText(), ReportPrinter::Target::Pdf);
    });
    connect(m_delete, &QPushButton::clicked, this, &MedLogDialog::deleteSelected);
    lay->addWidget(bb);

    connect(m_period, qOverload<int>(&QComboBox::currentIndexChanged), this, &MedLogDialog::reload);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &MedLogDialog::showDetails);
    connect(&Database::instance(), &Database::preparationsChanged, this, &MedLogDialog::reload);
    m_period->setCurrentIndex(Days90);
    reload();
    resize(900, 600);
}

int MedLogDialog::rowCount() const
{
    return m_table->rowCount();
}

QDate MedLogDialog::fromDate() const
{
    const QDate today = QDate::currentDate();
    switch (m_period->currentData().toInt()) {
    case Days30:   return today.addDays(-29);
    case Days90:   return today.addDays(-89);
    case Months12: return today.addMonths(-12).addDays(1);
    default:       return {};
    }
}

QString MedLogDialog::periodText() const
{
    const QDate f = fromDate();
    const QLocale loc;
    return f.isValid() ? tr("%1 – %2").arg(loc.toString(f, QStringLiteral("dd.MM.yyyy")),
                                          loc.toString(QDate::currentDate(), QStringLiteral("dd.MM.yyyy")))
                       : tr("gesamter Zeitraum");
}

QString MedLogDialog::statusText(const MedPreparation &e)
{
    return MedPrep::statusText(e);
}

QString MedLogDialog::itemsHtml(const MedPreparation &e)
{
    QString h = QStringLiteral("<ul>");
    for (const QJsonValue &v : e.items) {
        const QJsonObject o = v.toObject();
        QString line = QStringLiteral("<b>%1</b>").arg(o.value(QStringLiteral("name")).toString().toHtmlEscaped());
        QStringList d;
        for (const char *k : { "strength", "form" }) {
            const QString s = o.value(QLatin1String(k)).toString();
            if (!s.isEmpty()) d << s.toHtmlEscaped();
        }
        if (!d.isEmpty()) line += QStringLiteral(" ") + d.join(QStringLiteral(", "));
        const QString dose = o.value(QStringLiteral("dose")).toString();
        if (!dose.isEmpty()) line += QStringLiteral(" – ") + dose.toHtmlEscaped();
        if (!o.value(QStringLiteral("confirmed")).toBool(true))
            line += tr(" <i>(nicht bestätigt)</i>");
        h += QStringLiteral("<li>") + line + QStringLiteral("</li>");
    }
    return h + QStringLiteral("</ul>");
}

void MedLogDialog::reload()
{
    m_log = Database::instance().preparations(m_patient.id, fromDate());
    const QLocale loc;
    m_table->setRowCount(int(m_log.size()));
    for (int r = 0; r < m_log.size(); ++r) {
        const MedPreparation &e = m_log[r];
        const QStringList cells = {
            loc.toString(e.preparedAt, QStringLiteral("ddd dd.MM.yyyy HH:mm")),
            ReportPrinter::preparationForText(e),
            e.preparedBy,
            statusText(e),
            tr("%1 Medikament(e)").arg(e.items.size()),
            e.note,
        };
        for (int c = 0; c < cells.size(); ++c) {
            auto *it = new QTableWidgetItem(cells[c]);
            if (c == 3 && !e.complete) it->setForeground(QColor(0xD5, 0x5E, 0x00));
            m_table->setItem(r, c, it);
        }
    }
    m_table->resizeColumnsToContents();
    m_count->setText(tr("%1 Eintrag/Einträge").arg(m_log.size()));
    if (!m_log.isEmpty()) m_table->selectRow(0);
    else m_details->setHtml(tr("<p>Im gewählten Zeitraum wurden keine Medikamente über den Assistenten gestellt.</p>"));
}

void MedLogDialog::showDetails()
{
    const auto rows = m_table->selectionModel()->selectedRows();
    m_delete->setEnabled(!rows.isEmpty());
    if (rows.isEmpty()) return;
    const MedPreparation &e = m_log[rows.first().row()];
    QString h = QStringLiteral("<p><b>%1</b> · %2 · %3 · %4</p>")
                    .arg(QLocale().toString(e.preparedAt, QStringLiteral("dddd, dd.MM.yyyy HH:mm")).toHtmlEscaped(),
                         tr("für %1").arg(ReportPrinter::preparationForText(e)).toHtmlEscaped(),
                         tr("gestellt von %1").arg(e.preparedBy.toHtmlEscaped()), statusText(e).toHtmlEscaped());
    h += itemsHtml(e);
    if (!e.note.isEmpty()) h += tr("<p><b>Bemerkung:</b> %1</p>").arg(e.note.toHtmlEscaped());
    m_details->setHtml(h);
}

void MedLogDialog::deleteSelected()
{
    if (!Session::isAdmin()) return;
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    const MedPreparation e = m_log[rows.first().row()];
    if (QMessageBox::question(this, windowTitle(),
            tr("Protokolleintrag vom %1 löschen?").arg(QLocale().toString(e.preparedAt, QStringLiteral("dd.MM.yyyy HH:mm"))))
        != QMessageBox::Yes)
        return;
    QString err;
    if (!Database::instance().deletePreparation(e.id, &err))
        QMessageBox::critical(this, windowTitle(), err);
}
