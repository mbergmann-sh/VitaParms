// SPDX-License-Identifier: MIT
#include "protocoldialog.h"
#include "appsettings.h"
#include "database.h"
#include "reportprinter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBrowser>
#include <QVBoxLayout>

ProtocolDialog::ProtocolDialog(Protocols::Kind kind, const Patient &p, QWidget *parent)
    : QDialog(parent), m_kind(kind), m_patient(p)
{
    setObjectName(kind == Protocols::Kind::Stool ? QStringLiteral("stoolProtocolDialog")
                  : kind == Protocols::Kind::Glucose ? QStringLiteral("glucoseProtocolDialog") : QStringLiteral("fluidProtocolDialog"));
    setWindowTitle(QStringLiteral("%1 – %2").arg(Protocols::title(kind), p.displayName()));
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Zeitraum:"), this));
    m_period = new QComboBox(this);
    m_period->setObjectName(QStringLiteral("periodCombo"));
    m_period->addItem(tr("letzte 7 Tage"), 7);
    m_period->addItem(tr("letzte 14 Tage"), 14);
    m_period->addItem(tr("letzte 30 Tage"), 30);
    m_period->addItem(tr("letzte 90 Tage"), 90);
    m_period->addItem(tr("Von – bis"), 0);
    top->addWidget(m_period);
    m_from = new QDateEdit(QDate::currentDate().addDays(-13), this);
    m_to = new QDateEdit(QDate::currentDate(), this);
    for (QDateEdit *e : { m_from, m_to }) {
        e->setCalendarPopup(true);
        e->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
        connect(e, &QDateEdit::dateChanged, this, &ProtocolDialog::reload);
    }
    auto *dash = new QLabel(QStringLiteral("–"), this);
    top->addWidget(m_from);
    top->addWidget(dash);
    top->addWidget(m_to);
    top->addSpacing(16);
    if (kind == Protocols::Kind::Stool) {
        m_emptyDays = new QCheckBox(tr("Tage ohne Stuhlgang aufführen"), this);
        m_emptyDays->setObjectName(QStringLiteral("emptyDaysCheck"));
        m_emptyDays->setChecked(true);
        connect(m_emptyDays, &QCheckBox::toggled, this, &ProtocolDialog::reload);
        top->addWidget(m_emptyDays);
    } else if (kind == Protocols::Kind::Glucose) {
        top->addWidget(new QLabel(tr("Tage mit mindestens"), this));
        m_minValues = new QSpinBox(this);
        m_minValues->setObjectName(QStringLiteral("minValuesSpin"));
        m_minValues->setRange(1, 12);
        m_minValues->setValue(4);
        m_minValues->setSuffix(tr(" Werten"));
        m_minValues->setToolTip(tr("1 = jeder Tag mit einem Blutzuckerwert; ab 4 Werten: Tagesprofile"));
        connect(m_minValues, qOverload<int>(&QSpinBox::valueChanged), this, &ProtocolDialog::reload);
        top->addWidget(m_minValues);
    }
    top->addStretch();
    lay->addLayout(top);
    connect(m_period, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, dash] {
        const bool custom = m_period->currentData().toInt() == 0;
        m_from->setEnabled(custom);
        m_to->setEnabled(custom);
        dash->setEnabled(custom);
        reload();
    });

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    lay->addWidget(m_summary);
    m_view = new QTextBrowser(this);
    m_view->setObjectName(QStringLiteral("protocolView"));
    lay->addWidget(m_view, 1);

    auto *bb = new QDialogButtonBox(this);
    auto *print = bb->addButton(tr("&Drucken…"), QDialogButtonBox::ActionRole);
    auto *preview = bb->addButton(tr("Druck&vorschau…"), QDialogButtonBox::ActionRole);
    auto *pdf = bb->addButton(tr("Als &PDF…"), QDialogButtonBox::ActionRole);
    auto *xlsx = bb->addButton(tr("Nach &Excel…"), QDialogButtonBox::ActionRole);
    xlsx->setObjectName(QStringLiteral("excelButton"));
    bb->addButton(tr("&Schließen"), QDialogButtonBox::RejectRole);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto out = [this](ReportPrinter::Target t) {
        ReportPrinter::htmlReport(this, m_patient, m_table.title, Protocols::toHtml(m_table, Protocols::periodText(options())), t, true);
    };
    connect(print, &QPushButton::clicked, this, [out] { out(ReportPrinter::Target::Printer); });
    connect(preview, &QPushButton::clicked, this, [out] { out(ReportPrinter::Target::Preview); });
    connect(pdf, &QPushButton::clicked, this, [out] { out(ReportPrinter::Target::Pdf); });
    connect(xlsx, &QPushButton::clicked, this, [this] {
        const QString path = ReportPrinter::askExportFile(this, tr("%1 nach Excel exportieren").arg(m_table.title),
                                                          m_kind == Protocols::Kind::Stool ? QStringLiteral("Stuhlprotokoll")
                                                          : m_kind == Protocols::Kind::Glucose ? QStringLiteral("BZ-Tagesprofile")
                                                                                               : QStringLiteral("Bilanz"),
                                                          m_patient, QStringLiteral("xlsx"), tr("Excel-Arbeitsmappe (*.xlsx)"));
        if (path.isEmpty()) return;
        QString err;
        if (!Protocols::toXlsx(m_table, m_patient, Protocols::periodText(options()), path, &err))
            QMessageBox::critical(this, windowTitle(), tr("Export fehlgeschlagen:\n%1").arg(err));
    });
    lay->addWidget(bb);

    connect(&Database::instance(), &Database::vitalsChanged, this, [this](qint64 pid) { if (pid == m_patient.id) reload(); });
    m_period->setCurrentIndex(kind == Protocols::Kind::Glucose ? 2 : kind == Protocols::Kind::Stool ? 1 : 0);
    m_from->setEnabled(false);
    m_to->setEnabled(false);
    dash->setEnabled(false);
    reload();
    const QByteArray geo = AppSettings::windowState(QStringLiteral("protocoldialog"));
    if (!geo.isEmpty()) restoreGeometry(geo);
    else resize(1040, 680);
    connect(this, &QDialog::finished, this, [this] { AppSettings::setWindowState(QStringLiteral("protocoldialog"), saveGeometry()); });
}

void ProtocolDialog::setPeriodDays(int days)
{
    const int i = m_period->findData(days);
    if (i >= 0) m_period->setCurrentIndex(i);
}

Protocols::Options ProtocolDialog::options() const
{
    Protocols::Options o;
    const int days = m_period->currentData().toInt();
    if (days > 0) {
        o.to = QDate::currentDate();
        o.from = o.to.addDays(1 - days);
    } else {
        o.from = std::min(m_from->date(), m_to->date());
        o.to = std::max(m_from->date(), m_to->date());
    }
    if (m_emptyDays) o.showEmptyDays = m_emptyDays->isChecked();
    if (m_minValues) o.minValues = m_minValues->value();
    return o;
}

void ProtocolDialog::reload()
{
    const Protocols::Options o = options();
    // 31 Tage Vorlauf: Tage ohne Stuhlgang, die vor dem Zeitraum begonnen haben, zählen weiter
    const QList<VitalRecord> recs = Database::instance().vitals(m_patient.id, QDateTime(o.from.addDays(-31), QTime(0, 0)),
                                                                QDateTime(o.to.addDays(1), QTime(0, 0)));
    m_table = Protocols::build(m_kind, recs, o, m_patient);
    m_summary->setText(m_table.summary);
    m_view->setHtml(Protocols::toHtml(m_table, Protocols::periodText(o)));
}
