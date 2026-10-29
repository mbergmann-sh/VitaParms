// SPDX-License-Identifier: MIT
#include "medstock.h"
#include "database.h"
#include "medschedule.h"
#include "session.h"
#include "sqlquery.h"

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>
#include <cmath>

using MedStock::Package;

namespace {
QString tr(const char *s) { return QCoreApplication::translate("MedStock", s); }
const QString kDt = QStringLiteral("yyyy-MM-ddTHH:mm:ss");
SqlDb db() { return Database::instance().handleDb(); }
QVariant dateVar(const QDate &d) { return d.isValid() ? QVariant(d.toString(Qt::ISODate)) : QVariant(); }
QDate dateOf(const QVariant &v) { return QDate::fromString(v.toString().left(10), Qt::ISODate); }
bool failQ(const SqlQuery &q, QString *error) { if (error) *error = q.lastError().text(); return false; }

QString dayText(const QDate &d)
{
    return QLocale().toString(d, QStringLiteral("ddd dd.MM.yyyy"));
}

std::optional<Medication> medicationById(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT patient_id FROM medications WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next()) return std::nullopt;
    const qint64 pid = q.value(0).toLongLong();
    q.finish();
    for (const Medication &m : Database::instance().medications(pid, true))
        if (m.id == id) return m;
    return std::nullopt;
}

QString medUid(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT coalesce(uid, '') FROM medications WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next() && !q.value(0).toString().isEmpty()) return q.value(0).toString();
    return QString::number(id);
}

bool insertMove(qint64 patientId, qint64 medId, qint64 stockId, int kind, double qty, const QDate &forDate,
                const QString &note, const QString &prepKey, QString *error)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO med_stock_moves (patient_id, medication_id, stock_id, at, for_date, kind,"
                             " quantity, by_user, note, prep_key) VALUES (?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(patientId);
    q.addBindValue(medId);
    q.addBindValue(stockId > 0 ? QVariant(stockId) : QVariant());
    q.addBindValue(QDateTime::currentDateTime().toString(kDt));
    q.addBindValue(dateVar(forDate));
    q.addBindValue(kind);
    q.addBindValue(qty);
    q.addBindValue(Session::user());
    q.addBindValue(note.isEmpty() ? QVariant() : QVariant(note));
    q.addBindValue(prepKey.isEmpty() ? QVariant() : QVariant(prepKey));
    return q.exec() || failQ(q, error);
}

bool setQuantity(qint64 stockId, double qty, QString *error)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE med_stock SET quantity = ?, modified_at = datetime('now','localtime') WHERE id = ?"));
    q.addBindValue(qty);
    q.addBindValue(stockId);
    return q.exec() || failQ(q, error);
}

Package packageFrom(const SqlQuery &q)
{
    Package p;
    p.id = q.value(QStringLiteral("id")).toLongLong();
    p.patientId = q.value(QStringLiteral("patient_id")).toLongLong();
    p.medicationId = q.value(QStringLiteral("medication_id")).toLongLong();
    p.quantity = q.value(QStringLiteral("quantity")).toDouble();
    p.expiry = dateOf(q.value(QStringLiteral("expiry")));
    p.received = dateOf(q.value(QStringLiteral("received")));
    p.note = q.value(QStringLiteral("note")).toString();
    return p;
}

// Verbrauchsreihenfolge: zuerst ablaufende Packung, Packungen ohne Verfallsdatum zuletzt
void sortFefo(QList<Package> &l)
{
    std::stable_sort(l.begin(), l.end(), [](const Package &a, const Package &b) {
        if (a.expiry.isValid() != b.expiry.isValid()) return a.expiry.isValid();
        if (a.expiry != b.expiry) return a.expiry < b.expiry;
        return a.id < b.id;
    });
}
} // namespace

