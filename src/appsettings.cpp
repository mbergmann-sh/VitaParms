// SPDX-License-Identifier: MIT
#include "appsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <algorithm>
#include <QStandardPaths>

static QString dataDir()
{
    QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d;
}

QString AppSettings::defaultDatabasePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("vitaparms.sqlite"));
}

QString AppSettings::databasePath()
{
    QSettings s;
    QString p = s.value(QStringLiteral("database/path")).toString();
    return p.isEmpty() ? defaultDatabasePath() : p;
}

void AppSettings::setDatabasePath(const QString &p)
{
    QSettings().setValue(QStringLiteral("database/path"), p);
}

AppSettings::GlucoseUnit AppSettings::glucoseUnit()
{
    return QSettings().value(QStringLiteral("units/glucose"), QStringLiteral("mg/dl")).toString()
                   == QLatin1String("mmol/l")
               ? GlucoseUnit::MmolL
               : GlucoseUnit::MgDl;
}

void AppSettings::setGlucoseUnit(GlucoseUnit u)
{
    QSettings().setValue(QStringLiteral("units/glucose"),
                         u == GlucoseUnit::MmolL ? QStringLiteral("mmol/l") : QStringLiteral("mg/dl"));
}

qint64 AppSettings::primaryUser(int slot)
{
    return QSettings().value(QStringLiteral("users/primary%1").arg(slot), 0).toLongLong();
}

void AppSettings::setPrimaryUser(int slot, qint64 patientId)
{
    QSettings().setValue(QStringLiteral("users/primary%1").arg(slot), patientId);
}

qint64 AppSettings::lastPatient()
{
    return QSettings().value(QStringLiteral("users/last"), 0).toLongLong();
}

void AppSettings::setLastPatient(qint64 id)
{
    QSettings().setValue(QStringLiteral("users/last"), id);
}

bool AppSettings::disclaimerAccepted()
{
    return QSettings().value(QStringLiteral("app/disclaimerAccepted"), false).toBool();
}

void AppSettings::setDisclaimerAccepted(bool b)
{
    QSettings().setValue(QStringLiteral("app/disclaimerAccepted"), b);
}

QStringList AppSettings::defaultProfileContexts()
{
    return {
        QCoreApplication::translate("AppSettings", "nüchtern"),
        QCoreApplication::translate("AppSettings", "vor dem Frühstück"),
        QCoreApplication::translate("AppSettings", "2 h nach dem Frühstück"),
        QCoreApplication::translate("AppSettings", "vor dem Mittagessen"),
        QCoreApplication::translate("AppSettings", "2 h nach dem Mittagessen"),
        QCoreApplication::translate("AppSettings", "vor dem Abendessen"),
        QCoreApplication::translate("AppSettings", "2 h nach dem Abendessen"),
        QCoreApplication::translate("AppSettings", "vor dem Schlafengehen"),
        QCoreApplication::translate("AppSettings", "nachts"),
        QCoreApplication::translate("AppSettings", "nach Belastung"),
        QCoreApplication::translate("AppSettings", "bei Beschwerden"),
    };
}

QStringList AppSettings::profileContexts()
{
    QSettings s;
    if (!s.contains(QStringLiteral("profile/contexts")))
        return defaultProfileContexts();
    return s.value(QStringLiteral("profile/contexts")).toStringList();
}

void AppSettings::setProfileContexts(const QStringList &l)
{
    QSettings().setValue(QStringLiteral("profile/contexts"), l);
}

QStringList AppSettings::chartSeries()
{
    return QSettings()
        .value(QStringLiteral("chart/panels"),
               QStringList{ QStringLiteral("bp"), QStringLiteral("glucose"),
                            QStringLiteral("weight") })
        .toStringList();
}

void AppSettings::setChartSeries(const QStringList &l)
{
    QSettings().setValue(QStringLiteral("chart/panels"), l);
}

QStringList AppSettings::chartHiddenKeys()
{
    return QSettings().value(QStringLiteral("chart/hidden")).toStringList();
}

void AppSettings::setChartHiddenKeys(const QStringList &l)
{
    QSettings().setValue(QStringLiteral("chart/hidden"), l);
}

bool AppSettings::chartSeparatePages()
{
    return QSettings().value(QStringLiteral("chart/separatePages"), false).toBool();
}

void AppSettings::setChartSeparatePages(bool b)
{
    QSettings().setValue(QStringLiteral("chart/separatePages"), b);
}

int AppSettings::chartScale()
{
    return std::clamp(QSettings().value(QStringLiteral("chart/scale"), 1).toInt(), 0, 4);
}

void AppSettings::setChartScale(int s)
{
    QSettings().setValue(QStringLiteral("chart/scale"), s);
}

QString AppSettings::userFormsDir()
{
    return QDir(dataDir()).filePath(QStringLiteral("forms"));
}

QByteArray AppSettings::windowState(const QString &key)
{
    return QSettings().value(QStringLiteral("window/") + key).toByteArray();
}

void AppSettings::setWindowState(const QString &key, const QByteArray &st)
{
    QSettings().setValue(QStringLiteral("window/") + key, st);
}

QString AppSettings::language()
{
    return QSettings().value(QStringLiteral("ui/language"), QStringLiteral("system")).toString();
}

void AppSettings::setLanguage(const QString &l)
{
    QSettings().setValue(QStringLiteral("ui/language"), l);
}

int AppSettings::autoLockMinutes()
{
    return QSettings().value(QStringLiteral("security/autoLockMinutes"), 0).toInt();
}

void AppSettings::setAutoLockMinutes(int m)
{
    QSettings().setValue(QStringLiteral("security/autoLockMinutes"), m);
}

QString AppSettings::voiceGender()
{
    return QSettings().value(QStringLiteral("speech/gender"), QStringLiteral("female")).toString();
}

void AppSettings::setVoiceGender(const QString &g)
{
    QSettings().setValue(QStringLiteral("speech/gender"), g);
}

double AppSettings::speechRate()
{
    return QSettings().value(QStringLiteral("speech/rate"), -0.1).toDouble();
}

void AppSettings::setSpeechRate(double r)
{
    QSettings().setValue(QStringLiteral("speech/rate"), r);
}
