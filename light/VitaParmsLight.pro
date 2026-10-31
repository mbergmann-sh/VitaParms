# ---------------------------------------------------------------------------
#  VitaParms light – das einfache Tagebuch für Vitalwerte (eine Person)
#  Copyright (c) 2026 Michael Bergmann – MIT License (siehe ../LICENSE)
#
#  Unterprojekt von VitaParms: derselbe Code und dieselbe (verschlüsselte) Datenbank, aber nur ein
#  Teil der Funktionen und eine vereinfachte Oberfläche (Schalter VP_LIGHT, siehe src/edition.h).
#  Eine Datenbank von VitaParms light lässt sich in VitaParms übernehmen
#  (Datei → Datenbank der Light-Version importieren).
#
#  Build:   qmake6 light/VitaParmsLight.pro && make   (bzw. nmake/jom unter Windows)
#           qmake (Qt 5.15) light/VitaParmsLight.pro – z. B. für macOS 10.13–10.15
#  Benötigt dieselben Qt-Module wie VitaParms (widgets printsupport uitools network).
#  Installer (Qt Installer Framework) für Windows, Linux und macOS: light/installer/README.md
# ---------------------------------------------------------------------------

lessThan(QT_MAJOR_VERSION, 5) | equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 15) {
    error("VitaParms light braucht Qt 5.15 oder Qt 6.4 und neuer (gefunden: $$[QT_VERSION])")
}
equals(QT_MAJOR_VERSION, 6):lessThan(QT_MINOR_VERSION, 4) {
    error("VitaParms light braucht unter Qt 6 mindestens Qt 6.4 (gefunden: $$[QT_VERSION])")
}
macx:equals(QT_MAJOR_VERSION, 5): QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.13

QT += core gui widgets printsupport uitools network
qtHaveModule(texttospeech) {
    QT      += texttospeech
    DEFINES += VP_HAVE_TTS
}
qtHaveModule(multimedia) {
    QT      += multimedia
    DEFINES += VP_HAVE_MULTIMEDIA
}
qtHaveModule(pdfwidgets) {
    QT      += pdf pdfwidgets
    DEFINES += VP_HAVE_QTPDF
}
CONFIG   += c++17
TEMPLATE  = app
TARGET    = VitaParmsLight

include(../version.pri)
DEFINES  += VITAPARMS_VERSION=\\\"$$VERSION\\\" VP_LIGHT
greaterThan(QT_MAJOR_VERSION, 5): DEFINES += QT_DISABLE_DEPRECATED_UP_TO=0x060400

INCLUDEPATH += ../src
include(../core.pri)
include(../desktop.pri)

RESOURCES += ../resources/vitaparms.qrc light.qrc

# Eine gemeinsame Übersetzungsdatei mit VitaParms (lupdate über ../VitaParms.pro)
TRANSLATIONS = ../translations/vitaparms_en.ts
CONFIG += lrelease embed_translations
QM_FILES_RESOURCE_PREFIX = /i18n

win32: RC_ICONS = resources/vitaparms-light.ico
macx {
    ICON = resources/vitaparms-light.icns
    QMAKE_TARGET_BUNDLE_PREFIX = de.mbergmann
}

# Handbücher (Benutzerhandbuch, Erste Schritte – deutsch und englisch) neben das Programm
VP_DOCS = $$files($$PWD/docs/*.pdf)
win32: VP_DOCS += $$files($$PWD/docs/*.chm)
lightdocs.files = $$VP_DOCS
win32:debug_and_release {
    CONFIG(debug, debug|release): lightdocs.path = $$OUT_PWD/debug/docs
    else:                         lightdocs.path = $$OUT_PWD/release/docs
} else {
    lightdocs.path = $$OUT_PWD/docs
}
macx: lightdocs.path = $$OUT_PWD/$${TARGET}.app/Contents/Resources/docs
COPIES += lightdocs

# Versionsnummer und Datum in die Installer-Vorlagen übernehmen (*.in → Datei ohne .in)
VP_TEMPLATES = installer/qtifw/config/config.xml.in \
               installer/qtifw/packages/de.mbergmann.vitaparms.light/meta/package.xml.in \
               installer/qtifw/packages/de.mbergmann.vitaparms.light.docs/meta/package.xml.in
for(t, VP_TEMPLATES) {
    vp_in = $$cat($$PWD/$$t, blob)
    vp_in = $$replace(vp_in, @VP_VERSION@, $$VERSION)
    vp_in = $$replace(vp_in, @VP_RELEASE_DATE@, $$VP_RELEASE_DATE)
    vp_out = $$PWD/$$replace(t, \.in$, )
    vp_cur = $$cat($$vp_out, blob)
    !equals(vp_cur, $$vp_in$$escape_expand(\\n)): write_file($$vp_out, vp_in)
}

# Bereitstellen und Installer bauen (Windows, Linux, macOS) – nur im Release-Build
include(installer/light_deploy.pri)

OTHER_FILES += light.qrc installer/README.md installer/light_deploy.pri installer/linux_deploy.sh \
    $$files(installer/qtifw/config/*) $$files(installer/qtifw/packages/*/meta/*)