namespace MedStock {

// ------------------------------------------------------------------ Stell-Rhythmus

PrepPlan prepPlan(qint64 patientId)
{
    PrepPlan p;
    if (const auto pt = Database::instance().patient(patientId)) {
        p.days = pt->extra.value(QStringLiteral("prepDays")).toInt(1) >= 7 ? 7 : 1;
        p.weekday = std::clamp(pt->extra.value(QStringLiteral("prepWeekday")).toInt(1), 1, 7);
    }
    return p;
}

void setPrepPlan(qint64 patientId, const PrepPlan &plan)
{
    auto pt = Database::instance().patient(patientId);
    if (!pt) return;
    const int days = plan.weekly() ? 7 : 1, wd = std::clamp(plan.weekday, 1, 7);
    if (pt->extra.value(QStringLiteral("prepDays")).toInt(1) == days && pt->extra.value(QStringLiteral("prepWeekday")).toInt(1) == wd)
        return;
    pt->extra[QStringLiteral("prepDays")] = days;
    pt->extra[QStringLiteral("prepWeekday")] = wd;
    Database::instance().savePatient(*pt);
    Database::instance().notifyStockChanged(patientId);
}

QDate nextPrepStart(const PrepPlan &plan, const QDate &from)
{
    if (!plan.weekly()) return from;
    QDate d = from;
    while (d.dayOfWeek() != plan.weekday) d = d.addDays(1);
    return d;
}

QString prepPlanText(const PrepPlan &plan)
{
    if (!plan.weekly()) return tr("täglich (Tagesdosierer)");
    return tr("wöchentlich (Wochendosierer, ab %1)").arg(QLocale().dayName(plan.weekday));
}

// ------------------------------------------------------------------ Einstellungen und Hilfen

int leadDays()
{
    return std::clamp(QSettings().value(QStringLiteral("stock/leadDays"), 7).toInt(), 1, 60);
}

void setLeadDays(int days)
{
    QSettings().setValue(QStringLiteral("stock/leadDays"), std::clamp(days, 1, 60));
}

double parseAmount(const QString &text)
{
    QString t = text.trimmed();
    if (t.isEmpty() || t == QLatin1String("-")) return 0;
    static const QList<QPair<QString, double>> fractions = {
        { QStringLiteral("½"), 0.5 }, { QStringLiteral("¼"), 0.25 }, { QStringLiteral("¾"), 0.75 },
        { QStringLiteral("⅓"), 1.0 / 3 }, { QStringLiteral("⅔"), 2.0 / 3 },
    };
    double v = 0;
    for (const auto &[sym, val] : fractions)
        if (t.contains(sym)) { v += val; t.remove(sym); }
    t = t.trimmed();
    static const QRegularExpression frac(QStringLiteral("^(\\d+)\\s*/\\s*(\\d+)"));
    if (const auto m = frac.match(t); m.hasMatch()) {
        const double den = m.captured(2).toDouble();
        return v + (den > 0 ? m.captured(1).toDouble() / den : 0);
    }
    static const QRegularExpression num(QStringLiteral("^(\\d+(?:[.,]\\d+)?)"));
    if (const auto m = num.match(t); m.hasMatch())
        v += m.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
    return v;
}

double doseOn(const Medication &m, const QDate &day, int slot)
{
    if (m.category != Medication::Regular) return 0;   // Bedarf/Notfall/Sonstiges: kein fester Verbrauch
    if (m.startDate.isValid() && day < m.startDate) return 0;
    if (m.endDate.isValid() && day > m.endDate) return 0;
    if (!MedSchedule::isDueOn(m, day)) return 0;
    const QString d[] = { m.morning, m.noon, m.evening, m.night };
    double sum = 0;
    for (int s = 0; s < 4; ++s)
        if (slot < 0 || slot == s) sum += parseAmount(d[s]);
    return sum;
}

bool isTracked(const Medication &m)
{
    return m.extra.value(QStringLiteral("stock")).toBool();
}

void setTracked(Medication &m, bool on)
{
    if (on) m.extra[QStringLiteral("stock")] = true;
    else m.extra.remove(QStringLiteral("stock"));
    if (m.id) Database::instance().updateMedicationLeaflet(m.id, m.pzn, m.extra);
}

QDate orderDeadline(const QDate &runOut, int lead)
{
    QDate d = runOut.addDays(-lead);
    while (d.dayOfWeek() >= 6) d = d.addDays(-1);   // Sa/So: keine Bestellung möglich → Freitag
    return d;
}

QString qtyText(double q)
{
    const QLocale loc;
    QString s = loc.toString(q, 'f', 2);
    if (s.contains(loc.decimalPoint())) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(loc.decimalPoint())) s.chop(QString(loc.decimalPoint()).size());
    }
    return s;
}

