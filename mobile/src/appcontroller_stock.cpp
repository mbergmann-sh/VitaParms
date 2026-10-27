// VitaParms Tablet – Medikamente stellen (Vorlese-Assistent), Bestand und Bestellungen
// (Teil von AppController). Fachlogik wie am Computer: MedPrep (Auswahl, Vorlesetexte,
// Protokoll, Abbuchung) und MedStock (Packungen, Reichweite, Bestelllisten).
// SPDX-License-Identifier: MIT
#include "appcontroller.h"

#include "appsettings.h"
#include "database.h"
#include "medprep.h"
#include "medschedule.h"
#include "medstock.h"
#include "reportprinter.h"
#include "session.h"
#include "sounds.h"
#include "speech.h"

#include <QJsonObject>
#include <QLocale>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {
QString isoDate(const QDate &d) { return d.isValid() ? d.toString(Qt::ISODate) : QString(); }
QDate dateFrom(const QVariant &v) { return QDate::fromString(v.toString().left(10), Qt::ISODate); }
QString dayText(const QDate &d, const char *fmt = "ddd dd.MM.yyyy")
{
    return d.isValid() ? QLocale().toString(d, QString::fromLatin1(fmt)) : QString();
}

std::optional<Medication> findMed(qint64 pid, qint64 medId)
{
    for (const Medication &m : Database::instance().medications(pid, true))
        if (m.id == medId) return m;
    return std::nullopt;
}

QString medTitle(const Medication &m)
{
    return (m.name + QLatin1Char(' ') + m.strength).trimmed();
}
} // namespace

// ---------------------------------------------------------------- Sprachausgabe

Speech *AppController::speech()
{
    if (!m_speech) {
        m_speech = new Speech(this);
        m_speech->setRate(AppSettings::speechRate());
        m_speech->setChoice(AppSettings::voiceGender());
    }
    return m_speech;
}

QVariantMap AppController::speechInfo()
{
    Speech *s = speech();
    QVariantList voices;
    for (const auto &[k, label] : Speech::voiceChoices())
        voices << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("label"), label } };
    QString info;
    if (!s->available())
        info = tr("Keine Sprachausgabe verfügbar – der Assistent zeigt die Angaben nur an. "
                  "(Unter Android: Einstellungen → Bedienungshilfen → Text-in-Sprache-Ausgabe.)");
    else if (!s->hint().isEmpty())
        info = s->hint();
    else if (!s->languageMatches())
        info = tr("Für die eingestellte Sprache ist keine Systemstimme installiert, die Aussprache kann fremd klingen.");
    return { { QStringLiteral("available"), s->available() }, { QStringLiteral("voices"), voices },
             { QStringLiteral("voice"), AppSettings::voiceGender() }, { QStringLiteral("description"), s->voiceDescription() },
             { QStringLiteral("info"), info } };
}

void AppController::setVoice(const QString &key)
{
    AppSettings::setVoiceGender(key);
    speech()->setChoice(key);
}

void AppController::say(const QString &text)
{
    userActivity();
    speech()->say(text);
}

void AppController::stopSpeech()
{
    if (m_speech) m_speech->stop();
}

// ---------------------------------------------------------------- Medikamente stellen

QVariantMap AppController::prepInfo(qint64 pid)
{
    QVariantMap r;
    if (!loggedIn() || !pid) return r;
    const MedStock::PrepPlan plan = MedStock::prepPlan(pid);
    r.insert(QStringLiteral("weekly"), plan.weekly());
    r.insert(QStringLiteral("weekday"), plan.weekday);
    r.insert(QStringLiteral("start"), isoDate(plan.weekly() ? MedStock::nextPrepStart(plan, QDate::currentDate()) : QDate::currentDate()));
    QStringList slotList{ tr("alle Zeitpunkte") };
    for (int s = 0; s < 4; ++s) slotList << MedPrep::slotName(s);
    r.insert(QStringLiteral("slotNames"), slotList);
    const auto p = Database::instance().patient(pid);
    if (const auto last = Database::instance().lastPreparation(pid))
        r.insert(QStringLiteral("last"), tr("Zuletzt gestellt: %1 von %2 (für %3) – %4")
                                             .arg(QLocale().toString(last->preparedAt, QStringLiteral("ddd dd.MM.yyyy HH:mm")),
                                                  last->preparedBy, ReportPrinter::preparationForText(*last), MedPrep::statusText(*last)));
    else if (p)
        r.insert(QStringLiteral("last"), tr("Bisher wurden für %1 keine Medikamente mit dem Assistenten gestellt.").arg(p->displayName()));
    return r;
}

