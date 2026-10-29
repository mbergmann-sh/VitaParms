// SPDX-License-Identifier: MIT
#include "medsearchdialog.h"
#include "medschedule.h"
#include "reportprinter.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QMap>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Col { ColName, ColSubstance, ColCategory, ColDose, ColRhythm, ColUnits, ColActive, ColReason, ColCount };

QString doseScheme(const Medication &m)
{
    return m.dosageScheme() + (m.unit.isEmpty() ? QString() : QLatin1Char(' ') + m.unit);
}
}

MedSearchDialog::MedSearchDialog(const Patient &p, const QList<Medication> &meds, int group, QWidget *parent)
    : QDialog(parent), m_patient(p), m_meds(meds)
{
    setWindowTitle(tr("Medikamente suchen – %1").arg(p.displayName()));
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("&Suchen nach:"), this));
    m_group = new QComboBox(this);
    for (int g = 0; g < DrugClass::GroupCount; ++g) m_group->addItem(DrugClass::groupName(DrugClass::Group(g)), g);
    m_group->addItem(tr("Freitext (Name, Wirkstoff, Grund, Bemerkung)"), -1);
    qobject_cast<QLabel *>(top->itemAt(0)->widget())->setBuddy(m_group);
    top->addWidget(m_group);
    m_text = new QLineEdit(this);
    m_text->setPlaceholderText(tr("Suchbegriff"));
    m_text->setClearButtonEnabled(true);
    top->addWidget(m_text, 1);
    m_archived = new QCheckBox(tr("auch &abgesetzte (archivierte)"), this);
    top->addWidget(m_archived);
    lay->addLayout(top);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    lay->addWidget(m_summary);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({ tr("Präparat"), tr("Wirkstoff"), tr("Art"), tr("Dosierung"),
                                         tr("Einnahme"), tr("Menge"), tr("Wirkstoffmenge"), tr("Zuordnung") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setWordWrap(true);
    lay->addWidget(m_table, 1);

    auto *note = new QLabel(tr("<small>Die Zuordnung erfolgt automatisch anhand von Wirkstoff- und Handelsnamen "
                               "(BtM nach Anlage III BtMG inkl. ausgenommener Zubereitungen) und ist ohne Gewähr. "
                               "<i>ggf.</i> = hängt von Zubereitung oder Stärke ab. Korrigieren lässt sie sich je "
                               "Medikament im Medikamentendialog unter „Kennzeichnung“.</small>"), this);
    note->setWordWrap(true);
    lay->addWidget(note);

    auto *bb = new QDialogButtonBox(this);
    auto *print = bb->addButton(tr("&Drucken…"), QDialogButtonBox::ActionRole);
    auto *pdf = bb->addButton(tr("Als &PDF…"), QDialogButtonBox::ActionRole);
    bb->addButton(tr("&Schließen"), QDialogButtonBox::RejectRole);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(print, &QPushButton::clicked, this, [this] {
        ReportPrinter::htmlReport(this, m_patient, criterionText(), html(), ReportPrinter::Target::Preview);
    });
    connect(pdf, &QPushButton::clicked, this, [this] {
        ReportPrinter::htmlReport(this, m_patient, criterionText(), html(), ReportPrinter::Target::Pdf);
    });
    lay->addWidget(bb);

    connect(m_group, qOverload<int>(&QComboBox::currentIndexChanged), this, &MedSearchDialog::run);
    connect(m_text, &QLineEdit::textChanged, this, &MedSearchDialog::run);
    connect(m_archived, &QCheckBox::toggled, this, &MedSearchDialog::run);
    m_group->setCurrentIndex(std::max(0, m_group->findData(group)));
    run();
    resize(1180, 560);
}

void MedSearchDialog::setGroup(int group)
{
    m_group->setCurrentIndex(std::max(0, m_group->findData(group)));
}

QList<MedSearchDialog::Row> MedSearchDialog::search(const QList<Medication> &meds, int group, const QString &text,
                                                    bool archived)
{
    QList<Row> out;
    const QString needle = text.trimmed().toLower();
    for (const auto &[cat, list] : MedSchedule::grouped(meds)) {
        for (const Medication &m : list) {
            if (m.archived && !archived) continue;
            Row r{ m, {}, DrugClass::dailyAmount(m) };
            if (group >= 0) {
                r.hit = DrugClass::classify(m, DrugClass::Group(group));
                if (!r.hit.match) continue;
            }
            if (!needle.isEmpty()) {
                const QString hay = (m.name + QLatin1Char(' ') + m.substance + QLatin1Char(' ') + m.reason + QLatin1Char(' ')
                                     + m.notes + QLatin1Char(' ') + m.rhythmNote + QLatin1Char(' ') + m.pzn).toLower();
                if (!hay.contains(needle)) continue;
                if (group < 0) r.hit.reason = tr("Treffer für „%1“").arg(text.trimmed());
            } else if (group < 0) {
                continue;   // Freitext ohne Begriff: nichts anzeigen
            }
            out << r;
        }
    }
    return out;
}

QString MedSearchDialog::criterionText() const
{
    const int g = m_group->currentData().toInt();
    QString t = g >= 0 ? DrugClass::groupName(DrugClass::Group(g)) : tr("Suche „%1“").arg(m_text->text().trimmed());
    if (g >= 0 && !m_text->text().trimmed().isEmpty()) t += tr(" – „%1“").arg(m_text->text().trimmed());
    return t;
}

void MedSearchDialog::run()
{
    const int g = m_group->currentData().toInt();
    m_rows = search(m_meds, g, m_text->text(), m_archived->isChecked());
    m_table->setRowCount(int(m_rows.size()));
    int uncertain = 0;
    QMap<QString, int> subclasses;
    for (int r = 0; r < m_rows.size(); ++r) {
        const Row &row = m_rows[r];
        const Medication &m = row.med;
        if (row.hit.uncertain) ++uncertain;
        if (!row.hit.subclass.isEmpty()) subclasses[row.hit.subclass]++;
        QString name = m.name;
        if (!m.strength.isEmpty()) name += QLatin1Char(' ') + m.strength;
        if (!m.form.isEmpty()) name += QStringLiteral(" (") + m.form + QLatin1Char(')');
        if (m.archived) name += tr(" – abgesetzt");
        QString rhythm = m.category == Medication::Regular ? MedSchedule::describe(m) : MedSchedule::categoryName(m.category);
        if (!m.rhythmNote.isEmpty()) rhythm += QStringLiteral("\n") + m.rhythmNote;
        const QString units = row.amount.unitsText.isEmpty() ? QString() : row.amount.unitsText + QStringLiteral("\n") + row.amount.perText;
        const QString active = row.amount.activeText.isEmpty() ? QString() : row.amount.activeText + QStringLiteral("\n") + row.amount.perText;
        const QString reason = (row.hit.uncertain ? tr("ggf. ") : QString()) + row.hit.reason;
        const QStringList cells = { name, m.substance, MedSchedule::categoryName(m.category), doseScheme(m), rhythm,
                                    units, active, reason };
        for (int c = 0; c < ColCount; ++c) {
            auto *it = new QTableWidgetItem(cells[c]);
            if (c == ColReason && row.hit.uncertain) it->setForeground(QColor(0xD5, 0x5E, 0x00));
            if (m.category == Medication::Emergency && c == ColCategory) it->setForeground(QColor(0xD5, 0x2B, 0x1E));
            if (c == ColUnits || c == ColActive) { QFont f = it->font(); f.setBold(true); it->setFont(f); }
            m_table->setItem(r, c, it);
        }
    }
    m_table->resizeColumnsToContents();
    for (int c : { int(ColName), int(ColRhythm), int(ColReason) })
        m_table->setColumnWidth(c, std::min(m_table->columnWidth(c), 300));
    m_table->resizeRowsToContents();

    QString s;
    if (m_rows.isEmpty()) {
        s = g < 0 && m_text->text().trimmed().isEmpty() ? tr("Bitte einen Suchbegriff eingeben.")
                                                       : tr("<b>Keine passenden Medikamente</b> im aktuellen Medikamentenplan.");
    } else {
        s = tr("<b>%1 Medikament(e)</b> gefunden").arg(m_rows.size());
        if (uncertain) s += tr(", davon %1 mit „ggf.“ – bitte prüfen").arg(uncertain);
        s += QLatin1Char('.');
        if (g == DrugClass::Btm)
            s += tr(" BtM werden auf einem BtM-Rezept verordnet. Für Reisen in Schengen-Staaten gibt es eine "
                    "Bescheinigung nach Art. 75 Schengener Durchführungsübereinkommen (vom Arzt auszufüllen).");
        else if (g == DrugClass::Anticoag) {
            s += tr(" Vor Operationen, Zahnbehandlungen und Spritzen den Arzt auf die Blutverdünnung hinweisen; "
                    "einen Ausweis mitführen.");
            if (m_rows.size() > 1)
                s += tr(" <span style=\"color:#D52B1E;\">Mehrere gerinnungshemmende Medikamente – nur als ärztlich "
                        "verordnete Kombination einnehmen (erhöhtes Blutungsrisiko).</span>");
        } else if (g == DrugClass::Antihypertensive) {
            const int raas = subclasses.value(QCoreApplication::translate("DrugClass", "ACE-Hemmer"))
                           + subclasses.value(QCoreApplication::translate("DrugClass", "Sartan (AT1-Blocker)"))
                           + subclasses.value(QCoreApplication::translate("DrugClass", "Renininhibitor"))
                           + subclasses.value(QCoreApplication::translate("DrugClass", "ARNI (Sacubitril/Valsartan)"));
            s += tr(" Blutdruck regelmäßig messen und die Werte zum Arzttermin mitbringen.");
            if (raas > 1)
                s += tr(" <span style=\"color:#D52B1E;\">Mehrere Hemmstoffe des Renin-Angiotensin-Systems (ACE-Hemmer, Sartan, "
                        "Aliskiren, Sacubitril/Valsartan) gleichzeitig – bitte ärztlich abklären.</span>");
        } else if (g == DrugClass::Corticosteroid) {
            if (subclasses.value(QCoreApplication::translate("DrugClass", "systemisch")) > 0)
                s += tr(" Systemisch eingenommenes Kortison nach längerer Anwendung nicht eigenmächtig absetzen – "
                        "Dosisänderungen nur nach ärztlicher Anweisung; bei Langzeittherapie an einen Kortison-Notfallausweis denken.");
        } else if (g == DrugClass::Pain) {
            if (subclasses.value(QCoreApplication::translate("DrugClass", "NSAR"))
                + subclasses.value(QCoreApplication::translate("DrugClass", "Coxib")) > 1)
                s += tr(" <span style=\"color:#D52B1E;\">Mehrere NSAR/Coxibe gleichzeitig – bitte ärztlich abklären "
                        "(Magen-, Nieren- und Blutungsrisiko).</span>");
        }
    }
    m_summary->setText(s);
}

QString MedSearchDialog::html() const
{
    const QLocale loc;
    QString h = QStringLiteral("<p><b>%1</b> &nbsp;·&nbsp; %2 &nbsp;·&nbsp; %3</p>")
                    .arg(criterionText().toHtmlEscaped(),
                         tr("aus dem Medikamentenplan, Stand %1").arg(loc.toString(QDate::currentDate(), QStringLiteral("dd.MM.yyyy"))),
                         tr("%1 Treffer").arg(m_rows.size()));
    h += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\" border=\"1\" "
                        "style=\"border-collapse: collapse; border-color: #8899aa;\"><tr>");
    for (const QString &t : { tr("Präparat"), tr("Wirkstoff"), tr("Art"), tr("Dosierung"), tr("Einnahme"),
                              tr("Menge"), tr("Wirkstoffmenge"), tr("Zuordnung") })
        h += QStringLiteral("<th bgcolor=\"#e8eef4\" align=\"left\">%1</th>").arg(t.toHtmlEscaped());
    h += QStringLiteral("</tr>");
    auto cell = [](const QString &s) {
        return QStringLiteral("<td>%1</td>").arg(s.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    };
    for (const Row &row : m_rows) {
        const Medication &m = row.med;
        QString name = m.name + (m.strength.isEmpty() ? QString() : QLatin1Char(' ') + m.strength)
                     + (m.form.isEmpty() ? QString() : QStringLiteral(" (") + m.form + QLatin1Char(')'));
        if (m.archived) name += tr(" – abgesetzt");
        QString rhythm = m.category == Medication::Regular ? MedSchedule::describe(m) : MedSchedule::categoryName(m.category);
        if (!m.rhythmNote.isEmpty()) rhythm += QStringLiteral("\n") + m.rhythmNote;
        h += QStringLiteral("<tr>") + cell(name) + cell(m.substance) + cell(MedSchedule::categoryName(m.category))
           + cell(doseScheme(m)) + cell(rhythm)
           + cell(row.amount.unitsText.isEmpty() ? QString() : row.amount.unitsText + QStringLiteral("\n") + row.amount.perText)
           + cell(row.amount.activeText.isEmpty() ? QString() : row.amount.activeText + QStringLiteral("\n") + row.amount.perText)
           + cell((row.hit.uncertain ? tr("ggf. ") : QString()) + row.hit.reason) + QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table>");
    h += QStringLiteral("<p>%1</p>").arg(m_summary->text());
    h += QStringLiteral("<p><small>%1</small></p>")
             .arg(tr("Automatische Zuordnung ohne Gewähr; maßgeblich sind Verordnung und Packungsbeilage."));
    return h;
}
