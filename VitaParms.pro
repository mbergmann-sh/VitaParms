# ---------------------------------------------------------------------------
#  VitaParms – Tagebuch für Vitalwerte
#  Copyright (c) 2026 Michael Bergmann – MIT License (siehe LICENSE)
#
#  Build:   qmake6 VitaParms.pro && make        (Linux/macOS)
#           qmake6 VitaParms.pro && nmake/jom   (Windows, MSVC)
#           qmake (Qt 5.15) VitaParms.pro && make – z. B. für macOS 10.13–10.15
#  Benötigt Qt 6.4 oder neuer bzw. Qt 5.15 mit den Modulen: widgets printsupport uitools network
#  (Qt-5/6-Unterschiede gleicht src/qtcompat.h aus; Qt PDF gibt es nur für Qt 6)
#  Optional: texttospeech (Vorlesen im Medikamenten-Assistenten), multimedia (Signaltöne)
#  Datenbank: SQLite3 Multiple Ciphers (3rdparty/sqlite3mc, MIT) – fest eingebaut, verschlüsselt
#  Empfohlen: Qt PDF (pdf, pdfwidgets) für Vorschau, Volltextsuche und Druck der
#  Dokumente in der Krankenakte – ohne Qt PDF wird trotzdem gebaut.
# ---------------------------------------------------------------------------

# Qt 5.15 (ältere macOS-Versionen) oder Qt 6.4 und neuer
lessThan(QT_MAJOR_VERSION, 5) | equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 15) {
    error("VitaParms braucht Qt 5.15 oder Qt 6.4 und neuer (gefunden: $$[QT_VERSION])")
}
equals(QT_MAJOR_VERSION, 6):lessThan(QT_MINOR_VERSION, 4) {
    error("VitaParms braucht unter Qt 6 mindestens Qt 6.4 (gefunden: $$[QT_VERSION])")
}
equals(QT_MAJOR_VERSION, 5): message("VitaParms: Build mit Qt $$[QT_VERSION] (Qt-5-Kompatibilitaet, ohne Qt PDF)")
# Qt 5.15 auf macOS: lauffähig ab macOS 10.13 (High Sierra) – auch wenn auf einem neueren Mac gebaut wird
macx:equals(QT_MAJOR_VERSION, 5): QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.13

QT       += core gui widgets printsupport uitools network

# Sprachausgabe für den Medikamenten-Assistenten (optional, Qt TextToSpeech)
qtHaveModule(texttospeech) {
    QT      += texttospeech
    DEFINES += VP_HAVE_TTS
    message("VitaParms: mit Sprachausgabe (Qt TextToSpeech)")
} else {
    message("VitaParms: OHNE Sprachausgabe - Assistent zeigt nur an")
}
# Signaltöne (optional, Qt Multimedia) – sonst Systempiepton
qtHaveModule(multimedia) {
    QT      += multimedia
    DEFINES += VP_HAVE_MULTIMEDIA
}

qtHaveModule(pdfwidgets) {
    QT      += pdf pdfwidgets
    DEFINES += VP_HAVE_QTPDF
    message("VitaParms: mit Qt PDF (Vorschau, Volltextsuche, Druck der Dokumente)")
} else {
    message("VitaParms: OHNE Qt PDF - Dokumente werden extern geoeffnet, keine Volltextsuche")
}
CONFIG   += c++17
TEMPLATE  = app
TARGET    = VitaParms

include(version.pri)
DEFINES  += VITAPARMS_VERSION=\\\"$$VERSION\\\"
greaterThan(QT_MAJOR_VERSION, 5): DEFINES += QT_DISABLE_DEPRECATED_UP_TO=0x060400

INCLUDEPATH += src

# Gemeinsamer Kern (auch für die Tablet-App unter mobile/)
include(core.pri)

# Desktop-Oberfläche (auch für VitaParms light unter light/)
include(desktop.pri)

# Die Formulare werden NICHT mit uic übersetzt, sondern zur Laufzeit per
# QUiLoader geladen. Die Standardversionen liegen als Ressource im Programm;
# Benutzer können angepasste Kopien im Formularordner ablegen.
RESOURCES += resources/vitaparms.qrc

