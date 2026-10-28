# ---------------------------------------------------------------------------
#  VitaParms – gemeinsamer Kern: Datenbank, Verschlüsselung, Fachlogik, Berichte,
#  Website und Datenbank-Abgleich. Wird vom Desktop-Programm (VitaParms.pro) und
#  von der Tablet-App (mobile/VitaParmsMobile.pro) eingebunden.
#
#  Plattformschalter (von der Tablet-App gesetzt):
#    VP_NO_PRINTSUPPORT  – kein Qt Print Support (Android/iOS): Ausgabe nur als PDF
#    VP_NO_UITOOLS       – keine zur Laufzeit geladenen .ui-Formulare
# ---------------------------------------------------------------------------
INCLUDEPATH += $$PWD/src
include($$PWD/3rdparty/sqlite3mc/sqlite3mc.pri)

SOURCES += \
    $$PWD/src/keystore.cpp \
    $$PWD/src/session.cpp \
    $$PWD/src/sqlquery.cpp \
    $$PWD/src/aes256.cpp \
    $$PWD/src/appsettings.cpp \
    $$PWD/src/bmi.cpp \
    $$PWD/src/chartwidget.cpp \
    $$PWD/src/database.cpp \
    $$PWD/src/formloader.cpp \
    $$PWD/src/journalmodel.cpp \
    $$PWD/src/reportprinter.cpp \
    $$PWD/src/vitaldefs.cpp \
    $$PWD/src/xlsxwriter.cpp \
    $$PWD/src/zipwriter.cpp \
    $$PWD/src/sounds.cpp \
    $$PWD/src/speech.cpp \
    $$PWD/src/recurrence.cpp \
    $$PWD/src/medschedule.cpp \
    $$PWD/src/medstock.cpp $$PWD/src/medprep.cpp $$PWD/src/demodata.cpp \
    $$PWD/src/dbtransfer.cpp \
    $$PWD/src/drugclass.cpp \
    $$PWD/src/websync.cpp \
    $$PWD/src/i18n.cpp \
    $$PWD/src/dbsync.cpp \
    $$PWD/src/careinfo.cpp \
    $$PWD/src/contacts.cpp \
    $$PWD/src/usbsync.cpp \
    $$PWD/src/apptcore.cpp \
    $$PWD/src/quarterreminder.cpp \
    $$PWD/src/glucoseprofile.cpp $$PWD/src/peakflow.cpp \
    $$PWD/src/stool.cpp $$PWD/src/protocols.cpp $$PWD/src/inr.cpp $$PWD/src/fluid.cpp $$PWD/src/docfiles.cpp $$PWD/src/akteexportcore.cpp $$PWD/src/pdftools.cpp \
    $$PWD/src/themes.cpp

HEADERS += \
    $$PWD/src/keystore.h \
    $$PWD/src/session.h \
    $$PWD/src/sqlquery.h \
    $$PWD/src/aes256.h \
    $$PWD/src/appsettings.h \
    $$PWD/src/bmi.h \
    $$PWD/src/chartwidget.h \
    $$PWD/src/database.h \
    $$PWD/src/formloader.h \
    $$PWD/src/journalmodel.h \
    $$PWD/src/reportprinter.h \
    $$PWD/src/vitaldefs.h \
    $$PWD/src/xlsxwriter.h \
    $$PWD/src/zipwriter.h \
    $$PWD/src/sounds.h \
    $$PWD/src/speech.h \
    $$PWD/src/recurrence.h \
    $$PWD/src/medschedule.h \
    $$PWD/src/medstock.h $$PWD/src/medprep.h $$PWD/src/demodata.h \
    $$PWD/src/dbtransfer.h \
    $$PWD/src/drugclass.h \
    $$PWD/src/websync.h \
    $$PWD/src/i18n.h \
    $$PWD/src/dbsync.h \
    $$PWD/src/careinfo.h \
    $$PWD/src/contacts.h \
    $$PWD/src/usbsync.h \
    $$PWD/src/apptcore.h \
    $$PWD/src/quarterreminder.h \
    $$PWD/src/glucoseprofile.h $$PWD/src/peakflow.h \
    $$PWD/src/stool.h $$PWD/src/protocols.h $$PWD/src/inr.h $$PWD/src/fluid.h $$PWD/src/docfiles.h $$PWD/src/akteexportcore.h $$PWD/src/pdftools.h \
    $$PWD/src/themes.h \
    $$PWD/src/datatypes.h
