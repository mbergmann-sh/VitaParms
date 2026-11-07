// SPDX-License-Identifier: MIT
#include "apptconflictdialog.h"
#include "appointmentdialog.h"
#include "database.h"
#include "reportprinter.h"
#include "session.h"

#include <QCheckBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeEdit>
#include <QVBoxLayout>

ApptConflictDialog::ApptConflictDialog(qint64 currentPatient, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Terminkollisionen prüfen"));
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Vergleicht die Termine der gewählten Personen und meldet Termine, die sich überschneiden "
                                "– mit Vorschlägen für freie Ausweichtermine."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto *top = new QHBoxLayout;
    auto *pg = new QGroupBox(tr("Personen (mindestens zwei)"), this);
    auto *pl = new QVBoxLayout(pg);
    m_persons = new QListWidget(pg);
    m_persons->setObjectName(QStringLiteral("conflictPersons"));
    const QSettings st;
    const QStringList remembered = st.value(QStringLiteral("conflicts/persons")).toStringList();
    for (const Patient &p : Database::instance().patients()) {
        auto *it = new QListWidgetItem(p.displayName(), m_persons);
        it->setData(Qt::UserRole, p.id);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        const bool on = remembered.isEmpty() ? true : (remembered.contains(QString::number(p.id)) || p.id == currentPatient);
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    m_persons->setMinimumHeight(110);
    pl->addWidget(m_persons);
    auto *allNone = new QHBoxLayout;
    auto *all = new QPushButton(tr("Alle"), pg);
    auto *none = new QPushButton(tr("Keine"), pg);
    for (QPushButton *b : { all, none }) b->setAutoDefault(false);
    allNone->addWidget(all);
    allNone->addWidget(none);
    allNone->addStretch();
    pl->addLayout(allNone);
    connect(all, &QPushButton::clicked, this, [this] { for (int i = 0; i < m_persons->count(); ++i) m_persons->item(i)->setCheckState(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [this] { for (int i = 0; i < m_persons->count(); ++i) m_persons->item(i)->setCheckState(Qt::Unchecked); });
    top->addWidget(pg, 1);

    auto *og = new QGroupBox(tr("Zeitraum und Vorschläge"), this);
    auto *form = new QFormLayout(og);
    auto *range = new QHBoxLayout;
    m_from = new QDateEdit(QDate::currentDate(), og);
    m_to = new QDateEdit(QDate::currentDate().addDays(st.value(QStringLiteral("conflicts/days"), 60).toInt()), og);
    m_from->setObjectName(QStringLiteral("conflictFrom"));
    m_to->setObjectName(QStringLiteral("conflictTo"));
    for (QDateEdit *d : { m_from, m_to }) { d->setCalendarPopup(true); d->setDisplayFormat(QStringLiteral("dd.MM.yyyy")); }
    range->addWidget(m_from);
    range->addWidget(new QLabel(tr("bis"), og));
    range->addWidget(m_to);
    range->addStretch();
    form->addRow(tr("Zeitraum:"), range);
    m_buffer = new QSpinBox(og);
    m_buffer->setObjectName(QStringLiteral("conflictBuffer"));
    m_buffer->setRange(0, 240);
    m_buffer->setSingleStep(5);
    m_buffer->setSuffix(tr(" min"));
    m_buffer->setValue(st.value(QStringLiteral("conflicts/buffer"), 0).toInt());
    m_buffer->setToolTip(tr("Zeit, die zwischen zwei Terminen mindestens frei bleiben soll – z. B. für die Fahrt"));
    form->addRow(tr("&Mindestabstand:"), m_buffer);
    auto *hours = new QHBoxLayout;
    m_dayStart = new QTimeEdit(st.value(QStringLiteral("conflicts/dayStart"), QTime(8, 0)).toTime(), og);
    m_dayEnd = new QTimeEdit(st.value(QStringLiteral("conflicts/dayEnd"), QTime(18, 0)).toTime(), og);
    for (QTimeEdit *t : { m_dayStart, m_dayEnd }) t->setDisplayFormat(QStringLiteral("HH:mm"));
    hours->addWidget(m_dayStart);
    hours->addWidget(new QLabel(tr("bis"), og));
    hours->addWidget(m_dayEnd);
    hours->addStretch();
    form->addRow(tr("Vorschläge zwischen:"), hours);
    m_weekdays = new QCheckBox(tr("nur Montag bis Freitag"), og);
    m_weekdays->setChecked(st.value(QStringLiteral("conflicts/weekdays"), true).toBool());
    form->addRow(QString(), m_weekdays);
    auto *checkBtn = new QPushButton(tr("&Prüfen"), og);
    checkBtn->setObjectName(QStringLiteral("conflictCheckButton"));
    checkBtn->setDefault(true);
    form->addRow(QString(), checkBtn);
    top->addWidget(og, 1);
    lay->addLayout(top);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("conflictSummary"));
    m_summary->setWordWrap(true);
    lay->addWidget(m_summary);
    m_table = new QTableWidget(0, 4, this);
    m_table->setObjectName(QStringLiteral("conflictTable"));
    m_table->setHorizontalHeaderLabels({ tr("Termin 1"), tr("Termin 2"), tr("Kollision"), tr("Ausweichtermine") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setWordWrap(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setToolTip(tr("Doppelklick öffnet den Termin, der verschoben werden soll"));
    lay->addWidget(m_table, 1);

    auto *bb = new QDialogButtonBox(this);
    m_print = bb->addButton(tr("&Drucken…"), QDialogButtonBox::ActionRole);
    m_pdf = bb->addButton(tr("Als P&DF…"), QDialogButtonBox::ActionRole);
    m_print->setObjectName(QStringLiteral("conflictPrintButton"));
    auto *close = bb->addButton(QDialogButtonBox::Close);
    close->setText(tr("&Schließen"));
    lay->addWidget(bb);

    connect(checkBtn, &QPushButton::clicked, this, &ApptConflictDialog::runCheck);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_print, &QPushButton::clicked, this, [this] {
        ReportPrinter::appointmentConflicts(this, m_lastOptions, m_result, ReportPrinter::Target::Preview);
    });
    connect(m_pdf, &QPushButton::clicked, this, [this] {
        ReportPrinter::appointmentConflicts(this, m_lastOptions, m_result, ReportPrinter::Target::Pdf);
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { editMoved(row); });
    updatePrintButtons();
    resize(980, 640);
}

ApptConflicts::Options ApptConflictDialog::options() const
{
    ApptConflicts::Options o;
    for (int i = 0; i < m_persons->count(); ++i)
        if (m_persons->item(i)->checkState() == Qt::Checked) o.patients << m_persons->item(i)->data(Qt::UserRole).toLongLong();
    o.from = m_from->date();
    o.to = m_to->date();
    o.bufferMinutes = m_buffer->value();
    o.dayStart = m_dayStart->time();
    o.dayEnd = m_dayEnd->time();
    o.weekdaysOnly = m_weekdays->isChecked();
    o.owner = Session::isAdmin() ? QString() : Session::user();
    return o;
}

void ApptConflictDialog::runCheck()
{
    m_lastOptions = options();
    {   // Auswahl merken
        QSettings st;
        QStringList ids;
        for (qint64 id : m_lastOptions.patients) ids << QString::number(id);
        st.setValue(QStringLiteral("conflicts/persons"), ids);
        st.setValue(QStringLiteral("conflicts/days"), int(m_lastOptions.from.daysTo(m_lastOptions.to)));
        st.setValue(QStringLiteral("conflicts/buffer"), m_lastOptions.bufferMinutes);
        st.setValue(QStringLiteral("conflicts/dayStart"), m_lastOptions.dayStart);
        st.setValue(QStringLiteral("conflicts/dayEnd"), m_lastOptions.dayEnd);
        st.setValue(QStringLiteral("conflicts/weekdays"), m_lastOptions.weekdaysOnly);
    }
    m_result = ApptConflicts::check(m_lastOptions);
    m_checked = m_result.error.isEmpty();
    m_table->setRowCount(0);
    if (!m_result.error.isEmpty()) {
        m_summary->setText(QStringLiteral("<span style=\"color:#B00020;\">%1</span>").arg(m_result.error.toHtmlEscaped()));
        updatePrintButtons();
        return;
    }
    m_summary->setText(m_result.conflicts.isEmpty()
        ? tr("<b>Keine Überschneidungen</b> – %1 Termine geprüft.").arg(m_result.checked)
        : tr("<b>%1 Kollision(en)</b> – %2 Termine geprüft. Doppelklick auf eine Zeile öffnet den Termin, der verschoben werden soll.")
              .arg(m_result.conflicts.size()).arg(m_result.checked));
    m_table->setRowCount(int(m_result.conflicts.size()));
    for (int i = 0; i < m_result.conflicts.size(); ++i) {
        const ApptConflicts::Conflict &c = m_result.conflicts[i];
        auto apptText = [](const Appointment &a, const QString &who, bool move) {
            QString t = ApptConflicts::when(a) + QLatin1Char('\n') + a.title + QLatin1Char('\n') + who;
            if (move) t += QLatin1Char('\n') + tr("→ verschieben");
            return t;
        };
        m_table->setItem(i, 0, new QTableWidgetItem(apptText(c.a, c.personA, !c.moveB)));
        m_table->setItem(i, 1, new QTableWidgetItem(apptText(c.b, c.personB, c.moveB)));
        auto *ov = new QTableWidgetItem(ApptConflicts::overlapText(c, m_lastOptions.bufferMinutes));
        if (c.overlapMinutes > 0) ov->setForeground(QColor(0xB0, 0x00, 0x20));
        m_table->setItem(i, 2, ov);
        m_table->setItem(i, 3, new QTableWidgetItem(tr("für %1:").arg(ApptConflicts::movedTitle(c)) + QLatin1Char('\n')
                                                    + ApptConflicts::alternativesText(c)));
    }
    m_table->resizeRowsToContents();
    updatePrintButtons();
}

void ApptConflictDialog::editMoved(int row)
{
    if (row < 0 || row >= m_result.conflicts.size()) return;
    const ApptConflicts::Conflict &c = m_result.conflicts[row];
    const auto a = Database::instance().appointment(c.moveB ? c.b.id : c.a.id);
    if (!a) return;
    AppointmentDialog dlg(*a, this);
    if (dlg.exec() == QDialog::Accepted) runCheck();   // nach dem Verschieben neu prüfen
}

void ApptConflictDialog::updatePrintButtons()
{
    m_print->setEnabled(m_checked);
    m_pdf->setEnabled(m_checked);
}
