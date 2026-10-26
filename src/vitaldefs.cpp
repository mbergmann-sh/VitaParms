// SPDX-License-Identifier: MIT
#include "vitaldefs.h"
#include "appsettings.h"

#include <QCoreApplication>
#include <QLocale>

namespace VitalDefs {

static QString tr(const char *s) { return QCoreApplication::translate("VitalDefs", s); }

QList<VitalKey> all()
{
    return { VitalKey::Pulse, VitalKey::Systolic, VitalKey::Diastolic,
             VitalKey::Temperature, VitalKey::SpO2, VitalKey::PeakFlow, VitalKey::Glucose,
             VitalKey::Weight, VitalKey::Height, VitalKey::Bmi };
}

QString key(VitalKey k)
{
    switch (k) {
    case VitalKey::Pulse:       return QStringLiteral("pulse");
    case VitalKey::Systolic:    return QStringLiteral("systolic");
    case VitalKey::Diastolic:   return QStringLiteral("diastolic");
    case VitalKey::Temperature: return QStringLiteral("temperature");
    case VitalKey::SpO2:        return QStringLiteral("spo2");
    case VitalKey::PeakFlow:    return QStringLiteral("peakflow");
    case VitalKey::Glucose:     return QStringLiteral("glucose");
    case VitalKey::Weight:      return QStringLiteral("weight");
    case VitalKey::Height:      return QStringLiteral("height");
    case VitalKey::Bmi:         return QStringLiteral("bmi");
    }
    return {};
}

QString label(VitalKey k)
{
    switch (k) {
    case VitalKey::Pulse:       return tr("Puls");
    case VitalKey::Systolic:    return tr("Blutdruck systolisch");
    case VitalKey::Diastolic:   return tr("Blutdruck diastolisch");
    case VitalKey::Temperature: return tr("Körpertemperatur");
    case VitalKey::SpO2:        return tr("Sauerstoffsättigung (SpO₂)");
    case VitalKey::PeakFlow:    return tr("Peak Flow");
    case VitalKey::Glucose:     return tr("Blutzucker");
    case VitalKey::Weight:      return tr("Gewicht");
    case VitalKey::Height:      return tr("Körpergröße");
    case VitalKey::Bmi:         return tr("BMI");
    }
    return {};
}

QString shortLabel(VitalKey k)
{
    switch (k) {
    case VitalKey::Pulse:       return tr("Puls");
    case VitalKey::Systolic:    return tr("RR sys.");
    case VitalKey::Diastolic:   return tr("RR dia.");
    case VitalKey::Temperature: return tr("Temp.");
    case VitalKey::SpO2:        return tr("SpO₂");
    case VitalKey::PeakFlow:    return tr("Peak Flow");
    case VitalKey::Glucose:     return tr("BZ");
    case VitalKey::Weight:      return tr("Gewicht");
    case VitalKey::Height:      return tr("Größe");
    case VitalKey::Bmi:         return tr("BMI");
    }
    return {};
}

QString unit(VitalKey k)
{
    switch (k) {
    case VitalKey::Pulse:       return tr("/min");
    case VitalKey::Systolic:
    case VitalKey::Diastolic:   return tr("mmHg");
    case VitalKey::Temperature: return tr("°C");
    case VitalKey::SpO2:        return tr("%");
    case VitalKey::PeakFlow:    return tr("l/min");
    case VitalKey::Glucose:
        return AppSettings::glucoseUnit() == AppSettings::GlucoseUnit::MmolL
                   ? tr("mmol/l") : tr("mg/dl");
    case VitalKey::Weight:      return tr("kg");
    case VitalKey::Height:      return tr("cm");
    case VitalKey::Bmi:         return tr("kg/m²");
    }
    return {};
}

int decimals(VitalKey k)
{
    switch (k) {
    case VitalKey::Temperature: return 1;
    case VitalKey::Glucose:
        return AppSettings::glucoseUnit() == AppSettings::GlucoseUnit::MmolL ? 1 : 0;
    case VitalKey::Weight:      return 1;
    case VitalKey::Height:      return 0;
    case VitalKey::Bmi:         return 1;
    default:                    return 0;
    }
}

// Okabe-Ito-Palette: auch bei Rot-Grün-Schwäche unterscheidbar
QColor color(VitalKey k)
{
    switch (k) {
    case VitalKey::Systolic:    return QColor(0xD5, 0x5E, 0x00); // Zinnober
    case VitalKey::Diastolic:   return QColor(0x00, 0x72, 0xB2); // Blau
    case VitalKey::Pulse:       return QColor(0x00, 0x9E, 0x73); // Blaugrün
    case VitalKey::Temperature: return QColor(0xCC, 0x79, 0xA7); // Purpur
    case VitalKey::SpO2:        return QColor(0x56, 0xB4, 0xE9); // Himmelblau
    case VitalKey::PeakFlow:    return QColor(0x00, 0x9E, 0x73); // Blaugrün (eigener Bereich)
    case VitalKey::Glucose:     return QColor(0xE6, 0x9F, 0x00); // Orange
    case VitalKey::Weight:      return QColor(0x33, 0x33, 0x33); // Dunkelgrau
    case VitalKey::Height:      return QColor(0x88, 0x88, 0x88);
    case VitalKey::Bmi:         return QColor(0x00, 0x72, 0xB2);
    }
    return Qt::black;
}

MarkerShape marker(VitalKey k)
{
    switch (k) {
    case VitalKey::Systolic:    return MarkerShape::Triangle;
    case VitalKey::Diastolic:   return MarkerShape::TriangleDown;
    case VitalKey::Pulse:       return MarkerShape::Circle;
    case VitalKey::Temperature: return MarkerShape::Diamond;
    case VitalKey::SpO2:        return MarkerShape::Square;
    case VitalKey::PeakFlow:    return MarkerShape::Diamond;
    case VitalKey::Glucose:     return MarkerShape::Circle;
    case VitalKey::Weight:      return MarkerShape::Square;
    case VitalKey::Height:      return MarkerShape::Cross;
    case VitalKey::Bmi:         return MarkerShape::Diamond;
    }
    return MarkerShape::Circle;
}

double glucoseToDisplay(double mgdl)
{
    return AppSettings::glucoseUnit() == AppSettings::GlucoseUnit::MmolL
               ? mgdl / kMgDlPerMmolL : mgdl;
}

double glucoseFromDisplay(double v)
{
    return AppSettings::glucoseUnit() == AppSettings::GlucoseUnit::MmolL
               ? v * kMgDlPerMmolL : v;
}

std::optional<double> value(const VitalRecord &r, VitalKey k)
{
    auto i = [](const std::optional<int> &o) -> std::optional<double> {
        return o ? std::optional<double>(*o) : std::nullopt;
    };
    switch (k) {
    case VitalKey::Pulse:       return i(r.pulse);
    case VitalKey::Systolic:    return i(r.systolic);
    case VitalKey::Diastolic:   return i(r.diastolic);
    case VitalKey::Temperature: return r.temperature;
    case VitalKey::SpO2:        return i(r.spo2);
    case VitalKey::PeakFlow:    return i(r.peakFlow);
    case VitalKey::Glucose:
        return r.glucose ? std::optional<double>(glucoseToDisplay(*r.glucose)) : std::nullopt;
    case VitalKey::Weight:      return r.weight;
    case VitalKey::Height:      return r.height;
    case VitalKey::Bmi:         return r.bmi;
    }
    return std::nullopt;
}

QString formatted(const VitalRecord &r, VitalKey k, bool withUnit)
{
    const auto v = value(r, k);
    if (!v) return {};
    QString s = QLocale().toString(*v, 'f', decimals(k));
    if (withUnit) s += QLatin1Char(' ') + unit(k);
    return s;
}

QList<Panel> panels()
{
    return {
        { QStringLiteral("bp"),      tr("Blutdruck & Puls"),
          { VitalKey::Systolic, VitalKey::Diastolic, VitalKey::Pulse } },
        { QStringLiteral("temp"),    tr("Körpertemperatur"), { VitalKey::Temperature } },
        { QStringLiteral("spo2"),    tr("Sauerstoffsättigung"), { VitalKey::SpO2 } },
        { QStringLiteral("peakflow"), tr("Peak Flow"), { VitalKey::PeakFlow } },
        { QStringLiteral("glucose"), tr("Blutzucker"), { VitalKey::Glucose } },
        { QStringLiteral("weight"),  tr("Gewicht"), { VitalKey::Weight } },
        { QStringLiteral("bmi"),     tr("BMI"), { VitalKey::Bmi } },
    };
}

} // namespace VitalDefs
