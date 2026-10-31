// SPDX-License-Identifier: MIT
#include "contacts.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace Contacts {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("Contacts", s); }

const QString kInsurance = QStringLiteral("Krankenkasse");
const QString kDoctor    = QStringLiteral("Hausarztpraxis");
const QString kDocPhone  = QStringLiteral("Praxis-Telefon");
const QString kEmergency = QStringLiteral("Notfallkontakt");

QJsonObject toJson(const Contact &c)
{
    QJsonObject o{ { QStringLiteral("uid"), c.uid }, { QStringLiteral("kind"), c.kind } };
    auto put = [&](const char *k, const QString &v) { if (!v.trimmed().isEmpty()) o.insert(QLatin1String(k), v.trimmed()); };
    put("name", c.name); put("specialty", c.specialty); put("person", c.person); put("insuranceNo", c.insuranceNo);
    put("street", c.street); put("zip", c.zip); put("city", c.city);
    put("phone", c.phone); put("mobile", c.mobile); put("email", c.email); put("website", c.website);
    put("note", c.note);
    return o;
}

Contact fromJson(const QJsonObject &o)
{
    Contact c;
    auto get = [&](const char *k) { return o.value(QLatin1String(k)).toString(); };
    c.uid = get("uid");
    c.kind = std::clamp(o.value(QStringLiteral("kind")).toInt(Other), 0, int(KindCount) - 1);
    c.name = get("name"); c.specialty = get("specialty"); c.person = get("person"); c.insuranceNo = get("insuranceNo");
    c.street = get("street"); c.zip = get("zip"); c.city = get("city");
    c.phone = get("phone"); c.mobile = get("mobile"); c.email = get("email"); c.website = get("website");
    c.note = get("note");
    if (c.uid.isEmpty()) c.uid = newUid();
    return c;
}

QStringList nonEmpty(const QStringList &l)
{
    QStringList r;
    for (const QString &s : l) if (!s.trimmed().isEmpty()) r << s.trimmed();
    return r;
}

// Alte Zusatzfelder → Kontakte (einmalig, solange es noch keine Kontaktliste gibt)
QList<Contact> fromLegacy(const Patient &p)
{
    QList<Contact> l;
    const QString ins = p.extra.value(kInsurance).toString().trimmed();
    if (!ins.isEmpty()) {
        Contact c; c.uid = newUid(); c.kind = Insurance; c.name = ins;
        l << c;
    }
    const QString doc = p.extra.value(kDoctor).toString().trimmed();
    const QString docPhone = p.extra.value(kDocPhone).toString().trimmed();
    if (!doc.isEmpty() || !docPhone.isEmpty()) {
        Contact c; c.uid = newUid(); c.kind = FamilyDoctor; c.name = doc; c.phone = docPhone;
        l << c;
    }
    const QString em = p.extra.value(kEmergency).toString().trimmed();
    if (!em.isEmpty()) {
        Contact c; c.uid = newUid(); c.kind = Emergency;
        // „Max Mustermann (Ehemann), 0170 7654321“ → Name, Beziehung, Telefon, soweit erkennbar
        QString rest = em;
        static const QRegularExpression phoneRe(QStringLiteral("(\\+?[0-9][0-9 /()\\-]{5,}[0-9])"));
        if (const auto m = phoneRe.match(rest); m.hasMatch()) {
            const QString num = m.captured(1).trimmed();
            // Mobilfunk: 015x/016x/017x (DE), 07 (UK), +4915…/+4916…/+4917…, +447…
            static const QRegularExpression mobRe(QStringLiteral("^(01[5-7]|07|\\+491[5-7]|\\+447)"));
            (mobRe.match(num).hasMatch() ? c.mobile : c.phone) = num;
            rest.remove(m.capturedStart(1), m.capturedLength(1));
        }
        static const QRegularExpression relRe(QStringLiteral("\\(([^)]*)\\)"));
        if (const auto m = relRe.match(rest); m.hasMatch()) {
            c.person = m.captured(1).trimmed();
            rest.remove(m.capturedStart(0), m.capturedLength(0));
        }
        rest = rest.simplified();
        while (!rest.isEmpty() && QStringLiteral(",;–-").contains(rest.back())) rest.chop(1);
        c.name = rest.trimmed();
        if (c.name.isEmpty()) c.name = em.split(QLatin1Char('\n')).value(0);
        l << c;
    }
    return l;
}
} // namespace

bool Contact::isEmpty() const
{
    return nonEmpty({ name, specialty, person, insuranceNo, street, zip, city, phone, mobile, email, website, note }).isEmpty();
}

QString Contact::address() const
{
    return nonEmpty({ street, nonEmpty({ zip, city }).join(QLatin1Char(' ')) }).join(QStringLiteral(", "));
}

QString Contact::title() const
{
    return nonEmpty({ name, specialty }).join(QStringLiteral(" – "));
}