// ------------------------------------------------------------------ Packungen und Buchungen

QList<Package> packages(qint64 medicationId, bool includeEmpty)
{
    QList<Package> l;
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM med_stock WHERE medication_id = ?%1")
                  .arg(includeEmpty ? QString() : QStringLiteral(" AND quantity > 0.0001")));
    q.addBindValue(medicationId);
    if (q.exec())
        while (q.next()) l << packageFrom(q);
    sortFefo(l);
    return l;
}

bool addPackage(const Medication &m, Package &p, const QString &note, QString *error)
{
    if (!m.id || p.quantity <= 0) { if (error) *error = tr("Bitte eine Stückzahl größer als 0 angeben."); return false; }
    SqlDb d = db();
    d.transaction();
    SqlQuery q(d);
    q.prepare(QStringLiteral("INSERT INTO med_stock (patient_id, medication_id, quantity, expiry, received, note)"
                             " VALUES (?,?,?,?,?,?)"));
    q.addBindValue(m.patientId);
    q.addBindValue(m.id);
    q.addBindValue(p.quantity);
    q.addBindValue(dateVar(p.expiry));
    q.addBindValue(dateVar(p.received.isValid() ? p.received : QDate::currentDate()));
    q.addBindValue(p.note.isEmpty() ? QVariant() : QVariant(p.note));
    if (!q.exec()) { d.rollback(); return failQ(q, error); }
    p.id = q.lastInsertId().toLongLong();
    p.patientId = m.patientId;
    p.medicationId = m.id;
    if (!insertMove(m.patientId, m.id, p.id, Receipt, p.quantity, QDate::currentDate(),
                    note.isEmpty() ? tr("Packung eingebucht") : note, {}, error)) { d.rollback(); return false; }
    if (!d.commit()) { if (error) *error = d.lastError().text(); return false; }
    if (!isTracked(m)) { Medication c = m; setTracked(c, true); }
    Database::instance().notifyStockChanged(m.patientId);
    return true;
}

bool changePackage(const Package &p, double newQty, const QDate &expiry, const QString &note, QString *error)
{
    SqlDb d = db();
    d.transaction();
    SqlQuery q(d);
    q.prepare(QStringLiteral("UPDATE med_stock SET quantity = ?, expiry = ?, note = ?, modified_at = datetime('now','localtime')"
                             " WHERE id = ?"));
    q.addBindValue(std::max(0.0, newQty));
    q.addBindValue(dateVar(expiry));
    q.addBindValue(note.isEmpty() ? QVariant() : QVariant(note));
    q.addBindValue(p.id);
    if (!q.exec()) { d.rollback(); return failQ(q, error); }
    const double diff = std::max(0.0, newQty) - p.quantity;
    if (std::abs(diff) > 0.0001
        && !insertMove(p.patientId, p.medicationId, p.id, Correction, diff, QDate::currentDate(), tr("Bestand korrigiert"), {}, error)) {
        d.rollback();
        return false;
    }
    if (!d.commit()) { if (error) *error = d.lastError().text(); return false; }
    Database::instance().notifyStockChanged(p.patientId);
    return true;
}

bool discardPackage(const Package &p, QString *error)
{
    SqlDb d = db();
    d.transaction();
    if (!setQuantity(p.id, 0, error)
        || !insertMove(p.patientId, p.medicationId, p.id, Discard, -p.quantity, QDate::currentDate(),
                       p.expiredOn(QDate::currentDate()) ? tr("verfallen, entsorgt") : tr("entsorgt"), {}, error)) {
        d.rollback();
        return false;
    }
    if (!d.commit()) { if (error) *error = d.lastError().text(); return false; }
    Database::instance().notifyStockChanged(p.patientId);
    return true;
}

QList<Move> moves(qint64 medicationId, int limit)
{
    QList<Move> l;
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM med_stock_moves WHERE medication_id = ? ORDER BY at DESC, id DESC LIMIT %1").arg(limit));
    q.addBindValue(medicationId);
    if (q.exec())
        while (q.next()) {
            Move m;
            m.id = q.value(QStringLiteral("id")).toLongLong();
            m.medicationId = medicationId;
            m.stockId = q.value(QStringLiteral("stock_id")).toLongLong();
            m.at = QDateTime::fromString(q.value(QStringLiteral("at")).toString(), kDt);
            m.forDate = dateOf(q.value(QStringLiteral("for_date")));
            m.kind = q.value(QStringLiteral("kind")).toInt();
            m.quantity = q.value(QStringLiteral("quantity")).toDouble();
            m.by = q.value(QStringLiteral("by_user")).toString();
            m.note = q.value(QStringLiteral("note")).toString();
            l << m;
        }
    return l;
}