QString AppController::prepWeekStart(qint64 pid)
{
    MedStock::PrepPlan plan = MedStock::prepPlan(pid);
    if (!plan.weekly()) plan.weekday = QDate::currentDate().dayOfWeek();
    return isoDate(MedStock::nextPrepStart(MedStock::PrepPlan{ 7, plan.weekday }, QDate::currentDate()));
}

QVariantMap AppController::prepSession(qint64 pid, int days, int slot, const QString &isoDay)
{
    QVariantMap r;
    const auto p = Database::instance().patient(pid);
    if (!loggedIn() || !p) return r;
    days = std::max(1, days);
    const QDate day = dateFrom(isoDay).isValid() ? dateFrom(isoDay) : QDate::currentDate();
    const MedPrep::Selection sel = MedPrep::select(Database::instance().medications(pid, false), day, days, slot);
    QVariantList items;
    for (int i = 0; i < sel.list.size(); ++i) {
        const Medication &m = sel.list[i];
        QStringList details;
        if (!m.strength.isEmpty()) details << m.strength;
        if (!m.form.isEmpty()) details << m.form;
        if (!m.substance.isEmpty()) details << tr("Wirkstoff: %1").arg(m.substance);
        QVariantList doses, active;
        for (int s = 0; s < 4; ++s) {
            const QString d = MedPrep::doseOf(m, s);
            doses << (d.isEmpty() ? QString() : d + MedPrep::unitSuffix(m));
            active << (!d.isEmpty() && (slot < 0 || slot == s));
        }
        QVariantList week;
        if (days > 1) {
            for (int k = 0; k < days; ++k) {
                const QDate d = day.addDays(k);
                const bool due = MedPrep::givenOn(m, d);
                QVariantList wd;
                for (int s = 0; s < 4; ++s) wd << (due ? MedPrep::doseOf(m, s) : QString());
                week << QVariantMap{ { QStringLiteral("day"), QLocale().toString(d, QStringLiteral("ddd dd.MM.")) },
                                     { QStringLiteral("doses"), wd } };
            }
        }
        QVariantList hints;
        auto hint = [&](const QString &label, const QString &text, bool warn) {
            hints << QVariantMap{ { QStringLiteral("label"), label }, { QStringLiteral("text"), text }, { QStringLiteral("warn"), warn } };
        };
        if (MedSchedule::isInterval(m)) hint(tr("Einnahme:"), MedSchedule::describe(m), true);
        if (!m.rhythmNote.isEmpty()) hint(tr("Bemerkung:"), m.rhythmNote, true);
        if (!m.notes.isEmpty()) hint(tr("Hinweis:"), m.notes, false);
        if (!m.reason.isEmpty()) hint(tr("Grund:"), m.reason, false);
        double total = 0;
        for (int k = 0; k < days; ++k) total += MedStock::doseOn(m, day.addDays(k), slot);
        items << QVariantMap{ { QStringLiteral("id"), m.id }, { QStringLiteral("name"), m.name },
                              { QStringLiteral("details"), details.join(QStringLiteral(" · ")) },
                              { QStringLiteral("unit"), m.unit }, { QStringLiteral("doses"), doses },
                              { QStringLiteral("active"), active }, { QStringLiteral("week"), week },
                              { QStringLiteral("hints"), hints },
                              { QStringLiteral("total"), total > 0 && days > 1 ? tr("insgesamt %1%2").arg(MedStock::qtyText(total), MedPrep::unitSuffix(m)) : QString() },
                              { QStringLiteral("speech"), MedPrep::textFor(m, day, days, slot) } };
    }
    r.insert(QStringLiteral("items"), items);
    r.insert(QStringLiteral("intro"), MedPrep::introText(*p, day, days, slot, int(sel.list.size())));
    r.insert(QStringLiteral("nothing"), MedPrep::nothingText(days));
    r.insert(QStringLiteral("doneText"), MedPrep::doneText());
    r.insert(QStringLiteral("range"), days > 1 ? tr("%1 bis %2").arg(dayText(day), dayText(day.addDays(days - 1))) : dayText(day));
    r.insert(QStringLiteral("notDue"), sel.notDue);
    r.insert(QStringLiteral("asNeeded"), sel.asNeeded);
    r.insert(QStringLiteral("other"), sel.other);
    r.insert(QStringLiteral("emergency"), sel.emergency);
    return r;
}

