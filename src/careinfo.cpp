// SPDX-License-Identifier: MIT
#include "careinfo.h"
#include "contacts.h"
#include <algorithm>
#include <QJsonObject>

#include <QCoreApplication>
#include <QLocale>

namespace CareInfo {

static QString tr(const char *s) { return QCoreApplication::translate("CareInfo", s); }

const QList<Mark> &marks()
{
    // Merkzeichen nach § 3 Schwerbehindertenausweisverordnung
    static const QList<Mark> m = {
        { QStringLiteral("G"),      tr("erheblich beeinträchtigt in der Bewegungsfähigkeit im Straßenverkehr") },
        { QStringLiteral("aG"),     tr("außergewöhnlich gehbehindert") },
        { QStringLiteral("B"),      tr("ständige Begleitung erforderlich (Begleitperson fährt kostenlos)") },
        { QStringLiteral("H"),      tr("hilflos") },
        { QStringLiteral("Bl"),     tr("blind") },
        { QStringLiteral("Gl"),     tr("gehörlos") },
        { QStringLiteral("TBl"),    tr("taubblind") },
        { QStringLiteral("RF"),     tr("Ermäßigung des Rundfunkbeitrags") },
        { QStringLiteral("1. Kl."), tr("Benutzung der 1. Wagenklasse mit Fahrschein der 2. Klasse") },
    };
    return m;
}

QString careLevelName(int level)
{
    return level > 0 ? tr("Pflegegrad %1").arg(level) : tr("kein Pflegegrad");
}

QString careLevelText(const Patient &p)
{
    if (p.careLevel <= 0) return {};
    QString t = careLevelName(p.careLevel);
    if (p.careReview.isValid())
        t += tr(", nächste Überprüfung %1").arg(QLocale().toString(p.careReview, QStringLiteral("dd.MM.yyyy")));
    return t;
}

QString disabilityText(const Patient &p)
{
    if (p.disabilityDegree <= 0) return {};
    QString t = tr("GdB %1").arg(p.disabilityDegree);
    if (!p.disabilityMarks.isEmpty())
        t += tr(", Merkzeichen %1").arg(p.disabilityMarks.join(QStringLiteral(", ")));
    t += p.disabilityValidUntil.isValid()
             ? tr(" – gültig bis %1").arg(QLocale().toString(p.disabilityValidUntil, QStringLiteral("dd.MM.yyyy")))
             : tr(" – unbefristet");
    return t;
}

bool careReviewSoon(const Patient &p, int days)
{
    return p.careLevel > 0 && p.careReview.isValid() && QDate::currentDate().daysTo(p.careReview) < days;
}

bool disabilityExpiresSoon(const Patient &p, int days)
{
    return p.disabilityDegree > 0 && p.disabilityValidUntil.isValid()
           && QDate::currentDate().daysTo(p.disabilityValidUntil) < days;
}

// ------------------------------------------------------------------ Vorsorgedokumente

QString advanceKey(int kind)
{
    static const char *k[] = { "pv", "vv", "bv" };
    return QString::fromLatin1(k[std::clamp(kind, 0, AdvanceCount - 1)]);
}

QString advanceName(int kind)
{
    switch (kind) {
    case PowerOfAttorney: return QCoreApplication::translate("CareInfo", "Vorsorgevollmacht");
    case CareProxy:       return QCoreApplication::translate("CareInfo", "Betreuungsvollmacht");
    default:              return QCoreApplication::translate("CareInfo", "Patientenverfügung");
    }
}

QString advancePersonLabel(int kind)
{
    switch (kind) {
    case PowerOfAttorney: return QCoreApplication::translate("CareInfo", "Bevollmächtigte(r)");
    case CareProxy:       return QCoreApplication::translate("CareInfo", "Betreuer(in)");
    default:              return QCoreApplication::translate("CareInfo", "Vertrauensperson");
    }
}

QString advanceDocIdKey(int kind)
{
    return advanceKey(kind) + QStringLiteral("DocId");
}

AdvanceDoc advanceDoc(const Patient &p, int kind)
{
    AdvanceDoc d;
    const QJsonObject o = p.extra.value(QStringLiteral("advance")).toObject().value(advanceKey(kind)).toObject();
    d.present = o.value(QStringLiteral("present")).toBool();
    d.person = o.value(QStringLiteral("person")).toString();
    d.address = o.value(QStringLiteral("address")).toString();
    d.phone = o.value(QStringLiteral("phone")).toString();
    d.validUntil = QDate::fromString(o.value(QStringLiteral("valid")).toString(), Qt::ISODate);
    d.docId = p.extra.value(advanceDocIdKey(kind)).toVariant().toLongLong();
    return d;
}

void setAdvanceDoc(Patient &p, int kind, const AdvanceDoc &d)
{
    QJsonObject all = p.extra.value(QStringLiteral("advance")).toObject();
    if (!d.present && d.person.isEmpty() && d.address.isEmpty() && d.phone.isEmpty() && !d.docId) {
        all.remove(advanceKey(kind));
    } else {
        QJsonObject o;
        o[QStringLiteral("present")] = d.present;
        if (!d.person.isEmpty()) o[QStringLiteral("person")] = d.person;
        if (!d.address.isEmpty()) o[QStringLiteral("address")] = d.address;
        if (!d.phone.isEmpty()) o[QStringLiteral("phone")] = d.phone;
        if (d.validUntil.isValid()) o[QStringLiteral("valid")] = d.validUntil.toString(Qt::ISODate);
        all[advanceKey(kind)] = o;
    }
    if (all.isEmpty()) p.extra.remove(QStringLiteral("advance"));
    else p.extra[QStringLiteral("advance")] = all;
    if (d.docId) p.extra[advanceDocIdKey(kind)] = d.docId;
    else p.extra.remove(advanceDocIdKey(kind));
}

QString advanceLine(const Patient &p, int kind)
{
    const QString d = advanceDetails(p, kind);
    return d.isEmpty() ? QString() : advanceName(kind) + QStringLiteral(": ") + d;
}

QString advanceDetails(const Patient &p, int kind)
{
    const AdvanceDoc d = advanceDoc(p, kind);
    if (!d.present) return {};
    QStringList who;
    if (!d.person.isEmpty()) who << d.person;
    if (!d.address.isEmpty()) who << d.address;
    if (!d.phone.isEmpty()) who << QCoreApplication::translate("CareInfo", "Tel. %1").arg(d.phone);
    QString s = who.isEmpty() ? QCoreApplication::translate("CareInfo", "liegt vor") : who.join(QStringLiteral(", "));
    s += d.validUntil.isValid()
             ? QCoreApplication::translate("CareInfo", " – gültig bis %1").arg(QLocale().toString(d.validUntil, QStringLiteral("dd.MM.yyyy")))
             : QCoreApplication::translate("CareInfo", " – unbefristet");
    if (d.docId) s += QCoreApplication::translate("CareInfo", " (PDF hinterlegt)");
    return s;
}

QString advanceShort(const Patient &p)
{
    QStringList l;
    for (int k = 0; k < AdvanceCount; ++k) {
        const AdvanceDoc d = advanceDoc(p, k);
        if (d.present) l << advanceName(k) + (d.docId ? QCoreApplication::translate("CareInfo", " (PDF)") : QString());
    }
    return l.join(QStringLiteral(", "));
}

bool isInternalExtraKey(const QString &key)
{
    static const QStringList keys = { QStringLiteral("advance"), QStringLiteral("pvDocId"), QStringLiteral("vvDocId"),
                                      QStringLiteral("bvDocId"), QStringLiteral("prepDays"), QStringLiteral("prepWeekday"),
                                      Contacts::contactsKey(), QStringLiteral("peakFlowBest"), QStringLiteral("bioSex") };
    // die alten Kontakt-Zusatzfelder werden aus der Kontaktliste mitgeschrieben und dort angezeigt
    return keys.contains(key) || Contacts::isLegacyKey(key);
}

bool advanceExpiresSoon(const Patient &p, int days)
{
    for (int k = 0; k < AdvanceCount; ++k) {
        const AdvanceDoc d = advanceDoc(p, k);
        if (d.present && d.validUntil.isValid() && QDate::currentDate().daysTo(d.validUntil) < days) return true;
    }
    return false;
}

} // namespace CareInfo
