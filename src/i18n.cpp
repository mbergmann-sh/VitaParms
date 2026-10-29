// SPDX-License-Identifier: MIT
#include "i18n.h"
#include "qtcompat.h"
#include "appsettings.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QTranslator>

namespace I18n {

QString effectiveLanguage()
{
    const QString l = AppSettings::language();
    if (l == QLatin1String("de") || l == QLatin1String("en")) return l;
    // Systemsprache: Deutsch, sonst Englisch
    return QLocale::system().language() == QLocale::German ? QStringLiteral("de") : QStringLiteral("en");
}

QString install(QCoreApplication &app)
{
    // erneuter Aufruf (Sprachwechsel zur Laufzeit, Tablet): bisherige Übersetzer entfernen
    static QList<QTranslator *> s_installed;
    for (QTranslator *t : std::as_const(s_installed)) {
        app.removeTranslator(t);
        delete t;
    }
    s_installed.clear();
    const QString lang = effectiveLanguage();
    const QLocale locale = lang == QLatin1String("en") ? QLocale(QLocale::English, QLocale::UnitedKingdom)
                                                       : QLocale(QLocale::German, QLocale::Germany);
    QLocale::setDefault(locale);

    auto *qtTr = new QTranslator(&app);
    if (qtTr->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                   vpLibraryPath(QLibraryInfo::TranslationsPath)))
        app.installTranslator(qtTr);
    s_installed << qtTr;

    if (lang == QLatin1String("en")) {
        auto *appTr = new QTranslator(&app);
        if (appTr->load(QStringLiteral(":/i18n/vitaparms_en.qm")))
            app.installTranslator(appTr);
        s_installed << appTr;
    }
    return lang;
}

void restartApplication()
{
    QStringList args = QCoreApplication::arguments();
    args.removeFirst();
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    qApp->quit();
}

} // namespace I18n