QVariantMap AppController::prepFinish(qint64 pid, int days, int slot, const QString &isoDay, const QVariantList &confirmed,
                                      bool complete, const QString &note)
{
    QVariantMap r;
    const auto p = Database::instance().patient(pid);
    if (!loggedIn() || !p) return r;
    days = std::max(1, days);
    const QDate day = dateFrom(isoDay).isValid() ? dateFrom(isoDay) : QDate::currentDate();
    const MedPrep::Selection sel = MedPrep::select(Database::instance().medications(pid, false), day, days, slot);
    QSet<int> conf;
    for (const QVariant &v : confirmed) conf.insert(v.toInt());
    if (sel.list.isEmpty() || (!complete && conf.isEmpty())) return r;
    const MedPrep::LogResult lr = MedPrep::log(*p, Session::user(), sel.list, conf, complete, slot, day, days, note);
    if (!lr.ok) { setError(lr.error); return r; }
    if (complete) Sounds::play(Sounds::Kind::Done);
    QStringList warn;
    for (const QString &s : lr.shortage)
        warn << tr("Medikament %1: Der Bestand reichte beim Stellen nicht – bitte Bestand prüfen.").arg(s);
    warn << MedStock::reminderLines(MedStock::updateOpenOrder(pid));
    r.insert(QStringLiteral("ok"), true);
    r.insert(QStringLiteral("id"), lr.id);
    r.insert(QStringLiteral("warnings"), warn);
    r.insert(QStringLiteral("logged"), tr("Im Protokoll vermerkt: %1 von %2")
                                           .arg(QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("dd.MM.yyyy HH:mm")), Session::user()));
    bump();
    return r;
}

bool AppController::prepNote(qint64 prepId, const QString &note)
{
    return prepId && Database::instance().updatePreparationNote(prepId, note.trimmed());
}

QVariantList AppController::preparationLog(qint64 pid, int days)
{
    QVariantList l;
    if (!loggedIn() || !pid) return l;
    const QDate from = days > 0 ? QDate::currentDate().addDays(-days) : QDate();
    QList<MedPreparation> log = Database::instance().preparations(pid, from, QDate());
    std::sort(log.begin(), log.end(), [](const MedPreparation &a, const MedPreparation &b) { return a.preparedAt > b.preparedAt; });
    for (const MedPreparation &e : std::as_const(log)) {
        QStringList items;
        for (const QJsonValue &v : e.items) {
            const QJsonObject o = v.toObject();
            QString t = (o.value(QStringLiteral("name")).toString() + QLatin1Char(' ') + o.value(QStringLiteral("strength")).toString()).trimmed();
            const QString dose = o.value(QStringLiteral("dose")).toString();
            if (!dose.isEmpty()) t += QStringLiteral(" – ") + dose;
            if (!o.value(QStringLiteral("confirmed")).toBool(true)) t += QStringLiteral(" (") + tr("nicht bestätigt") + QLatin1Char(')');
            items << t;
        }
        l << QVariantMap{ { QStringLiteral("id"), e.id },
                          { QStringLiteral("when"), QLocale().toString(e.preparedAt, QStringLiteral("ddd dd.MM.yyyy HH:mm")) },
                          { QStringLiteral("by"), e.preparedBy }, { QStringLiteral("forText"), ReportPrinter::preparationForText(e) },
                          { QStringLiteral("status"), MedPrep::statusText(e) }, { QStringLiteral("complete"), e.complete },
                          { QStringLiteral("items"), items }, { QStringLiteral("note"), e.note } };
    }
    return l;
}

