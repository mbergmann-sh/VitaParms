// SPDX-License-Identifier: MIT
#include "appointmentspage.h"
#include "appointmentdialog.h"
#include "quarterreminder.h"
#include "database.h"
#include "icalexport.h"
#include "session.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QTextCharFormat>
#include <QVBoxLayout>

namespace {
enum View { ViewDay, ViewWeek, ViewMonth, ViewUpcoming, ViewAll };

QIcon dotIcon(const QColor &c)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(c);
    p.setPen(Qt::NoPen);
    p.drawEllipse(1, 1, 10, 10);
    return QIcon(pm);
}
} // namespace

// ================================================================ Modell

void AppointmentModel::setAppointments(const QList<Appointment> &l)
{
    beginResetModel();
    m_items = l;
    m_patientNames.clear();
    for (const Patient &p : Database::instance().patients()) m_patientNames.insert(p.id, p.displayName());
    m_seriesText.clear();
    for (const AppointmentSeries &se : Database::instance().allSeries())
        m_seriesText.insert(se.id, se.rule.describe(se.dtstart.date()));
    endResetModel();
}

const Appointment *AppointmentModel::at(int row) const
{
    return row >= 0 && row < m_items.size() ? &m_items[row] : nullptr;
}

QVariant AppointmentModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColWhen:     return tr("Wann");
    case ColPriority: return tr("Priorität");
    case ColTitle:    return tr("Termin");
    case ColPatient:  return tr("Person");
    case ColLocation: return tr("Ort");
    case ColOwner:    return tr("Zuständig");
    case ColReminder: return tr("Erinnerung");
    }
    return {};
}

QVariant AppointmentModel::data(const QModelIndex &index, int role) const
{
    const Appointment *a = at(index.row());
    if (!a) return {};
    const QLocale loc;
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColWhen: {
            if (a->allDay) return loc.toString(a->start.date(), QStringLiteral("ddd dd.MM.yyyy")) + tr(", ganztägig");
            QString s = loc.toString(a->start, QStringLiteral("ddd dd.MM.yyyy HH:mm"));
            if (a->end.isValid() && a->end > a->start)
                s += QStringLiteral("–") + (a->end.date() == a->start.date()
                        ? loc.toString(a->end.time(), QStringLiteral("HH:mm"))
                        : loc.toString(a->end, QStringLiteral("dd.MM. HH:mm")));
            return s;
        }
        case ColPriority: return ApptUi::priorityName(a->priority);
        case ColTitle:    return a->isSeries() ? QStringLiteral("↻ ") + a->title : a->title;
        case ColPatient:  return a->patientId ? m_patientNames.value(a->patientId) : QString();
        case ColLocation: return a->location;
        case ColOwner:    return a->owner;
        case ColReminder: {
            if (!a->hasReminder()) return QString();
            QStringList ch;
            if (a->remindVisual) ch << tr("optisch");
            if (a->remindSound) ch << tr("akustisch");
            if (a->remindEmail) ch << tr("E-Mail");
            QString s = a->snoozeUntil.isValid()
                ? tr("später: %1").arg(loc.toString(a->snoozeUntil, QStringLiteral("HH:mm")))
                : ApptUi::leadText(a->remindMinutes);
            s += QStringLiteral(" (") + ch.join(QStringLiteral(", ")) + QLatin1Char(')');
            if (a->remindedAt.isValid()) s += tr(" ✓");
            return s;
        }
        }
    }
    if (role == Qt::DecorationRole && index.column() == ColPriority)
        return dotIcon(ApptUi::priorityColor(a->priority));
    if (role == Qt::FontRole) {
        QFont f;
        if (a->done) f.setStrikeOut(true);
        if (a->priority == Appointment::Urgent && !a->done) f.setBold(true);
        return f;
    }
    if (role == Qt::ForegroundRole && (a->done || (a->end.isValid() ? a->end : a->start) < QDateTime::currentDateTime()))
        return QBrush(QColor(0x88, 0x88, 0x88));
    if (role == Qt::ToolTipRole) {
        QStringList t;
        if (a->isSeries()) {
            t << tr("Serie: %1").arg(m_seriesText.value(a->seriesId));
            if (a->detached) t << tr("(dieser Termin wurde einzeln geändert)");
        }
        if (!a->notes.isEmpty()) t << a->notes;
        if (!t.isEmpty()) return t.join(QLatin1Char('\n'));
    }
    return {};
}

// ================================================================ Seite

