// SPDX-License-Identifier: MIT
#include "vitaldefs.h"
#include "appsettings.h"
#include "edition.h"
#include "stool.h"
#include "fluid.h"

#include <QCoreApplication>
#include <QLocale>

namespace VitalDefs {

static QString tr(const char *s) { return QCoreApplication::translate("VitalDefs", s); }

QList<VitalKey> all()
{
    return { VitalKey::Pulse, VitalKey::Systolic, VitalKey::Diastolic,
             VitalKey::Temperature, VitalKey::SpO2, VitalKey::PeakFlow, VitalKey::Glucose, VitalKey::Pain,
             VitalKey::Inr, VitalKey::Weight, VitalKey::Height, VitalKey::Bmi, VitalKey::FluidIn, VitalKey::FluidOut,
             VitalKey::Edema, VitalKey::Stool };
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
    case VitalKey::Stool:       return QStringLiteral("stool");
    case VitalKey::Inr:         return QStringLiteral("inr");
    case VitalKey::FluidIn:     return QStringLiteral("fluidIn");
    case VitalKey::FluidOut:    return QStringLiteral("fluidOut");
    case VitalKey::Edema:       return QStringLiteral("edema");
    case VitalKey::Pain:        return QStringLiteral("pain");
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
    case VitalKey::Stool:       return tr("Stuhlgang");
    case VitalKey::Inr:         return tr("INR");
    case VitalKey::FluidIn:     return tr("Einfuhr");
    case VitalKey::FluidOut:    return tr("Ausfuhr");
    case VitalKey::Edema:       return tr("Ödeme");
    case VitalKey::Pain:        return tr("Schmerzstärke");
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
    case VitalKey::Stool:       return tr("Stuhlgang");
    case VitalKey::Inr:         return tr("INR");
    case VitalKey::FluidIn:     return tr("Einfuhr");
    case VitalKey::FluidOut:    return tr("Ausfuhr");
    case VitalKey::Edema:       return tr("Ödeme");
    case VitalKey::Pain:        return tr("Schmerz");
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
    case VitalKey::Stool:       return tr("Bristol-Typ");
    case VitalKey::Inr:         return QString();
    case VitalKey::FluidIn:
    case VitalKey::FluidOut:    return tr("ml");
    case VitalKey::Edema:       return tr("Grad");
    case VitalKey::Pain:        return tr("0–10");
    }
    return {};
}

QString header(VitalKey k, const QString &sep)
{
    const QString u = unit(k);
    return u.isEmpty() ? shortLabel(k) : shortLabel(k) + sep + QLatin1Char('[') + u + QLatin1Char(']');
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
    case VitalKey::Inr:         return 1;
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
    case VitalKey::Stool:       return QColor(0x8C, 0x5A, 0x2B); // Braun (Balken)
    case VitalKey::Inr:         return QColor(0x88, 0x22, 0x55); // Weinrot
    case VitalKey::FluidIn:     return QColor(0x00, 0x72, 0xB2); // Blau
    case VitalKey::FluidOut:    return QColor(0xE6, 0x9F, 0x00); // Orange
    case VitalKey::Edema:       return QColor(0x6A, 0x3D, 0x9A); // Violett
    case VitalKey::Pain:        return QColor(0xB2, 0x22, 0x22); // Ziegelrot
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
    case VitalKey::Stool:       return MarkerShape::Square;
    case VitalKey::Inr:         return MarkerShape::Diamond;
    case VitalKey::FluidIn:     return MarkerShape::Square;
    case VitalKey::FluidOut:    return MarkerShape::Square;
    case VitalKey::Edema:       return MarkerShape::Triangle;
    case VitalKey::Pain:        return MarkerShape::Circle;
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
    case VitalKey::Stool:       return i(r.stool);
    case VitalKey::Inr:         return r.inr;
    case VitalKey::FluidIn:     return i(r.fluidIn);
    case VitalKey::FluidOut:    return i(r.fluidOut);
    case VitalKey::Edema:       return i(r.edema);
    case VitalKey::Pain:        return i(r.pain);
    }
    return std::nullopt;
}

QString formatted(const VitalRecord &r, VitalKey k, bool withUnit)
{
    if (k == VitalKey::Stool)
        return r.stool ? Stool::shortText(*r.stool) : QString();
    if (k == VitalKey::Edema)
        return r.edema ? Fluid::edemaShort(*r.edema) : QString();
    if (k == VitalKey::Pain)                                    // „3/10“ statt „3 0–10“
        return r.pain ? (withUnit ? QStringLiteral("%1/10").arg(*r.pain) : QString::number(*r.pain)) : QString();
    const auto v = value(r, k);
    if (!v) return {};
    QString s = QLocale().toString(*v, 'f', decimals(k));
    if (withUnit && !unit(k).isEmpty()) s += QLatin1Char(' ') + unit(k);
    return s;
}

QList<Panel> panels()
{
    if (Edition::light())       // VitaParms light: nur die erfassten Werte
        return {
            { QStringLiteral("bp"),      tr("Blutdruck & Puls"),
              { VitalKey::Systolic, VitalKey::Diastolic, VitalKey::Pulse } },
            { QStringLiteral("temp"),    tr("Körpertemperatur"), { VitalKey::Temperature } },
            { QStringLiteral("spo2"),    tr("Sauerstoffsättigung"), { VitalKey::SpO2 } },
            { QStringLiteral("glucose"), tr("Blutzucker"), { VitalKey::Glucose } },
            { QStringLiteral("pain"),    tr("Schmerzstärke (0–10)"), { VitalKey::Pain } },
            { QStringLiteral("weight"),  tr("Gewicht"), { VitalKey::Weight } },
            { QStringLiteral("bmi"),     tr("BMI"), { VitalKey::Bmi } },
        };
    return {
        { QStringLiteral("bp"),      tr("Blutdruck & Puls"),
          { VitalKey::Systolic, VitalKey::Diastolic, VitalKey::Pulse } },
        { QStringLiteral("temp"),    tr("Körpertemperatur"), { VitalKey::Temperature } },
        { QStringLiteral("spo2"),    tr("Sauerstoffsättigung"), { VitalKey::SpO2 } },
        { QStringLiteral("peakflow"), tr("Peak Flow"), { VitalKey::PeakFlow } },
        { QStringLiteral("glucose"), tr("Blutzucker"), { VitalKey::Glucose } },
        { QStringLiteral("pain"),    tr("Schmerzstärke (0–10)"), { VitalKey::Pain } },
        { QStringLiteral("inr"),     tr("Gerinnung (INR)"), { VitalKey::Inr } },
        { QStringLiteral("weight"),  tr("Gewicht"), { VitalKey::Weight } },
        { QStringLiteral("bmi"),     tr("BMI"), { VitalKey::Bmi } },
        { QStringLiteral("fluid"),   tr("Ein- und Ausfuhr (Tagesbilanz)"), { VitalKey::FluidIn, VitalKey::FluidOut } },
        { QStringLiteral("edema"),   tr("Ödeme"), { VitalKey::Edema } },
        { QStringLiteral("stool"),   tr("Stuhlgang (Häufigkeit)"), { VitalKey::Stool } },
    };
}

} // namespace VitalDefs