// ---------------------------------------------------------------- Bestand

QVariantMap AppController::stockOverview(qint64 pid)
{
    QVariantMap r;
    if (!loggedIn() || !pid) return r;
    const MedStock::PrepPlan plan = MedStock::prepPlan(pid);
    r.insert(QStringLiteral("weekly"), plan.weekly());
    r.insert(QStringLiteral("weekday"), plan.weekday);
    r.insert(QStringLiteral("lead"), MedStock::leadDays());
    r.insert(QStringLiteral("planText"), MedStock::prepPlanText(plan));
    QStringList wd;
    for (int i = 1; i <= 7; ++i) wd << QLocale().dayName(i);
    r.insert(QStringLiteral("weekdays"), wd);
    QVariantList meds;
    int warn = 0;
    for (const auto &[cat, list] : MedSchedule::grouped(Database::instance().medications(pid, false))) {
        for (const Medication &m : list) {
            const MedStock::Forecast f = MedStock::forecast(m);
            const QString u = MedPrep::unitSuffix(m);
            QVariantMap x{ { QStringLiteral("id"), m.id }, { QStringLiteral("name"), medTitle(m) }, { QStringLiteral("unit"), m.unit },
                           { QStringLiteral("category"), m.category }, { QStringLiteral("tracked"), f.tracked },
                           { QStringLiteral("state"), f.state }, { QStringLiteral("onOrder"), f.onOrder },
                           { QStringLiteral("orderConfirmed"), f.orderConfirmed }, { QStringLiteral("stateText"), f.stateText() },
                           { QStringLiteral("orderAmount"), m.extra.value(QStringLiteral("orderAmount")).toString() } };
            if (f.tracked) {
                x.insert(QStringLiteral("stock"), MedStock::qtyText(f.stock) + u);
                x.insert(QStringLiteral("expired"), f.expiredQty > 0 ? MedStock::qtyText(f.expiredQty) + u : QString());
                x.insert(QStringLiteral("daily"), f.daily > 0 ? MedStock::qtyText(std::round(f.daily * 100) / 100) + u : QString());
                x.insert(QStringLiteral("until"), f.runOut.isValid() ? dayText(f.runOut.addDays(-1)) : (f.daily > 0 ? tr("mehr als ein Jahr") : QString()));
                x.insert(QStringLiteral("orderBy"), dayText(f.orderBy));
                x.insert(QStringLiteral("nextExpiry"), dayText(f.nextExpiry, "dd.MM.yyyy"));
                if ((f.state == MedStock::OrderNow || f.state == MedStock::Empty) && !f.onOrder) ++warn;
            }
            meds << x;
        }
    }
    r.insert(QStringLiteral("meds"), meds);
    r.insert(QStringLiteral("warnCount"), warn);
    QVariantList orders;
    bool anyOpen = false;
    for (const MedStock::Order &o : MedStock::orders(pid)) {
        QVariantList items;
        int open = 0;
        for (const MedStock::OrderItem &it : o.items) {
            open += !it.delivered;
            items << QVariantMap{ { QStringLiteral("id"), it.id }, { QStringLiteral("medId"), it.medicationId },
                                  { QStringLiteral("name"), (it.name + QLatin1Char(' ') + it.strength).trimmed() },
                                  { QStringLiteral("pzn"), it.pzn }, { QStringLiteral("amount"), it.amount },
                                  { QStringLiteral("reason"), it.reason }, { QStringLiteral("delivered"), it.delivered },
                                  { QStringLiteral("deliveredText"), it.delivered ? tr("geliefert am %1 (%2)").arg(dayText(it.deliveredAt, "dd.MM.yyyy"), MedStock::qtyText(it.deliveredQty)) : QString() } };
        }
        anyOpen = anyOpen || o.status == MedStock::Open;
        orders << QVariantMap{ { QStringLiteral("id"), o.id }, { QStringLiteral("status"), o.status },
                               { QStringLiteral("statusText"), o.statusText() },
                               { QStringLiteral("created"), QLocale().toString(o.createdAt, QStringLiteral("dd.MM.yyyy HH:mm")) },
                               { QStringLiteral("note"), o.note }, { QStringLiteral("openCount"), open },
                               { QStringLiteral("items"), items } };
    }
    r.insert(QStringLiteral("orders"), orders);
    r.insert(QStringLiteral("anyOpen"), anyOpen);
    return r;
}