AppointmentsPage::AppointmentsPage(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Ansicht:"), this));
    m_view = new QComboBox(this);
    m_view->addItems({ tr("Tag"), tr("Woche"), tr("Monat"), tr("Alle kommenden"), tr("Alle") });
    m_view->setCurrentIndex(ViewUpcoming);
    top->addWidget(m_view);
    top->addWidget(new QLabel(tr("Person:"), this));
    m_patient = new QComboBox(this);
    top->addWidget(m_patient);
    m_owner = new QComboBox(this);
    m_owner->setToolTip(tr("Administratoren sehen die Termine aller Anmeldekonten"));
    top->addWidget(m_owner);
    m_showDone = new QCheckBox(tr("Erledigte zeigen"), this);
    top->addWidget(m_showDone);
    top->addStretch();
    m_info = new QLabel(this);
    top->addWidget(m_info);
    v->addLayout(top);

    // Quartalswechsel: Hinweis mit „Erledigt“ (zwei Tage vor bis zwei Tage nach dem Wechsel)
    m_quarterBar = new QFrame(this);
    m_quarterBar->setObjectName(QStringLiteral("quarterBar"));
    m_quarterBar->setFrameShape(QFrame::StyledPanel);
    m_quarterBar->setStyleSheet(QStringLiteral("#quarterBar { border-left: 6px solid %1; }")
                                    .arg(ApptUi::priorityColor(Appointment::High).name()));
    auto *qb = new QHBoxLayout(m_quarterBar);
    m_quarterText = new QLabel(m_quarterBar);
    m_quarterText->setWordWrap(true);
    auto *qDone = new QPushButton(tr("Erledigt"), m_quarterBar);
    qDone->setObjectName(QStringLiteral("quarterBarDone"));
    qb->addWidget(m_quarterText, 1);
    qb->addWidget(qDone);
    connect(qDone, &QPushButton::clicked, this, [this] {
        const QDate q = QuarterReminder::relevantChange(QDate::currentDate());
        if (q.isValid()) QuarterReminder::confirm(q);
        updateQuarterBar();
    });
    v->addWidget(m_quarterBar);
    m_quarterBar->hide();

    auto *split = new QSplitter(Qt::Horizontal, this);
    m_cal = new QCalendarWidget(split);
    m_cal->setGridVisible(true);
    m_cal->setVerticalHeaderFormat(QCalendarWidget::ISOWeekNumbers);
    m_cal->setFirstDayOfWeek(Qt::Monday);
    m_model = new AppointmentModel(this);
    m_table = new QTableView(split);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 8);
    m_table->horizontalHeader()->setStretchLastSection(true);
    // Kalender links oben, darunter die Prioritäten-Legende
    auto *left = new QWidget(split);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    m_cal->setParent(left);
    m_cal->setMaximumHeight(std::max(260, m_cal->sizeHint().height()));
    ll->addWidget(m_cal);
    QString legend = tr("<b>Markierung im Kalender:</b>");
    for (int pr = Appointment::Low; pr <= Appointment::Urgent; ++pr)
        legend += QStringLiteral("<br><span style=\"color:%1;\">●</span> %2")
                      .arg(ApptUi::priorityColor(pr).name(), ApptUi::priorityName(pr));
    legend += tr("<br><br>Doppelklick auf einen Tag legt dort einen neuen Termin an.");
    auto *leg = new QLabel(legend, left);
    leg->setWordWrap(true);
    leg->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    ll->addWidget(leg);
    ll->addStretch();
    split->addWidget(left);
    split->addWidget(m_table);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 1);

    auto *row = new QHBoxLayout;
    auto button = [this, row](const QString &t, void (AppointmentsPage::*slot)()) {
        auto *b = new QPushButton(t, this);
        connect(b, &QPushButton::clicked, this, slot);
        row->addWidget(b);
        return b;
    };
    button(tr("&Neuer Termin…"), &AppointmentsPage::newAppointment);
    m_needSel << button(tr("&Bearbeiten…"), &AppointmentsPage::editAppointment);
    m_needSel << button(tr("&Löschen…"), &AppointmentsPage::deleteAppointment);
    m_needSel << button(tr("E&rledigt / offen"), &AppointmentsPage::toggleDone);
    row->addStretch();
    button(tr("iCal-E&xport…"), &AppointmentsPage::exportICal)
        ->setToolTip(tr("Termine als .ics-Datei für Kalender-Programme und Smartphones exportieren"));
    v->addLayout(row);

    connect(m_view, &QComboBox::currentIndexChanged, this, &AppointmentsPage::reload);
    connect(m_patient, &QComboBox::activated, this, &AppointmentsPage::reload);
    connect(m_owner, &QComboBox::activated, this, &AppointmentsPage::reload);
    connect(m_showDone, &QCheckBox::toggled, this, &AppointmentsPage::reload);
    connect(m_cal, &QCalendarWidget::selectionChanged, this, [this] {
        if (m_view->currentIndex() == ViewUpcoming || m_view->currentIndex() == ViewAll)
            m_view->setCurrentIndex(ViewDay);   // Klick in den Kalender zeigt den Tag
        else
            reload();
    });
    connect(m_cal, &QCalendarWidget::activated, this, [this] { newAppointment(); });
    connect(m_cal, &QCalendarWidget::currentPageChanged, this, [this] { markCalendar(); });
    connect(m_table, &QTableView::doubleClicked, this, &AppointmentsPage::editAppointment);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, &AppointmentsPage::updateButtons);
    connect(&Database::instance(), &Database::appointmentsChanged, this, &AppointmentsPage::reload);
    connect(&Database::instance(), &Database::patientsChanged, this, &AppointmentsPage::reload);
    connect(&Database::instance(), &Database::databaseReopened, this, &AppointmentsPage::reload);
}

