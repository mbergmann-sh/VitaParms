// SPDX-License-Identifier: MIT
#include "vaccinations.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace Vaccinations {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("Vaccinations", s); }

QJsonObject toJson(const Vaccination &v)
{
    QJsonObject o{ { QStringLiteral("uid"), v.uid } };
    if (v.date.isValid()) o.insert(QStringLiteral("date"), v.date.toString(Qt::ISODate));
    auto put = [&](const char *k, const QString &s) { if (!s.trimmed().isEmpty()) o.insert(QLatin1String(k), s.trimmed()); };
    put("against", v.against); put("product", v.product); put("batch", v.batch); put("doctor", v.doctor); put("note", v.note);
    return o;
}

Vaccination fromJson(const QJsonObject &o)
{
    Vaccination v;
    auto get = [&](const char *k) { return o.value(QLatin1String(k)).toString(); };
    v.uid = get("uid");
    v.date = QDate::fromString(get("date"), Qt::ISODate);
    v.against = get("against"); v.product = get("product"); v.batch = get("batch"); v.doctor = get("doctor"); v.note = get("note");
    if (v.uid.isEmpty()) v.uid = newUid();
    return v;
}
} // namespace

bool Vaccination::isEmpty() const
{
    return !date.isValid() && against.trimmed().isEmpty() && product.trimmed().isEmpty() && batch.trimmed().isEmpty();
}

QString Vaccination::summary() const
{
    QString s = date.isValid() ? QLocale().toString(date, QStringLiteral("dd.MM.yyyy")) : QStringLiteral("—");
    if (!against.isEmpty()) s += QLatin1Char(' ') + against;
    QStringList more;
    if (!product.isEmpty()) more << product;
    if (!batch.isEmpty()) more << tr("Ch.-B. %1").arg(batch);
    if (!more.isEmpty()) s += QStringLiteral(" – ") + more.join(QStringLiteral(", "));
    return s;
}

QString key() { return QStringLiteral("vaccinations"); }

QString newUid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

void sort(QList<Vaccination> &l)
{
    std::stable_sort(l.begin(), l.end(), [](const Vaccination &a, const Vaccination &b) {
        if (a.date != b.date) return a.date > b.date;         // ohne Datum ans Ende
        return QString::localeAwareCompare(a.against, b.against) < 0;
    });
}

QList<Vaccination> list(const Patient &p)
{
    QList<Vaccination> l;
    for (const QJsonValue &v : p.extra.value(key()).toArray())
        if (v.isObject()) l << fromJson(v.toObject());
    sort(l);
    return l;
}

void store(Patient &p, QList<Vaccination> l)
{
    l.erase(std::remove_if(l.begin(), l.end(), [](const Vaccination &v) { return v.isEmpty(); }), l.end());
    sort(l);
    QJsonArray a;
    for (Vaccination &v : l) {
        if (v.uid.isEmpty()) v.uid = newUid();
        a.append(toJson(v));
    }
    if (a.isEmpty()) p.extra.remove(key());
    else p.extra.insert(key(), a);
}

QStringList againstSuggestions(const QList<Vaccination> &existing)
{
    QStringList l = {
        tr("Tetanus, Diphtherie, Pertussis"), tr("Tetanus, Diphtherie"), tr("Tetanus"),
        tr("Poliomyelitis"), tr("Influenza (Grippe)"), tr("COVID-19"), tr("Pneumokokken"),
        tr("Herpes zoster (Gürtelrose)"), tr("RSV"), tr("FSME"), tr("Hepatitis A"), tr("Hepatitis B"),
        tr("Hepatitis A und B"), tr("Masern, Mumps, Röteln"), tr("Varizellen (Windpocken)"),
        tr("Meningokokken"), tr("HPV"), tr("Haemophilus influenzae Typ b (Hib)"), tr("Tollwut"),
        tr("Typhus"), tr("Gelbfieber"), tr("Japanische Enzephalitis"), tr("Cholera")
    };
    for (const Vaccination &v : existing)
        if (!v.against.trimmed().isEmpty() && !l.contains(v.against.trimmed())) l << v.against.trimmed();
    return l;
}

QStringList productSuggestions(const QList<Vaccination> &existing)
{
    QStringList l;
    for (const Vaccination &v : existing)
        if (!v.product.trimmed().isEmpty() && !l.contains(v.product.trimmed())) l << v.product.trimmed();
    std::sort(l.begin(), l.end(), [](const QString &a, const QString &b) { return QString::localeAwareCompare(a, b) < 0; });
    return l;
}

QStringList latestPerAgainst(const QList<Vaccination> &l)
{
    QStringList out;
    QSet<QString> seen;
    QList<Vaccination> s = l;
    sort(s);                                            // neueste zuerst → erste Fundstelle je Eintrag
    for (const Vaccination &v : s) {
        const QString k = v.against.trimmed().toLower();
        if (k.isEmpty() || seen.contains(k) || !v.date.isValid()) continue;
        seen.insert(k);
        out << QStringLiteral("%1: %2").arg(v.against.trimmed(), QLocale().toString(v.date, QStringLiteral("dd.MM.yyyy")));
    }
    return out;
}

} // namespace Vaccinations