QStringList AppController::stockReminder(qint64 pid)
{
    if (!loggedIn() || !pid) return {};
    return MedStock::reminderLines(MedStock::updateOpenOrder(pid));
}

void AppController::setPrepPlan(qint64 pid, int days, int weekday)
{
    MedStock::setPrepPlan(pid, MedStock::PrepPlan{ days >= 7 ? 7 : 1, std::clamp(weekday, 1, 7) });
    bump();
}

void AppController::setLeadDays(int days)
{
    MedStock::setLeadDays(std::clamp(days, 1, 60));
    bump();
}

QVariantMap AppController::stockPackages(qint64 pid, qint64 medId)
{
    QVariantMap r;
    const auto m = findMed(pid, medId);
    if (!loggedIn() || !m) return r;
    const QDate today = QDate::currentDate();
    QVariantList pk;
    for (const MedStock::Package &p : MedStock::packages(medId, false))
        pk << QVariantMap{ { QStringLiteral("id"), p.id }, { QStringLiteral("qty"), p.quantity },
                           { QStringLiteral("qtyText"), MedStock::qtyText(p.quantity) + MedPrep::unitSuffix(*m) },
                           { QStringLiteral("expiry"), isoDate(p.expiry) }, { QStringLiteral("expiryText"), dayText(p.expiry, "dd.MM.yyyy") },
                           { QStringLiteral("expired"), p.expiredOn(today) }, { QStringLiteral("received"), dayText(p.received, "dd.MM.yyyy") },
                           { QStringLiteral("note"), p.note } };
    const QStringList kinds = { tr("gestellt"), tr("Eingang"), tr("Korrektur"), tr("entsorgt") };
    QVariantList mv;
    for (const MedStock::Move &x : MedStock::moves(medId, 60))
        mv << QVariantMap{ { QStringLiteral("when"), QLocale().toString(x.at, QStringLiteral("dd.MM.yyyy HH:mm")) },
                           { QStringLiteral("kind"), kinds.value(x.kind) },
                           { QStringLiteral("qty"), (x.quantity > 0 ? QStringLiteral("+") : QString()) + MedStock::qtyText(x.quantity) },
                           { QStringLiteral("forDate"), dayText(x.forDate, "ddd dd.MM.") }, { QStringLiteral("by"), x.by },
                           { QStringLiteral("note"), x.note } };
    r.insert(QStringLiteral("name"), medTitle(*m));
    r.insert(QStringLiteral("unit"), m->unit);
    r.insert(QStringLiteral("tracked"), MedStock::isTracked(*m));
    r.insert(QStringLiteral("packages"), pk);
    r.insert(QStringLiteral("moves"), mv);
    return r;
}

bool AppController::receivePackage(qint64 pid, qint64 medId, double qty, const QString &isoExpiry, const QString &note)
{
    const auto m = findMed(pid, medId);
    if (!m || qty <= 0) { setError(tr("Bitte eine Menge angeben.")); return false; }
    MedStock::Package p;
    p.quantity = qty;
    p.expiry = dateFrom(isoExpiry);
    p.note = note.trimmed();
    QString err;
    if (!MedStock::addPackage(*m, p, p.note, &err)) { setError(err); return false; }
    bump();
    return true;
}