void AppointmentsPage::rangeFor(QDateTime &from, QDateTime &to) const
{
    const QDate d = m_cal->selectedDate();
    from = to = {};
    switch (m_view->currentIndex()) {
    case ViewDay:
        from = QDateTime(d, QTime(0, 0));
        to = QDateTime(d.addDays(1), QTime(0, 0));
        break;
    case ViewWeek: {
        const QDate mon = d.addDays(1 - d.dayOfWeek());
        from = QDateTime(mon, QTime(0, 0));
        to = QDateTime(mon.addDays(7), QTime(0, 0));
        break;
    }
    case ViewMonth: {
        const QDate first(d.year(), d.month(), 1);
        from = QDateTime(first, QTime(0, 0));
        to = QDateTime(first.addMonths(1), QTime(0, 0));
        break;
    }
    case ViewUpcoming:
        from = QDateTime(QDate::currentDate(), QTime(0, 0));
        break;
    default:
        break;
    }
}

void AppointmentsPage::updateQuarterBar()
{
    const QDate q = QuarterReminder::relevantChange(QDate::currentDate());
    const bool show = Session::isLoggedIn() && Database::instance().isOpen() && QuarterReminder::enabled()
                      && q.isValid() && !QuarterReminder::confirmed(q);
    if (show)
        m_quarterText->setText(QStringLiteral("<b>%1</b> – %2").arg(QuarterReminder::title(q).toHtmlEscaped(),
                               QuarterReminder::message(q, QDate::currentDate()).toHtmlEscaped()));
    m_quarterBar->setVisible(show);
}

void AppointmentsPage::reload()
{
    updateQuarterBar();
    if (!Database::instance().isOpen() || !Session::isLoggedIn()) {
        m_model->setAppointments({});
        return;
    }
    // Serien fortschreiben; bei neuen Vorkommen kommt reload() über das Signal erneut
    if (Database::instance().extendSeries() > 0) return;
    // Personenfilter
    const qint64 selPatient = m_patient->count() ? m_patient->currentData().toLongLong() : -1;
    m_patient->clear();
    m_patient->addItem(tr("Alle Personen"), qint64(0));
    for (const Patient &p : Database::instance().patients()) m_patient->addItem(p.displayName(), p.id);
    m_patient->setCurrentIndex(std::max(0, m_patient->findData(selPatient < 0 ? qint64(0) : selPatient)));

    // Kontenfilter (nur Administratoren)
    const QString selOwner = m_owner->currentData().toString();
    m_owner->clear();
    if (Session::isAdmin()) {
        m_owner->addItem(tr("Alle Konten"), QString());
        for (const QString &n : ApptUi::accountNames()) m_owner->addItem(tr("Konto: %1").arg(n), n);
        m_owner->setCurrentIndex(std::max(0, m_owner->findData(selOwner)));
    }
    m_owner->setVisible(Session::isAdmin());

    QDateTime from, to;
    rangeFor(from, to);
    const QString owner = Session::isAdmin() ? m_owner->currentData().toString() : Session::user();
    const auto list = Database::instance().appointments(from, to, owner, m_patient->currentData().toLongLong(),
                                                        m_showDone->isChecked());
    m_model->setAppointments(list);
    m_table->resizeColumnsToContents();
    m_table->setColumnWidth(AppointmentModel::ColTitle, std::min(m_table->columnWidth(AppointmentModel::ColTitle), 280));
    m_info->setText(tr("%1 Termin(e)").arg(list.size()));
    markCalendar();
    updateButtons();
}