QStringList bookPreparation(qint64 patientId, const QList<Medication> &meds, const QDate &day, int slot)
{
    QStringList shortage;
    bool any = false;
    SqlDb d = db();
    d.transaction();
    for (const Medication &m : meds) {
        if (!isTracked(m) || m.archived) continue;
        const QString uid = medUid(m.id);
        for (int s = 0; s < 4; ++s) {
            if (slot >= 0 && s != slot) continue;
            const double need = doseOn(m, day, s);
            if (need <= 0) continue;
            const QString key = QStringLiteral("%1|%2|%3").arg(uid, day.toString(Qt::ISODate)).arg(s);
            {
                SqlQuery c(d);
                c.prepare(QStringLiteral("SELECT 1 FROM med_stock_moves WHERE prep_key = ? LIMIT 1"));
                c.addBindValue(key);
                if (c.exec() && c.next()) continue;   // schon abgebucht
            }
            double rest = need;
            for (const Package &p : packages(m.id)) {
                if (rest <= 0.0001) break;
                if (p.expiredOn(day)) continue;
                const double take = std::min(rest, p.quantity);
                setQuantity(p.id, p.quantity - take, nullptr);
                insertMove(patientId, m.id, p.id, Preparation, -take, day, {}, key, nullptr);
                rest -= take;
            }
            if (rest > 0.0001) {
                insertMove(patientId, m.id, 0, Preparation, -rest, day,
                           tr("Bestand reichte nicht (Fehlmenge)"), key, nullptr);
                if (!shortage.contains(m.name)) shortage << m.name;
            }
            any = true;
        }
    }
    d.commit();
    if (any) Database::instance().notifyStockChanged(patientId);
    return shortage;
}

// ------------------------------------------------------------------ Reichweite

Forecast forecast(const Medication &m, const QDate &today)
{
    Forecast f;
    f.tracked = isTracked(m);
    if (!f.tracked || !m.id) return f;
    QList<Package> pk = packages(m.id);
    for (const Package &p : pk) {
        if (p.expiredOn(today)) { f.expiredQty += p.quantity; continue; }
        f.stock += p.quantity;
        if (p.expiry.isValid() && (!f.nextExpiry.isValid() || p.expiry < f.nextExpiry)) f.nextExpiry = p.expiry;
    }
    double sum = 0;
    for (int i = 0; i < 28; ++i) sum += doseOn(m, today.addDays(i));
    f.daily = sum / 28.0;

    // bereits gestellte Tage sind abgebucht → Vorausrechnung ab dem Tag nach dem letzten Stellen
    QDate start = today;
    {
        SqlQuery q(db());
        q.prepare(QStringLiteral("SELECT max(for_date) FROM med_stock_moves WHERE medication_id = ? AND kind = 0"));
        q.addBindValue(m.id);
        if (q.exec() && q.next()) {
            const QDate last = dateOf(q.value(0));
            if (last.isValid() && last >= today) start = last.addDays(1);
        }
    }
    // Gestellt wird in Blöcken: täglich je ein Tag, wöchentlich bis zum Tag vor dem nächsten
    // Stelltag. Am Stelltag muss der Bestand für den ganzen Block reichen.
    const PrepPlan plan = prepPlan(m.patientId);
    QDate d = start;
    const QDate horizon = start.addDays(400);
    while (d <= horizon) {
        if (m.endDate.isValid() && d > m.endDate) break;
        const QDate blockEnd = plan.weekly() ? nextPrepStart(plan, d.addDays(1)).addDays(-1) : d;
        double need = 0;
        for (QDate x = d; x <= blockEnd; x = x.addDays(1)) need += doseOn(m, x);
        if (need > 0) {
            for (Package &p : pk) {
                if (need <= 0.0001) break;
                if (p.expiredOn(d) || p.quantity <= 0) continue;
                const double take = std::min(need, p.quantity);
                p.quantity -= take;
                need -= take;
            }
            if (need > 0.0001) { f.runOut = d; break; }
        }
        d = blockEnd.addDays(1);
    }
    if (f.runOut.isValid()) f.orderBy = orderDeadline(f.runOut, leadDays());
    {
        SqlQuery q(db());
        q.prepare(QStringLiteral("SELECT max(o.status) FROM med_order_items i JOIN med_orders o ON o.id = i.order_id"
                                 " WHERE i.medication_id = ? AND i.delivered = 0 AND o.status IN (0, 1)"));
        q.addBindValue(m.id);
        if (q.exec() && q.next() && !q.value(0).isNull()) {
            f.onOrder = true;
            f.orderConfirmed = q.value(0).toInt() == Ordered;
        }
    }
    if (f.stock <= 0.0001) f.state = Empty;
    else if (f.runOut.isValid() && today >= f.orderBy) f.state = OrderNow;
    else if (f.runOut.isValid() && today >= f.orderBy.addDays(-3)) f.state = OrderSoon;
    else f.state = Ok;
    return f;
}

