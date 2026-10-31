# ---------------------------------------------------------------------------
#  VitaParms Tablet – gemeinsamer Teil der Touch-Oberfläche (Qt Quick).
#  Eingebunden von VitaParmsMobile.pro (VitaParms) und ../light/mobile/VitaParmsLightMobile.pro
#  (VitaParms light, Schalter VP_LIGHT). Vor dem Einbinden setzbar: ANDROID_PACKAGE_SOURCE_DIR,
#  VP_MOBILE_DOCS (Kurzanleitungen im Android-Paket), VP_SOURCE_DOCS_DIR (Desktop-Test).
# ---------------------------------------------------------------------------
QT += core gui widgets network qml quick quickcontrols2

qtHaveModule(multimedia) {
    QT      += multimedia
    DEFINES += VP_HAVE_MULTIMEDIA
}
# Qt PDF (optional): Vorschau und Volltext der Dokumente in der Krankenakte
qtHaveModule(pdf) {
    QT      += pdf
    DEFINES += VP_HAVE_QTPDF
}
qtHaveModule(texttospeech) {
    QT      += texttospeech
    DEFINES += VP_HAVE_TTS
}

CONFIG   += c++17
TEMPLATE  = app

include($$PWD/../version.pri)
DEFINES  += VITAPARMS_VERSION=\\\"$$VERSION\\\"
DEFINES  += QT_DISABLE_DEPRECATED_UP_TO=0x060400
# Kein Qt Print Support und keine .ui-Formulare auf Tablets
DEFINES  += VP_NO_PRINTSUPPORT VP_NO_UITOOLS VP_MOBILE

include($$PWD/../core.pri)

INCLUDEPATH += $$PWD/src
SOURCES += \
    $$PWD/src/main.cpp \
    $$PWD/src/appcontroller.cpp \
    $$PWD/src/appcontroller_appts.cpp \
    $$PWD/src/appcontroller_docs.cpp \
    $$PWD/src/appcontroller_stock.cpp \
    $$PWD/src/appcontroller_reports.cpp \
    $$PWD/src/appcontroller_admin.cpp \
    $$PWD/src/appcontroller_wounds.cpp \
    $$PWD/src/tabletreminders.cpp \
    $$PWD/src/chartitem.cpp
HEADERS += \
    $$PWD/src/appcontroller.h \
    $$PWD/src/tabletreminders.h \
    $$PWD/src/chartitem.h

# Nur für Bildschirmfotos/Tests: automatischer Rundgang (qmake CONFIG+=autopilot)
autopilot {
    DEFINES += VP_AUTOPILOT
    SOURCES += $$PWD/src/autopilot.cpp
}

RESOURCES += $$PWD/mobile.qrc $$PWD/../resources/vitaparms.qrc

# Übersetzungen (Quellsprache Deutsch): Texte der QML-Oberfläche und des Kerns
TRANSLATIONS = $$PWD/translations/vitaparms_en.ts
CONFIG += lrelease embed_translations
QM_FILES_RESOURCE_PREFIX = /i18n
lupdate_only { SOURCES += $$files($$PWD/qml/*.qml, true) }

# ---------------------------------------------------------------- Android
android {
    # Versionsnummer für den Paketmanager: 0.10.0 → 10000, 1.2.3 → 1002003
    VP_PARTS = $$split(VERSION, .)
    ANDROID_VERSION_NAME = $$VERSION
    ANDROID_VERSION_CODE = $$num_add($$member(VP_PARTS, 0)000000, $$member(VP_PARTS, 1)000, $$member(VP_PARTS, 2))
    isEmpty(ANDROID_PACKAGE_SOURCE_DIR): ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    ANDROID_TARGET_SDK_VERSION = 34
    ANDROID_MIN_SDK_VERSION = 26

    # Kurzanleitung „Erste Schritte“ (PDF, deutsch und englisch) ins Paket: assets:/docs
    isEmpty(VP_MOBILE_DOCS): VP_MOBILE_DOCS = $$PWD/../docs/VitaParms-ErsteSchritte.pdf $$PWD/../docs/VitaParms-GettingStarted.pdf
    vpdocs.files = $$VP_MOBILE_DOCS
    vpdocs.path = /assets/docs
    INSTALLS += vpdocs

    # OpenSSL für HTTPS (Abgleich über die Website). Android bringt es für Qt nicht mit.
    # Mitgeliefert in ../3rdparty/android_openssl (OpenSSL 3, KDAB, Apache-2.0) – kein Download in
    # Qt Creator nötig. Eine andere Version: Umgebungsvariable VP_ANDROID_OPENSSL auf einen Ordner
    # mit openssl.pri setzen (z. B. den Download aus Qt Creator).
    # (VitaParms light gleicht nur per USB ab und braucht kein HTTPS)
    contains(DEFINES, VP_LIGHT): VP_OPENSSL_DIRS =
    else: VP_OPENSSL_DIRS = $$(VP_ANDROID_OPENSSL) $$PWD/../3rdparty/android_openssl
    VP_OPENSSL_PRI =
    for (d, VP_OPENSSL_DIRS) {
        isEmpty(VP_OPENSSL_PRI):exists($$clean_path($$d)/openssl.pri): VP_OPENSSL_PRI = $$clean_path($$d)/openssl.pri
    }
    !isEmpty(VP_OPENSSL_PRI) {
        include($$VP_OPENSSL_PRI)
        message("OpenSSL fuer Android: $$VP_OPENSSL_PRI")
        # Prüfen, ob die Bibliotheken für dieses ABI wirklich da sind (sonst später „TLS fehlt“)
        for (lib, ANDROID_EXTRA_LIBS) {
            contains(lib, .*(ssl|crypto).*) {
                exists($$lib): message("  OpenSSL-Bibliothek: $$lib")
                else: warning("  OpenSSL-Bibliothek FEHLT: $$lib - bitte OpenSSL in Qt Creator neu herunterladen")
            }
        }
    } else:!contains(DEFINES, VP_LIGHT) {
        message("OpenSSL fuer Android nicht gefunden - der Abgleich ueber die Website (HTTPS) geht dann nicht. Siehe mobile/README.md")
        message("Gesucht in: $$unique(VP_OPENSSL_DIRS)")
    }
} else {
    # Desktop-Test der Tablet-App: Kurzanleitung aus dem Quellordner
    isEmpty(VP_SOURCE_DOCS_DIR): VP_SOURCE_DOCS_DIR = $$clean_path($$PWD/../docs)
    DEFINES += VP_SOURCE_DOCS=\\\"$$VP_SOURCE_DOCS_DIR\\\"
}

# iOS: folgt in einer späteren Ausbaustufe (Bauen und Signieren nur auf einem Mac mit Xcode)

OTHER_FILES += $$files($$PWD/qml/*.qml, true) $$PWD/README.md
