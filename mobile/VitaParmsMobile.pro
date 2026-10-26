# ---------------------------------------------------------------------------
#  VitaParms Tablet – Touch-Oberfläche (Qt Quick) für Android-Tablets (später iOS)
#  Copyright (c) 2026 Michael Bergmann – MIT License (siehe ../LICENSE)
#
#  Verwendet denselben Kern wie das Desktop-Programm (../core.pri): verschlüsselte
#  Datenbank, Fachlogik, Berichte, Abgleich über die eigene Website.
#
#  Bauen:  Qt Creator → Projekt öffnen → Kit "Android Qt 6.x arm64-v8a" wählen → Ausführen
#          (Anleitung: mobile/README.md). Am Desktop lässt sich die App zum Testen mit
#          einem normalen Kit bauen und starten.
#  Benötigt Qt >= 6.4 mit: quick quickcontrols2 widgets network; optional multimedia, texttospeech
# ---------------------------------------------------------------------------

QT += core gui widgets network qml quick quickcontrols2

qtHaveModule(multimedia) {
    QT      += multimedia
    DEFINES += VP_HAVE_MULTIMEDIA
}
qtHaveModule(texttospeech) {
    QT      += texttospeech
    DEFINES += VP_HAVE_TTS
}

CONFIG   += c++17
TEMPLATE  = app
TARGET    = VitaParms

include(../version.pri)
DEFINES  += VITAPARMS_VERSION=\\\"$$VERSION\\\"
DEFINES  += QT_DISABLE_DEPRECATED_UP_TO=0x060400
# Kein Qt Print Support und keine .ui-Formulare auf Tablets
DEFINES  += VP_NO_PRINTSUPPORT VP_NO_UITOOLS VP_MOBILE

include(../core.pri)

INCLUDEPATH += src
SOURCES += \
    src/main.cpp \
    src/appcontroller.cpp \
    src/appcontroller_appts.cpp \
    src/tabletreminders.cpp \
    src/chartitem.cpp
HEADERS += \
    src/appcontroller.h \
    src/tabletreminders.h \
    src/chartitem.h

# Nur für Bildschirmfotos/Tests: automatischer Rundgang (qmake CONFIG+=autopilot)
autopilot {
    DEFINES += VP_AUTOPILOT
    SOURCES += src/autopilot.cpp
}

RESOURCES += mobile.qrc ../resources/vitaparms.qrc

# Übersetzungen (Quellsprache Deutsch): Texte der QML-Oberfläche und des Kerns
TRANSLATIONS = translations/vitaparms_en.ts
CONFIG += lrelease embed_translations
QM_FILES_RESOURCE_PREFIX = /i18n
lupdate_only { SOURCES += $$files(qml/*.qml, true) }

# ---------------------------------------------------------------- Android
android {
    # Versionsnummer für den Paketmanager: 0.10.0 → 10000, 1.2.3 → 1002003
    VP_PARTS = $$split(VERSION, .)
    ANDROID_VERSION_NAME = $$VERSION
    ANDROID_VERSION_CODE = $$num_add($$member(VP_PARTS, 0)000000, $$member(VP_PARTS, 1)000, $$member(VP_PARTS, 2))
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    ANDROID_TARGET_SDK_VERSION = 34
    ANDROID_MIN_SDK_VERSION = 26

    # Kurzanleitung „Erste Schritte“ (PDF, deutsch und englisch) ins Paket: assets:/docs
    vpdocs.files = $$PWD/../docs/VitaParms-ErsteSchritte.pdf $$PWD/../docs/VitaParms-GettingStarted.pdf
    vpdocs.path = /assets/docs
    INSTALLS += vpdocs

    # OpenSSL für HTTPS (Abgleich über die Website). Android bringt es für Qt nicht mit.
    # Die Bibliotheken lädt Qt Creator unter Einstellungen → SDKs → Android → „OpenSSL herunterladen“.
    # Gesucht wird in: Umgebungsvariable VP_ANDROID_OPENSSL (Ordner mit openssl.pri), dann in den
    # Ordnern „android_openssl“ und „openssl“ des Android-SDK. Das SDK wird auch aus dem NDK-Pfad
    # abgeleitet (<SDK>/ndk/<Version>), den Qt Creator immer übergibt.
    VP_SDK_DIRS = $$(ANDROID_SDK_ROOT) $$(ANDROID_HOME)
    !isEmpty(NDK_ROOT): VP_SDK_DIRS += $$clean_path($$NDK_ROOT/../..)
    !isEmpty(DEFAULT_ANDROID_NDK_ROOT): VP_SDK_DIRS += $$clean_path($$DEFAULT_ANDROID_NDK_ROOT/../..)
    VP_NDK_ENV = $$(ANDROID_NDK_ROOT)
    !isEmpty(VP_NDK_ENV): VP_SDK_DIRS += $$clean_path($$VP_NDK_ENV/../..)
    VP_LAD = $$(LOCALAPPDATA)
    !isEmpty(VP_LAD): VP_SDK_DIRS += $$VP_LAD/Android/Sdk
    VP_HOME = $$(HOME)
    !isEmpty(VP_HOME): VP_SDK_DIRS += $$VP_HOME/Android/Sdk $$VP_HOME/Library/Android/sdk
    VP_SDK_DIRS += C:/Android/Sdk C:/Android/sdk
    VP_OPENSSL_DIRS = $$(VP_ANDROID_OPENSSL)
    for (s, VP_SDK_DIRS): VP_OPENSSL_DIRS += $$s/android_openssl $$s/openssl
    VP_OPENSSL_PRI =
    for (d, VP_OPENSSL_DIRS) {
        isEmpty(VP_OPENSSL_PRI):exists($$clean_path($$d)/openssl.pri): VP_OPENSSL_PRI = $$clean_path($$d)/openssl.pri
    }
    !isEmpty(VP_OPENSSL_PRI) {
        include($$VP_OPENSSL_PRI)
        message("OpenSSL für Android: $$VP_OPENSSL_PRI")
    } else {
        message("OpenSSL für Android nicht gefunden - der Abgleich über die Website (HTTPS) geht dann nicht. Siehe mobile/README.md")
        message("Gesucht in: $$unique(VP_OPENSSL_DIRS)")
    }
} else {
    # Desktop-Test der Tablet-App: Kurzanleitung aus dem Quellordner
    DEFINES += VP_SOURCE_DOCS=\\\"$$clean_path($$PWD/../docs)\\\"
}

# iOS: folgt in einer späteren Ausbaustufe (Bauen und Signieren nur auf einem Mac mit Xcode)

OTHER_FILES += $$files(qml/*.qml, true) README.md
