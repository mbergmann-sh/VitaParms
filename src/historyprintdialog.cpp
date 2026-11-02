// SPDX-License-Identifier: MIT
#include "historyprintdialog.h"
#include "database.h"
#include "reportprinter.h"

#include <QCheckBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
const char *kKey = "historyPrint/groups";
}

HistoryPrintDialog::HistoryPrintDialog(const Patient &p, const QDate &from, const QDate &to, const QStringList &preselect,
                                       const QString &okText, QWidget *parent)
    : QDialog(parent), m_patient(p)
{
    setObjectName(QStringLiteral("HistoryPrintDialog"));
    setWindowTitle(tr("Verlauf drucken – %1").arg(p.displayName()));
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Der Ausdruck zeigt jede gewählte Wertegruppe für sich – chronologisch, mit Mittelwert, "
                                "Minimum und Maximum. Blutdruck & Puls enthält RR, Puls und Blutdruckamplitude."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto *period = new QGroupBox(tr("Zeitraum"), this);
    auto *pl = new QHBoxLayout(period);
    m_from = new QDateEdit(from, period);
    m_to = new QDateEdit(to, period);
    for (QDateEdit *e : { m_from, m_to }) {
        e->setCalendarPopup(true);
        e->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    }
    m_from->setObjectName(QStringLiteral("historyFrom"));
    m_to->setObjectName(QStringLiteral("historyTo"));
    pl->addWidget(new QLabel(tr("&Von:"), period));
    static_cast<QLabel *>(pl->itemAt(0)->widget())->setBuddy(m_from);
    pl->addWidget(m_from);
    auto *bis = new QLabel(tr("&bis:"), period);
    bis->setBuddy(m_to);
    pl->addWidget(bis);
    pl->addWidget(m_to);
    pl->addStretch();
    lay->addWidget(period);

    auto *values = new QGroupBox(tr("Werte"), this);
    auto *grid = new QGridLayout(values);
    const QList<ReportPrinter::HistoryGroup> gs = ReportPrinter::historyGroups();
    const QStringList saved = QSettings().value(QLatin1String(kKey)).toStringList();
    int i = 0;
    for (const auto &g : gs) {
        auto *cb = new QCheckBox(QString(g.title).replace(QLatin1Char('&'), QStringLiteral("&&")), values);   // „&“ ist kein Tastenkürzel
        cb->setObjectName(QStringLiteral("historyGroup_") + g.id);
        cb->setChecked(preselect.isEmpty() ? (saved.isEmpty() || saved.contains(g.id)) : preselect.contains(g.id));
        grid->addWidget(cb, i / 2, i % 2);
        m_checks.insert(g.id, cb);
        m_order << g.id;
        ++i;
    }
    auto *row = new QHBoxLayout;
    auto *all = new QPushButton(tr("&Alle"), values);
    auto *none = new QPushButton(tr("&Keine"), values);
    connect(all, &QPushButton::clicked, this, [this] { for (QCheckBox *c : std::as_const(m_checks)) if (c->isEnabled()) c->setChecked(true); });
    connect(none, &QPushButton::clicked, this, [this] { for (QCheckBox *c : std::as_const(m_checks)) c->setChecked(false); });
    row->addWidget(all);
    row->addWidget(none);
    row->addStretch();
    grid->addLayout(row, (i + 1) / 2, 0, 1, 2);
    lay->addWidget(values);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    lay->addWidget(m_info);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(okText);
    connect(bb, &QDialogButtonBox::accepted, this, &HistoryPrintDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    connect(m_from, &QDateEdit::dateChanged, this, &HistoryPrintDialog::updateGroups);
    connect(m_to, &QDateEdit::dateChanged, this, &HistoryPrintDialog::updateGroups);
    updateGroups();
}

QDate HistoryPrintDialog::from() const { return m_from->date(); }
QDate HistoryPrintDialog::to() const { return m_to->date(); }

QStringList HistoryPrintDialog::groups() const
{
    QStringList l;
    for (const QString &id : m_order)
        if (m_checks.value(id)->isChecked() && m_checks.value(id)->isEnabled()) l << id;
    return l;
}

QList<VitalRecord> HistoryPrintDialog::records() const
{
    return Database::instance().vitals(m_patient.id, QDateTime(from(), QTime(0, 0)), QDateTime(to().addDays(1), QTime(0, 0)));
}

QString HistoryPrintDialog::periodText() const
{
    const QLocale loc;
    return QStringLiteral("%1 – %2").arg(loc.toString(from(), QStringLiteral("dd.MM.yyyy")), loc.toString(to(), QStringLiteral("dd.MM.yyyy")));
}

// Gruppen ohne Werte im Zeitraum ausgrauen
void HistoryPrintDialog::updateGroups()
{
    if (m_to->date() < m_from->date()) m_to->setDate(m_from->date());
    const QList<VitalRecord> recs = records();
    const QStringList has = ReportPrinter::historyGroupsWithData(recs);
    for (const QString &id : m_order) {
        QCheckBox *cb = m_checks.value(id);
        cb->setEnabled(has.contains(id));
        cb->setToolTip(has.contains(id) ? QString() : tr("Keine Werte im gewählten Zeitraum"));
    }
    m_info->setText(tr("Messungen im Zeitraum: %1").arg(recs.size()));
}

void HistoryPrintDialog::accept()
{
    if (groups().isEmpty()) {
        m_info->setText(QStringLiteral("<b>%1</b>").arg(tr("Bitte mindestens eine Wertegruppe mit Werten wählen.")));
        return;
    }
    QSettings().setValue(QLatin1String(kKey), groups());
    QDialog::accept();
}