QString Contact::searchText() const
{
    return nonEmpty({ kindName(), name, specialty, person, insuranceNo, street, zip, city, phone, mobile, email, website, note })
        .join(QLatin1Char(' '));
}

QString Contact::summary() const
{
    QStringList l{ title() };
    if (!person.trimmed().isEmpty()) l << person.trimmed();
    if (!insuranceNo.trimmed().isEmpty()) l << QCoreApplication::translate("Contacts", "Vers.-Nr. %1").arg(insuranceNo.trimmed());
    l << address();
    if (!phone.trimmed().isEmpty()) l << QCoreApplication::translate("Contacts", "Tel. %1").arg(phone.trimmed());
    if (!mobile.trimmed().isEmpty()) l << QCoreApplication::translate("Contacts", "mobil %1").arg(mobile.trimmed());
    l << email << website;
    return nonEmpty(l).join(QStringLiteral(", "));
}

QList<int> kindOrder()
{
    return { Insurance, FamilyDoctor, Specialist, Clinic, CareService, Pharmacy, Relative, Emergency, Other };
}

QString kindName(int kind)
{
    switch (kind) {
    case Insurance:    return tr("Krankenkasse");
    case FamilyDoctor: return tr("Hausarztpraxis");
    case Specialist:   return tr("Facharzt");
    case Clinic:       return tr("Klinik");
    case CareService:  return tr("Pflegedienst");
    case Pharmacy:     return tr("Apotheke");
    case Emergency:    return tr("Notfallkontakt");
    case Relative:     return tr("Angehörige/r");
    default:           return tr("Sonstiger Kontakt");
    }
}

QString contactsKey() { return QStringLiteral("contacts"); }

bool isLegacyKey(const QString &key)
{
    return key == kInsurance || key == kDoctor || key == kDocPhone || key == kEmergency;
}

QString newUid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

void sort(QList<Contact> &contacts)
{
    std::stable_sort(contacts.begin(), contacts.end(), [](const Contact &a, const Contact &b) {
        if (a.kind != b.kind) return kindOrder().indexOf(a.kind) < kindOrder().indexOf(b.kind);
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
}

QList<Contact> list(const Patient &p)
{
    QList<Contact> l;
    if (p.extra.contains(contactsKey())) {
        for (const QJsonValue &v : p.extra.value(contactsKey()).toArray())
            if (v.isObject()) l << fromJson(v.toObject());
    } else {
        l = fromLegacy(p);
    }
    sort(l);
    return l;
}

void store(Patient &p, QList<Contact> contacts)
{
    contacts.erase(std::remove_if(contacts.begin(), contacts.end(), [](const Contact &c) { return c.isEmpty(); }),
                   contacts.end());
    sort(contacts);
    QJsonArray a;
    for (Contact &c : contacts) {
        if (c.uid.isEmpty()) c.uid = newUid();
        a.append(toJson(c));
    }
    p.extra.insert(contactsKey(), a);
    // alte Zusatzfelder für ältere Programmstände
    auto setOrRemove = [&](const QString &k, const QString &v) {
        if (v.trimmed().isEmpty()) p.extra.remove(k); else p.extra.insert(k, v.trimmed());
    };
    Contact ins, doc, em;
    for (const Contact &c : std::as_const(contacts)) {
        if (c.kind == Insurance && ins.uid.isEmpty()) ins = c;
        if (c.kind == FamilyDoctor && doc.uid.isEmpty()) doc = c;
        if (c.kind == Emergency && em.uid.isEmpty()) em = c;
    }
    setOrRemove(kInsurance, ins.name);
    setOrRemove(kDoctor, doc.title());
    setOrRemove(kDocPhone, doc.phone.isEmpty() ? doc.mobile : doc.phone);
    QString emText = em.name;
    if (!em.person.isEmpty()) emText += QStringLiteral(" (") + em.person + QLatin1Char(')');
    if (const QString tel = nonEmpty({ em.mobile, em.phone }).join(QStringLiteral(", ")); !tel.isEmpty())
        emText += (emText.isEmpty() ? QString() : QStringLiteral(", ")) + tel;
    setOrRemove(kEmergency, emText);
}

Contact first(const Patient &p, int kind)
{
    for (const Contact &c : list(p))
        if (c.kind == kind) return c;
    return {};
}

QString insuranceLine(const Patient &p)
{
    const Contact c = first(p, Insurance);
    return nonEmpty({ c.name, c.insuranceNo.isEmpty() ? QString() : tr("Vers.-Nr. %1").arg(c.insuranceNo) })
        .join(QStringLiteral(", "));
}

QString familyDoctorLine(const Patient &p)
{
    const Contact c = first(p, FamilyDoctor);
    return nonEmpty({ c.title(), c.phone.isEmpty() ? c.mobile : c.phone }).join(QStringLiteral(", "));
}

} // namespace Contacts