QString Forecast::reason(const QString &unit) const
{
    const QString u = unit.isEmpty() ? QString() : QLatin1Char(' ') + unit;
    QString r;
    if (state == Empty)
        r = expiredQty > 0 ? QCoreApplication::translate("MedStock", "kein verwendbarer Bestand – Packung verfallen") : QCoreApplication::translate("MedStock", "kein Bestand mehr");
    else if (runOut.isValid())
        r = QCoreApplication::translate("MedStock", "reicht bis %1 (noch %2%3)").arg(dayText(runOut.addDays(-1)), qtyText(stock), u);
    else
        r = QCoreApplication::translate("MedStock", "Bestand %1%2").arg(qtyText(stock), u);
    if (state != Empty && expiredQty > 0)
        r += QCoreApplication::translate("MedStock", " – %1%2 verfallen").arg(qtyText(expiredQty), u);
    return r;
}

QString Forecast::stateText() const
{
    if (!tracked) return QCoreApplication::translate("MedStock", "nicht erfasst");
    if (onOrder && state != Ok) return orderConfirmed ? QCoreApplication::translate("MedStock", "bestellt") : QCoreApplication::translate("MedStock", "auf der Bestellliste – noch nicht bestellt");
    switch (state) {
    case Empty:     return expiredQty > 0 ? QCoreApplication::translate("MedStock", "verfallen – nachbestellen") : QCoreApplication::translate("MedStock", "leer – nachbestellen");
    case OrderNow:  return QCoreApplication::translate("MedStock", "jetzt bestellen");
    case OrderSoon: return QCoreApplication::translate("MedStock", "bald bestellen");
    default:        return expiredQty > 0 ? QCoreApplication::translate("MedStock", "in Ordnung – verfallene Packung entsorgen") : QCoreApplication::translate("MedStock", "in Ordnung");
    }
}

// ------------------------------------------------------------------ Bestellungen

QString Order::statusText() const
{
    const QLocale loc;
    switch (status) {
    case Ordered:   return QCoreApplication::translate("MedStock", "bestellt am %1").arg(loc.toString(orderedAt, QStringLiteral("dd.MM.yyyy")));
    case Delivered: return QCoreApplication::translate("MedStock", "geliefert am %1").arg(loc.toString(deliveredAt, QStringLiteral("dd.MM.yyyy")));
    default:        return QCoreApplication::translate("MedStock", "offen – Bestellung noch nicht bestätigt");
    }
}

static QList<OrderItem> itemsOf(qint64 orderId)
{
    QList<OrderItem> l;
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM med_order_items WHERE order_id = ? ORDER BY name COLLATE NOCASE, id"));
    q.addBindValue(orderId);
    if (q.exec())
        while (q.next()) {
            OrderItem i;
            i.id = q.value(QStringLiteral("id")).toLongLong();
            i.orderId = orderId;
            i.medicationId = q.value(QStringLiteral("medication_id")).toLongLong();
            i.name = q.value(QStringLiteral("name")).toString();
            i.substance = q.value(QStringLiteral("substance")).toString();
            i.strength = q.value(QStringLiteral("strength")).toString();
            i.form = q.value(QStringLiteral("form")).toString();
            i.pzn = q.value(QStringLiteral("pzn")).toString();
            i.amount = q.value(QStringLiteral("amount")).toString();
            i.reason = q.value(QStringLiteral("reason")).toString();
            i.delivered = q.value(QStringLiteral("delivered")).toInt() != 0;
            i.deliveredQty = q.value(QStringLiteral("delivered_qty")).toDouble();
            i.deliveredAt = dateOf(q.value(QStringLiteral("delivered_at")));
            l << i;
        }
    return l;
}