bool AppController::correctPackage(qint64 pid, qint64 medId, qint64 packageId, double qty, const QString &isoExpiry, const QString &note)
{
    Q_UNUSED(pid)
    for (const MedStock::Package &p : MedStock::packages(medId, true)) {
        if (p.id != packageId) continue;
        QString err;
        if (!MedStock::changePackage(p, std::max(0.0, qty), dateFrom(isoExpiry), note.trimmed(), &err)) { setError(err); return false; }
        bump();
        return true;
    }
    return false;
}

bool AppController::discardPackage(qint64 medId, qint64 packageId)
{
    for (const MedStock::Package &p : MedStock::packages(medId, true)) {
        if (p.id != packageId) continue;
        QString err;
        if (!MedStock::discardPackage(p, &err)) { setError(err); return false; }
        bump();
        return true;
    }
    return false;
}

bool AppController::setStockTracked(qint64 pid, qint64 medId, bool on)
{
    auto m = findMed(pid, medId);
    if (!m) return false;
    MedStock::setTracked(*m, on);
    bump();
    return true;
}

bool AppController::addToOrder(qint64 pid, qint64 medId)
{
    const auto m = findMed(pid, medId);
    if (!m) return false;
    const MedStock::Forecast f = MedStock::forecast(*m);
    QString err;
    if (!MedStock::addToOpenOrder(pid, *m, f.tracked ? f.reason(m->unit) : tr("von Hand hinzugefügt"), &err)) { setError(err); return false; }
    bump();
    return true;
}

int AppController::createOrderList(qint64 pid)
{
    const MedStock::Order o = MedStock::updateOpenOrder(pid);
    bump();
    return o.id ? int(o.items.size()) : 0;
}

bool AppController::confirmOrder(qint64 orderId, const QString &isoDay, const QString &note)
{
    const QDate d = dateFrom(isoDay).isValid() ? std::min(dateFrom(isoDay), QDate::currentDate()) : QDate::currentDate();
    const QDateTime when(d, d == QDate::currentDate() ? QTime::currentTime() : QTime(12, 0));
    QString err;
    if (!MedStock::confirmOrdered(orderId, when, Session::user(), note.trimmed(), &err)) { setError(err); return false; }
    bump();
    return true;
}

bool AppController::deliverOrderItem(qint64 orderId, qint64 itemId, double qty, const QString &isoExpiry)
{
    const auto o = MedStock::order(orderId);
    if (!o) return false;
    for (const MedStock::OrderItem &it : o->items) {
        if (it.id != itemId || it.delivered) continue;
        if (!findMed(o->patientId, it.medicationId)) {
            setError(tr("Das Medikament steht nicht mehr im Medikamentenplan."));
            return false;
        }
        QString err;
        if (!MedStock::deliverItem(it, qty, dateFrom(isoExpiry), &err)) { setError(err); return false; }
        bump();
        return true;
    }
    return false;
}

bool AppController::setOrderItemAmount(qint64 pid, qint64 itemId, qint64 medId, const QString &amount)
{
    const QString a = amount.trimmed();
    if (a.isEmpty() || !MedStock::updateItemAmount(itemId, a)) return false;
    if (auto m = findMed(pid, medId)) {   // als Vorschlag für künftige Bestellungen merken
        m->extra[QStringLiteral("orderAmount")] = a;
        Database::instance().updateMedicationLeaflet(m->id, m->pzn, m->extra);
    }
    bump();
    return true;
}

bool AppController::removeOrderItem(qint64 itemId)
{
    const bool ok = MedStock::removeItem(itemId);
    bump();
    return ok;
}

bool AppController::deleteOrder(qint64 orderId)
{
    const bool ok = MedStock::deleteOrder(orderId);
    bump();
    return ok;
}
