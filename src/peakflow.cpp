// VitaParms – Peak Flow: Sollwert und Warnhinweise
// SPDX-License-Identifier: MIT
#include "peakflow.h"

#include <QCoreApplication>
#include <QJsonValue>
#include <QLocale>
#include <QStringList>
#include <cmath>

namespace PeakFlow {

static QString tr(const char *s) { return QCoreApplication::translate("PeakFlow", s); }

// Normwerttabelle für Peak Flow (l/min) nach EGKS, übernommen von der ERS 1994
// (veröffentlicht u. a. von R. Cegla GmbH & Co. KG, Stand 12/2018).
// Zeilen: Alter 5, 10, 15 … 80 Jahre; Spalten: Körpergröße 135, 140 … 195 cm.
static const int kAges = 16, kHeights = 13;
static const short kMale[kAges][kHeights] = {
    { 282, 308, 334, 361, 387, 413, 439, 465, 492, 518, 544, 570, 597 },   //  5
    { 282, 308, 334, 361, 387, 413, 439, 465, 492, 518, 544, 570, 597 },   // 10
    { 282, 308, 334, 361, 387, 413, 439, 465, 492, 518, 544, 570, 597 },   // 15
    { 442, 461, 479, 497, 516, 534, 553, 571, 590, 608, 626, 645, 663 },   // 20
    { 442, 461, 479, 497, 516, 534, 553, 571, 590, 608, 626, 645, 663 },   // 25
    { 429, 448, 466, 484, 503, 521, 540, 558, 577, 595, 614, 632, 650 },   // 30
    { 416, 435, 453, 472, 490, 508, 527, 545, 564, 582, 601, 619, 638 },   // 35
    { 403, 422, 440, 459, 477, 496, 514, 532, 551, 569, 588, 606, 625 },   // 40
    { 390, 409, 427, 446, 464, 483, 501, 520, 538, 556, 575, 593, 612 },   // 45
    { 378, 396, 414, 433, 451, 470, 488, 507, 525, 544, 562, 580, 599 },   // 50
    { 365, 383, 402, 420, 438, 457, 475, 494, 512, 531, 549, 567, 586 },   // 55
    { 352, 370, 389, 407, 426, 444, 462, 481, 499, 518, 536, 555, 573 },   // 60
    { 339, 357, 376, 394, 413, 431, 449, 468, 486, 505, 523, 542, 560 },   // 65
    { 326, 344, 363, 381, 400, 418, 437, 455, 473, 492, 510, 529, 547 },   // 70
    { 313, 332, 350, 368, 387, 405, 424, 442, 461, 479, 497, 516, 534 },   // 75
    { 300, 319, 337, 355, 374, 392, 411, 429, 448, 466, 485, 503, 521 },   // 80
};
static const short kFemale[kAges][kHeights] = {
    { 282, 308, 334, 361, 387, 413, 439, 465, 492, 518, 544, 570, 597 },   //  5
    { 282, 308, 334, 361, 387, 413, 439, 465, 492, 518, 544, 570, 597 },   // 10
    { 282, 308, 334, 361, 387, 413, 439, 465, 492, 518, 544, 570, 597 },   // 15
    { 334, 350, 367, 383, 400, 416, 433, 449, 466, 482, 499, 515, 532 },   // 20
    { 334, 350, 367, 383, 400, 416, 433, 449, 466, 482, 499, 515, 532 },   // 25
    { 325, 341, 358, 374, 391, 407, 424, 440, 457, 473, 490, 506, 523 },   // 30
    { 316, 332, 349, 365, 382, 398, 415, 431, 448, 464, 481, 497, 514 },   // 35
    { 307, 323, 340, 356, 373, 389, 406, 422, 439, 455, 472, 488, 505 },   // 40
    { 298, 314, 331, 347, 364, 380, 397, 413, 430, 446, 463, 479, 496 },   // 45
    { 289, 305, 322, 338, 355, 371, 388, 404, 421, 437, 454, 470, 487 },   // 50
    { 280, 296, 313, 329, 346, 362, 379, 395, 412, 428, 445, 461, 478 },   // 55
    { 271, 287, 304, 320, 337, 353, 370, 386, 403, 419, 436, 452, 469 },   // 60
    { 262, 278, 295, 311, 328, 344, 361, 377, 394, 410, 427, 443, 460 },   // 65
    { 253, 269, 286, 302, 319, 335, 352, 368, 385, 401, 418, 434, 451 },   // 70
    { 244, 260, 277, 293, 310, 326, 343, 359, 376, 392, 409, 425, 442 },   // 75
    { 235, 251, 268, 284, 301, 317, 334, 350, 367, 383, 400, 416, 433 },   // 80
};

std::optional<int> normValue(QChar sex, double age, double height)
{
    const short (*t)[kHeights] = sex == QLatin1Char('m') ? kMale : sex == QLatin1Char('w') ? kFemale : nullptr;
    if (!t || age < kMinAge || height < kMinHeight || height > kMaxHeight) return std::nullopt;
    age = std::min(age, double(kMaxAge));
    const double ai = (age - kMinAge) / 5.0, hi = (height - kMinHeight) / 5.0;
    const int a0 = std::min(int(ai), kAges - 1), a1 = std::min(a0 + 1, kAges - 1);
    const int h0 = std::min(int(hi), kHeights - 1), h1 = std::min(h0 + 1, kHeights - 1);
    const double fa = ai - a0, fh = hi - h0;
    const double top = t[a0][h0] + (t[a0][h1] - t[a0][h0]) * fh;
    const double bottom = t[a1][h0] + (t[a1][h1] - t[a1][h0]) * fh;
    return int(std::lround(top + (bottom - top) * fa));
}

int personalBest(const Patient &p)
{
    const int v = p.extra.value(QLatin1String(kBestKey)).toInt();
    return v > 0 ? v : 0;
}

void setPersonalBest(Patient &p, int v)
{
    if (v > 0) p.extra.insert(QLatin1String(kBestKey), v);
    else p.extra.remove(QLatin1String(kBestKey));
}

QChar sexFromField(const QString &sex)
{
    return sex == QLatin1String("m") || sex == QLatin1String("w") ? sex.at(0) : QChar();
}

QChar bioSexSetting(const Patient &p)
{
    return sexFromField(p.extra.value(QLatin1String(kBioSexKey)).toString());
}

QChar biologicalSex(const Patient &p)
{
    const QChar s = bioSexSetting(p);
    return s.isNull() ? sexFromField(p.sex) : s;
}

void setBiologicalSex(Patient &p, QChar sex)
{
    if ((sex == QLatin1Char('m') || sex == QLatin1Char('w')) && sex != sexFromField(p.sex))
        p.extra.insert(QLatin1String(kBioSexKey), QString(sex));
    else
        p.extra.remove(QLatin1String(kBioSexKey));
}

bool bioSexDiffers(const Patient &p)
{
    const QChar b = bioSexSetting(p);
    return !b.isNull() && b != sexFromField(p.sex);
}

// genaues Alter in Jahren (für die Interpolation zwischen den Tabellenzeilen)
static double exactAge(const Patient &p, const QDate &day)
{
    const int years = p.ageAt(day);
    if (years < 0) return -1;
    const QDate last = p.birthDate.addYears(years);
    const double span = std::max<qint64>(1, last.daysTo(last.addYears(1)));
    return years + last.daysTo(day) / span;
}

Reference normReference(const Patient &p, const QDate &day, std::optional<double> height)
{
    Reference r;
    QStringList miss;
    const QChar sex = biologicalSex(p);
    if (sex.isNull())
        miss << tr("das biologische Geschlecht (Stammblatt, Reiter „Körper & Aufnahme“)");
    if (!p.birthDate.isValid()) miss << tr("das Geburtsdatum im Stammblatt");
    if (!height) miss << tr("die Körpergröße (aus einer Messung)");
    if (!miss.isEmpty()) {
        r.missing = tr("Für den Peak-Flow-Sollwert fehlt %1.").arg(QLocale().createSeparatedList(miss));
        return r;
    }
    const double age = exactAge(p, day.isValid() ? day : QDate::currentDate());
    if (age < kMinAge) {
        r.missing = tr("Die Normwerttabelle beginnt mit 5 Jahren – bitte den persönlichen Bestwert eintragen.");
        return r;
    }
    if (*height < kMinHeight || *height > kMaxHeight) {
        r.missing = tr("Die Normwerttabelle gilt für 135 bis 195 cm Körpergröße – bitte den persönlichen Bestwert eintragen.");
        return r;
    }
    const std::optional<int> v = normValue(sex, age, *height);
    if (!v) return r;
    r.source = Source::Norm;
    r.value = *v;
    r.capped = age > kMaxAge;
    r.basis = tr("ERS 1994 für %1 Jahre, %2, %3 cm")
                  .arg(int(age))
                  .arg(sex == QLatin1Char('m') ? tr("männlich") : tr("weiblich"))
                  .arg(QLocale().toString(*height, 'f', 0));
    if (r.capped) r.basis += tr(", Tabellenwert für 80 Jahre");
    r.description = tr("Sollwert (%1)").arg(r.basis);
    return r;
}

Reference reference(const Patient &p, const QDate &day, std::optional<double> height)
{
    if (const int best = personalBest(p)) {
        Reference r;
        r.source = Source::PersonalBest;
        r.value = best;
        r.description = tr("persönlicher Bestwert");
        return r;
    }
    return normReference(p, day, height);
}

QString limitsText(const Reference &r)
{
    if (r.value <= 0) return {};
    const QString what = r.source == Source::PersonalBest ? tr("persönlicher Bestwert %1 l/min").arg(r.value)
                                                          : tr("Sollwert %1 l/min (%2)").arg(r.value).arg(r.basis);
    return tr("Bezugswert: %1 – gelb unter %2, rot unter %3 l/min.")
        .arg(what).arg(int(std::ceil(r.value * 0.8))).arg(int(std::ceil(r.value * 0.5)));
}

Zone zoneFor(int percent)
{
    if (percent >= 80) return Zone::Green;
    if (percent >= 50) return Zone::Yellow;
    return Zone::Red;
}

Assessment assess(const Patient &p, int value, const QDate &day, std::optional<double> height)
{
    Assessment a;
    a.ref = reference(p, day, height);
    if (value <= 0 || a.ref.value <= 0) return a;
    a.percent = int(std::lround(100.0 * value / a.ref.value));
    a.zone = zoneFor(a.percent);
    a.text = tr("%1 l/min = %2 % des %3 (%4 l/min) – %5.")
                 .arg(value).arg(a.percent)
                 .arg(a.ref.source == Source::PersonalBest ? tr("persönlichen Bestwerts") : tr("Sollwerts"))
                 .arg(a.ref.value).arg(zoneName(a.zone));
    switch (a.zone) {
    case Zone::Yellow:
        a.advice = tr("Achtung: Die Atemwege sind verengt. Bitte nach dem mit Ihrem Arzt vereinbarten "
                      "Asthma-Aktionsplan handeln; bei anhaltenden oder zunehmenden Beschwerden ärztlichen Rat einholen.");
        break;
    case Zone::Red:
        a.advice = tr("Warnung: Der Wert liegt unter 50 %. Sofort die Notfallmedikation nach Plan anwenden und "
                      "ärztliche Hilfe holen – bei Atemnot Notruf 112.");
        break;
    default: break;
    }
    return a;
}

QString zoneName(Zone z)
{
    switch (z) {
    case Zone::Green:  return tr("grüner Bereich");
    case Zone::Yellow: return tr("gelber Bereich");
    case Zone::Red:    return tr("roter Bereich");
    default:           return {};
    }
}

QColor zoneColor(Zone z, bool dark)
{
    switch (z) {
    case Zone::Green:  return dark ? QColor(0x6F, 0xD0, 0x8C) : QColor(0x1B, 0x7F, 0x3B);
    case Zone::Yellow: return dark ? QColor(0xF2, 0xB2, 0x4C) : QColor(0xB0, 0x5A, 0x00);
    case Zone::Red:    return dark ? QColor(0xFF, 0x8A, 0x80) : QColor(0xB3, 0x26, 0x1E);
    default:           return {};
    }
}

QString zoneKey(Zone z)
{
    switch (z) {
    case Zone::Green:  return QStringLiteral("green");
    case Zone::Yellow: return QStringLiteral("yellow");
    case Zone::Red:    return QStringLiteral("red");
    default:           return {};
    }
}

} // namespace PeakFlow