static Order orderFrom(const SqlQuery &q)
{
    Order o;
    o.id = q.value(QStringLiteral("id")).toLongLong();
    o.patientId = q.value(QStringLiteral("patient_id")).toLongLong();
    o.createdAt = QDateTime::fromString(q.value(QStringLiteral("created_at")).toString(), kDt);
    o.createdBy = q.value(QStringLiteral("created_by")).toString();
    o.status = q.value(QStringLiteral("status")).toInt();
    o.orderedAt = QDateTime::fromString(q.value(QStringLiteral("ordered_at")).toString(), kDt);
    o.orderedBy = q.value(QStringLiteral("ordered_by")).toString();
    o.deliveredAt = dateOf(q.value(QStringLiteral("delivered_at")));
    o.note = q.value(QStringLiteral("note")).toString();
    return o;
}

QList<Order> orders(qint64 patientId)
{
    QList<Order> l;
    {
        SqlQuery q(db());
        q.prepare(QStringLiteral("SELECT * FROM med_orders WHERE patient_id = ? ORDER BY status = 0 DESC, created_at DESC, id DESC"));
        q.addBindValue(patientId);
        if (q.exec())
            while (q.next()) l << orderFrom(q);
    }
    for (Order &o : l) o.items = itemsOf(o.id);
    return l;
}

std::optional<Order> order(qint64 orderId)
{
    std::optional<Order> o;
    {
        SqlQuery q(db());
        q.prepare(QStringLiteral("SELECT * FROM med_orders WHERE id = ?"));
        q.addBindValue(orderId);
        if (q.exec() && q.next()) o = orderFrom(q);
    }
    if (o) o->items = itemsOf(o->id);
    return o;
}

static qint64 openOrderId(qint64 patientId, bool create)
{
    {
        SqlQuery q(db());
        q.prepare(QStringLiteral("SELECT id FROM med_orders WHERE patient_id = ? AND status = 0 ORDER BY id LIMIT 1"));
        q.addBindValue(patientId);
        if (q.exec() && q.next()) return q.value(0).toLongLong();
    }
    if (!create) return 0;
    SqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO med_orders (patient_id, created_at, created_by, status) VALUES (?,?,?,0)"));
    q.addBindValue(patientId);
    q.addBindValue(QDateTime::currentDateTime().toString(kDt));
    q.addBindValue(Session::user());
    return q.exec() ? q.lastInsertId().toLongLong() : 0;
}

bool addToOpenOrder(qint64 patientId, const Medication &m, const QString &reason, QString *error)
{
    const qint64 oid = openOrderId(patientId, true);
    if (!oid) { if (error) *error = tr("Die Bestellliste konnte nicht angelegt werden."); return false; }
    {
        SqlQuery c(db());
        c.prepare(QStringLiteral("SELECT 1 FROM med_order_items WHERE order_id = ? AND medication_id = ?"));
        c.addBindValue(oid);
        c.addBindValue(m.id);
        if (c.exec() && c.next()) return true;   // steht schon drauf
    }
    SqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO med_order_items (patient_id, order_id, medication_id, name, substance, strength,"
                             " form, pzn, amount, reason) VALUES (?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(patientId);
    q.addBindValue(oid);
    q.addBindValue(m.id);
    q.addBindValue(m.name);
    q.addBindValue(m.substance);
    q.addBindValue(m.strength);
    q.addBindValue(m.form);
    q.addBindValue(m.pzn);
    q.addBindValue(m.extra.value(QStringLiteral("orderAmount")).toString(tr("1 Packung")));
    q.addBindValue(reason);
    if (!q.exec()) return failQ(q, error);
    Database::instance().notifyStockChanged(patientId);
    return true;
}

Order updateOpenOrder(qint64 patientId)
{
    for (const Medication &m : Database::instance().medications(patientId, false)) {
        if (!isTracked(m)) continue;
        const Forecast f = forecast(m);
        if ((f.state == OrderNow || f.state == Empty) && !f.onOrder)
            addToOpenOrder(patientId, m, f.reason(m.unit));
    }
    const qint64 oid = openOrderId(patientId, false);
    if (oid)
        if (auto o = order(oid); o && !o->items.isEmpty()) return *o;
    return Order{};
}

bool confirmOrdered(qint64 orderId, const QDateTime &at, const QString &by, const QString &note, QString *error)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE med_orders SET status = 1, ordered_at = ?, ordered_by = ?, note = ?,"
                             " modified_at = datetime('now','localtime') WHERE id = ?"));
    q.addBindValue(at.toString(kDt));
    q.addBindValue(by);
    q.addBindValue(note.isEmpty() ? QVariant() : QVariant(note));
    q.addBindValue(orderId);
    if (!q.exec()) return failQ(q, error);
    if (auto o = order(orderId)) Database::instance().notifyStockChanged(o->patientId);
    return true;
}