void AppointmentsPage::markCalendar()
{
    for (const QDate &d : m_marked) m_cal->setDateTextFormat(d, QTextCharFormat());
    m_marked.clear();
    const QDate shown(m_cal->yearShown(), m_cal->monthShown(), 1);
    const QString owner = Session::isAdmin() ? m_owner->currentData().toString() : Session::user();
    const auto list = Database::instance().appointments(QDateTime(shown.addDays(-7), QTime(0, 0)),
                                                        QDateTime(shown.addMonths(1).addDays(7), QTime(0, 0)),
                                                        owner, m_patient->currentData().toLongLong(), false);
    QHash<QDate, int> maxPrio;
    for (const Appointment &a : list) {
        const QDate d = a.start.date();
        maxPrio[d] = std::max(maxPrio.value(d, -1), a.priority);
    }
    for (auto it = maxPrio.begin(); it != maxPrio.end(); ++it) {
        QTextCharFormat f;
        f.setFontWeight(QFont::Bold);
        f.setForeground(ApptUi::priorityColor(it.value()));
        if (it.value() == Appointment::Urgent) f.setFontUnderline(true);
        m_cal->setDateTextFormat(it.key(), f);
        m_marked << it.key();
    }
}

const Appointment *AppointmentsPage::selected() const
{
    const auto rows = m_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? nullptr : m_model->at(rows.first().row());
}

void AppointmentsPage::updateButtons()
{
    const bool sel = selected() != nullptr;
    for (QWidget *w : m_needSel) w->setEnabled(sel);
}

void AppointmentsPage::newAppointment()
{
    Appointment a;
    const QDate d = m_cal->selectedDate();
    QTime t = QTime::currentTime();
    t = QTime(std::min(t.hour() + 1, 23), 0);
    a.start = QDateTime(d, d == QDate::currentDate() ? t : QTime(9, 0));
    a.end = a.start.addSecs(3600);
    a.owner = Session::user();
    a.patientId = m_patient->currentData().toLongLong() ? m_patient->currentData().toLongLong() : m_currentPatient;
    a.remindMinutes = 60;
    AppointmentDialog dlg(a, this);
    if (dlg.exec() == QDialog::Accepted) emit changed();
}

void AppointmentsPage::editAppointment()
{
    if (const Appointment *a = selected()) editAppointmentId(a->id);
}

void AppointmentsPage::editAppointmentId(qint64 id)
{
    const auto a = Database::instance().appointment(id);
    if (!a || !ApptUi::canSee(*a)) return;
    AppointmentDialog dlg(*a, this);
    if (dlg.exec() == QDialog::Accepted) emit changed();
}

void AppointmentsPage::deleteAppointment()
{
    const Appointment *a = selected();
    if (!a) return;
    const Appointment copy = *a;   // die Liste wird beim Löschen neu geladen
    if (ApptUi::deleteWithPrompt(this, copy)) emit changed();
}

void AppointmentsPage::toggleDone()
{
    if (const Appointment *a = selected()) {
        Database::instance().setAppointmentDone(a->id, !a->done);
        emit changed();
    }
}

void AppointmentsPage::exportICal()
{
    QList<ICalExportDialog::Scope> scopes;
    QList<Appointment> shown;
    for (int r = 0; r < m_model->rowCount(); ++r) shown << *m_model->at(r);
    QList<Appointment> sel;
    if (const Appointment *a = selected()) sel << *a;
    const QString owner = Session::isAdmin() ? m_owner->currentData().toString() : Session::user();
    const qint64 pid = m_patient->currentData().toLongLong();
    Database &db = Database::instance();
    scopes << ICalExportDialog::Scope{ tr("Ausgewählter Termin bzw. seine Serie"), sel }
           << ICalExportDialog::Scope{ tr("Termine der aktuellen Ansicht"), shown }
           << ICalExportDialog::Scope{ tr("Alle kommenden Termine (Filter Person/Konto)"),
                                      db.appointments(QDateTime(QDate::currentDate(), QTime(0, 0)), {}, owner, pid, true) }
           << ICalExportDialog::Scope{ tr("Alle Termine (Filter Person/Konto)"), db.appointments({}, {}, owner, pid, true) };
    ICalExportDialog d(scopes, sel.isEmpty() ? 1 : 0, this);
    d.exec();
}
