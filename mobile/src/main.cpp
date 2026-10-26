// VitaParms Tablet – Startpunkt
// SPDX-License-Identifier: MIT
#include "appcontroller.h"
#include "chartitem.h"
#include "database.h"
#include "i18n.h"
#include "session.h"
#include "appsettings.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QIcon>
#include <QTimer>
#include <QQmlApplicationEngine>
#include <QQuickImageProvider>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>

#ifdef VP_AUTOPILOT
int runAutopilot(QQmlApplicationEngine &engine, AppController &app);
#endif

int main(int argc, char *argv[])
{
#ifdef VP_AUTOPILOT
    QStandardPaths::setTestModeEnabled(true);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
#endif
    // QApplication (statt QGuiApplication): der gemeinsame Kern verwendet einzelne Widgets-Klassen
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("VitaParms"));
    QApplication::setApplicationName(QStringLiteral("VitaParms"));
    QApplication::setApplicationDisplayName(QStringLiteral("VitaParms"));
    QApplication::setApplicationVersion(QStringLiteral(VITAPARMS_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/vitaparms.png")));
#ifdef VP_AUTOPILOT
    AppSettings::setLanguage(qEnvironmentVariable("VP_AUTOPILOT_LANG", QStringLiteral("de")));
#endif
    I18n::install(app);

    for (const char *f : { ":/fonts/AtkinsonHyperlegible_400Regular.ttf", ":/fonts/AtkinsonHyperlegible_700Bold.ttf" })
        QFontDatabase::addApplicationFont(QString::fromLatin1(f));
    QFont font(QStringLiteral("Atkinson Hyperlegible"));
    font.setPixelSize(17);
    QGuiApplication::setFont(font);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    qmlRegisterType<ChartItem>("VitaParms", 1, 0, "Chart");
    ChartRenderer::setScreenFontScale(1.45);      // Kurven mit dem Finger und aus Armlänge lesbar

#ifdef VP_AUTOPILOT
    AppSettings::setDatabasePath(qEnvironmentVariable("VP_AUTOPILOT_DB"));
    AppSettings::setDisclaimerAccepted(true);
    AppSettings::setAutoLockMinutes(0);
#endif
    AppController controller;
#ifdef VP_AUTOPILOT
    if (!controller.login(qEnvironmentVariable("VP_AUTOPILOT_USER"), qEnvironmentVariable("VP_AUTOPILOT_PASSWORD")))
        qWarning("Anmeldung fehlgeschlagen: %s", qPrintable(controller.lastError()));
#endif
    QQmlApplicationEngine engine;
    controller.setEngine(&engine);
    // Seiten der Krankenakte: image://docpage/<Dokument-ID oder draft>/<Seite>
    struct DocPages : QQuickImageProvider {
        AppController *c;
        explicit DocPages(AppController *ctl) : QQuickImageProvider(QQuickImageProvider::Image), c(ctl) {}
        QImage requestImage(const QString &id, QSize *size, const QSize &requested) override {
            const QStringList parts = id.section(QLatin1Char('?'), 0, 0).split(QLatin1Char('/'));
            const int width = requested.width() > 0 ? requested.width() : 900;
            QImage img = c->documentPageImage(parts.value(0), parts.value(1).toInt(), width);
            if (img.isNull()) {                  // gelöscht oder (noch) ohne Inhalt: leere Fläche statt Warnung
                img = QImage(1, 1, QImage::Format_ARGB32);
                img.fill(Qt::transparent);
            }
            if (size) *size = img.size();
            return img;
        }
    };
    engine.addImageProvider(QStringLiteral("docpage"), new DocPages(&controller));
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
#ifdef VP_AUTOPILOT
    QTimer::singleShot(800, &app, [&] { QCoreApplication::exit(runAutopilot(engine, controller)); });
#endif

    // Hintergrund/Vordergrund: Änderungen senden, nach längerer Pause sperren und abgleichen
    QElapsedTimer away;
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &controller, [&](Qt::ApplicationState s) {
        if (s == Qt::ApplicationSuspended || s == Qt::ApplicationHidden) {
            away.start();
            controller.syncIfDue();
        } else if (s == Qt::ApplicationActive && away.isValid()) {
            const int lockMin = controller.autoLockMinutes();
            if (controller.loggedIn() && lockMin > 0 && away.elapsed() > qint64(lockMin) * 60000) {
                controller.lock();
                emit controller.locked();
            } else {
                controller.syncIfDue();
            }
            away.invalidate();
        }
    });

    const int rc = app.exec();
    Database::instance().close();
    Session::clear();
    return rc;
}