# Übersetzungen: Quellsprache Deutsch, Englisch als zweite Sprache.
# Aktualisieren:  lupdate VitaParms.pro   –   übersetzen mit Qt Linguist
# Die .qm-Datei wird beim Bauen automatisch erzeugt und eingebettet.
TRANSLATIONS = translations/vitaparms_en.ts
# Nur für lupdate: Texte der zur Laufzeit geladenen Formulare erfassen
lupdate_only {
    FORMS = forms/patientform.ui forms/vitalform.ui forms/medicationform.ui forms/documentform.ui forms/appointmentform.ui \
            forms/light/patientform.ui forms/light/vitalform.ui
}
CONFIG += lrelease embed_translations
QM_FILES_RESOURCE_PREFIX = /i18n

OTHER_FILES += \
    forms/patientform.ui \
    forms/vitalform.ui \
    forms/medicationform.ui \
    forms/documentform.ui \
    forms/appointmentform.ui \
    README.md LICENSE

win32: RC_ICONS = resources/icons/vitaparms.ico
macx:  ICON     = resources/icons/vitaparms.icns

# Handbücher: docs/ wird neben das Programm kopiert (Hilfe → Handbuch, F1).
#   VitaParms-Handbuch.pdf / VitaParms-Manual.pdf   – alle Systeme
#   VitaParms-Handbuch.chm / VitaParms-Manual.chm   – Windows-Hilfe (erzeugt mit tools/make_chm.py)
VP_DOCS = $$files($$PWD/docs/*.pdf)
win32: VP_DOCS += $$files($$PWD/docs/*.chm)
manualdocs.files = $$VP_DOCS
win32:debug_and_release {
    CONFIG(debug, debug|release): manualdocs.path = $$OUT_PWD/debug/docs
    else:                         manualdocs.path = $$OUT_PWD/release/docs
} else {
    manualdocs.path = $$OUT_PWD/docs
}
macx: manualdocs.path = $$OUT_PWD/$${TARGET}.app/Contents/Resources/docs
COPIES += manualdocs

# Windows-Installer: Versionsnummer und Datum in die Installer-Skripte übernehmen
# (*.in → Datei ohne .in; @VP_VERSION@, @VP_RELEASE_DATE@). Läuft auf allen Plattformen,
# damit die erzeugten Dateien im Quellbaum immer zur Programmversion passen.
# (Bewusst nicht QMAKE_SUBSTITUTES: das entfernt Anführungszeichen und Backslashes.)
VP_TEMPLATES = installer/VitaParms.iss.in \
               installer/qtifw/config/config.xml.in \
               installer/qtifw/packages/de.mbergmann.vitaparms/meta/package.xml.in \
               installer/qtifw/packages/de.mbergmann.vitaparms.docs/meta/package.xml.in
for(t, VP_TEMPLATES) {
    vp_in = $$cat($$PWD/$$t, blob)
    vp_in = $$replace(vp_in, @VP_VERSION@, $$VERSION)
    vp_in = $$replace(vp_in, @VP_RELEASE_DATE@, $$VP_RELEASE_DATE)
    vp_out = $$PWD/$$replace(t, \.in$, )
    vp_cur = $$cat($$vp_out, blob)
    !equals(vp_cur, $$vp_in$$escape_expand(\\n)): write_file($$vp_out, vp_in)
}

# Windows: windeployqt nach jedem Build, Installationsverzeichnis + Installer beim Release-Build
win32: include(installer/windows_deploy.pri)

OTHER_FILES += installer/VitaParms.iss.in installer/windows_deploy.pri installer/README.md \
    installer/qtifw/config/config.xml.in installer/qtifw/config/controlscript.qs \
    installer/qtifw/packages/de.mbergmann.vitaparms/meta/package.xml.in \
    installer/qtifw/packages/de.mbergmann.vitaparms/meta/installscript.qs \
    installer/qtifw/packages/de.mbergmann.vitaparms.docs/meta/package.xml.in \
    installer/qtifw/packages/de.mbergmann.vitaparms.docs/meta/installscript.qs

# Installation (make install)
unix:!macx {
    target.path = /usr/local/bin
    docs.files = $$files($$PWD/docs/*.pdf)
    docs.path  = /usr/local/share/vitaparms/docs
    INSTALLS += target docs
}