bool deliverItem(const OrderItem &item, double qty, const QDate &expiry, QString *error)
{
    const auto o = order(item.orderId);
    if (!o) return false;
    if (item.medicationId) {
        const auto m = medicationById(item.medicationId);
        if (m) {
            Package p;
            p.quantity = qty;
            p.expiry = expiry;
            p.received = QDate::currentDate();
            p.note = tr("Lieferung zur Bestellung vom %1").arg(QLocale().toString(o->createdAt.date(), QStringLiteral("dd.MM.yyyy")));
            if (!addPackage(*m, p, p.note, error)) return false;
        }
    }
    SqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE med_order_items SET delivered = 1, delivered_qty = ?, delivered_at = ?,"
                             " modified_at = datetime('now','localtime') WHERE id = ?"));
    q.addBindValue(qty);
    q.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    q.addBindValue(item.id);
    if (!q.exec()) return failQ(q, error);
    q.finish();
    // alle Positionen geliefert → Bestellung abgeschlossen (eine noch offene gilt damit auch als bestellt)
    const auto now = order(item.orderId);
    if (now && std::all_of(now->items.begin(), now->items.end(), [](const OrderItem &i) { return i.delivered; })) {
        SqlQuery u(db());
        u.prepare(QStringLiteral("UPDATE med_orders SET status = 2, delivered_at = ?,"
                                 " ordered_at = coalesce(ordered_at, ?), ordered_by = coalesce(ordered_by, ?),"
                                 " modified_at = datetime('now','localtime') WHERE id = ?"));
        u.addBindValue(QDate::currentDate().toString(Qt::ISODate));
        u.addBindValue(QDateTime::currentDateTime().toString(kDt));
        u.addBindValue(Session::user());
        u.addBindValue(item.orderId);
        u.exec();
    }
    Database::instance().notifyStockChanged(o->patientId);
    return true;
}

bool updateItemAmount(qint64 itemId, const QString &amount)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE med_order_items SET amount = ?, modified_at = datetime('now','localtime') WHERE id = ?"));
    q.addBindValue(amount);
    q.addBindValue(itemId);
    return q.exec();
}

bool removeItem(qint64 itemId)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM med_order_items WHERE id = ?"));
    q.addBindValue(itemId);
    return q.exec();
}

bool deleteOrder(qint64 orderId)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM med_orders WHERE id = ?"));
    q.addBindValue(orderId);
    return q.exec();
}

QStringList reminderLines(const Order &open)
{
    QStringList l;
    for (const OrderItem &i : open.items) {
        if (i.delivered) continue;
        const QString name = i.name + (i.strength.isEmpty() ? QString() : QLatin1Char(' ') + i.strength);
        if (i.reason.contains(tr("kein verwendbarer Bestand")))
            l << tr("Medikament %1: Packung verfallen – bitte Verordnung besorgen und bestellen!").arg(name);
        else if (i.reason.contains(tr("kein Bestand mehr")))
            l << tr("Medikament %1: ist aufgebraucht – bitte Verordnung besorgen und bestellen!").arg(name);
        else
            l << tr("Medikament %1: ist fast aufgebraucht – bitte Verordnung besorgen und bestellen!").arg(name);
    }
    return l;
}

} // namespace MedStock
