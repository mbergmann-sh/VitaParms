# ---------------------------------------------------------------------------
#  VitaParms – Tagebuch für Vitalwerte
#  Copyright (c) 2026 Michael Bergmann – MIT License (siehe LICENSE)
#
#  Build:   qmake6 VitaParms.pro && make        (Linux/macOS)
#           qmake6 VitaParms.pro && nmake/jom   (Windows, MSVC)
#  Benötigt Qt >= 6.4 mit den Modulen: widgets printsupport uitools network
#  Optional: texttospeech (Vorlesen im Medikamenten-Assistenten), multimedia (Signaltöne)
#  Datenbank: SQLite3 Multiple Ciphers (3rdparty/sqlite3mc, MIT) – fest eingebaut, verschlüsselt
#  Empfohlen: Qt PDF (pdf, pdfwidgets) für Vorschau, Volltextsuche und Druck der
#  Dokumente in der Krankenakte – ohne Qt PDF wird trotzdem gebaut.
# ---------------------------------------------------------------------------

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
DEFINES  += QT_DISABLE_DEPRECATED_UP_TO=0x060400

INCLUDEPATH += src

# Gemeinsamer Kern (auch für die Tablet-App unter mobile/)
include(core.pri)

SOURCES += \
    src/main.cpp \
    src/accountdialogs.cpp \
    src/inactivity.cpp \
    src/akteexport.cpp \
    src/documentdialog.cpp \
    src/documentspage.cpp \
    src/mailer.cpp \
    src/formcustomizedialog.cpp \
    src/mainwindow.cpp \
    src/medicationdialog.cpp \
    src/patientdialog.cpp \
    src/settingsdialog.cpp \
    src/vitaldialog.cpp \
    src/appointmentdialog.cpp \
    src/appointmentspage.cpp \
    src/reminderservice.cpp \
    src/smtpclient.cpp \
    src/smtpdialog.cpp \
    src/medassistant.cpp \
    src/icalexport.cpp \
    src/medlogdialog.cpp \
    src/protocoldialog.cpp \
    src/stockdialog.cpp \
    src/wounddialogs.cpp \
    src/upcoming.cpp \
    src/leaflet.cpp \
    src/manual.cpp \
    src/websitedialog.cpp \
    src/usbsyncdialog.cpp \
    src/glucoseprofiledialog.cpp \
    src/medsearchdialog.cpp \
    src/contactdialog.cpp

HEADERS += \
    src/accountdialogs.h \
    src/inactivity.h \
    src/akteexport.h \
    src/documentdialog.h \
    src/documentspage.h \
    src/mailer.h \
    src/formcustomizedialog.h \
    src/mainwindow.h \
    src/medicationdialog.h \
    src/patientdialog.h \
    src/settingsdialog.h \
    src/vitaldialog.h \
    src/appointmentdialog.h \
    src/appointmentspage.h \
    src/reminderservice.h \
    src/smtpclient.h \
    src/smtpdialog.h \
    src/medassistant.h \
    src/icalexport.h \
    src/medlogdialog.h \
    src/protocoldialog.h \
    src/stockdialog.h \
    src/wounddialogs.h \
    src/upcoming.h \
    src/leaflet.h \
    src/manual.h \
    src/websitedialog.h \
    src/usbsyncdialog.h \
    src/glucoseprofiledialog.h \
    src/medsearchdialog.h \
    src/contactdialog.h

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
    FORMS = forms/patientform.ui forms/vitalform.ui forms/medicationform.ui forms/documentform.ui forms/appointmentform.ui
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
# E-Mail-Versand mit Anhang über Simple MAPI (Outlook, Thunderbird …)
win32: SOURCES += src/mapi_win.cpp
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
