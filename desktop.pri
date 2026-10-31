# ---------------------------------------------------------------------------
#  VitaParms – Desktop-Oberfläche (Qt Widgets): Hauptfenster, Dialoge, Erinnerungen.
#  Eingebunden von VitaParms.pro und light/VitaParmsLight.pro (VitaParms light, Schalter VP_LIGHT).
# ---------------------------------------------------------------------------
SOURCES += \
    $$PWD/src/main.cpp \
    $$PWD/src/accountdialogs.cpp \
    $$PWD/src/inactivity.cpp \
    $$PWD/src/akteexport.cpp \
    $$PWD/src/documentdialog.cpp \
    $$PWD/src/documentspage.cpp \
    $$PWD/src/mailer.cpp \
    $$PWD/src/formcustomizedialog.cpp \
    $$PWD/src/mainwindow.cpp \
    $$PWD/src/medicationdialog.cpp \
    $$PWD/src/patientdialog.cpp \
    $$PWD/src/settingsdialog.cpp \
    $$PWD/src/vitaldialog.cpp \
    $$PWD/src/appointmentdialog.cpp \
    $$PWD/src/appointmentspage.cpp \
    $$PWD/src/reminderservice.cpp \
    $$PWD/src/smtpclient.cpp \
    $$PWD/src/smtpdialog.cpp \
    $$PWD/src/medassistant.cpp \
    $$PWD/src/icalexport.cpp \
    $$PWD/src/medlogdialog.cpp \
    $$PWD/src/protocoldialog.cpp \
    $$PWD/src/stockdialog.cpp \
    $$PWD/src/wounddialogs.cpp \
    $$PWD/src/upcoming.cpp \
    $$PWD/src/leaflet.cpp \
    $$PWD/src/manual.cpp \
    $$PWD/src/websitedialog.cpp \
    $$PWD/src/usbsyncdialog.cpp \
    $$PWD/src/glucoseprofiledialog.cpp \
    $$PWD/src/medsearchdialog.cpp \
    $$PWD/src/contactdialog.cpp

HEADERS += \
    $$PWD/src/accountdialogs.h \
    $$PWD/src/inactivity.h \
    $$PWD/src/akteexport.h \
    $$PWD/src/documentdialog.h \
    $$PWD/src/documentspage.h \
    $$PWD/src/mailer.h \
    $$PWD/src/formcustomizedialog.h \
    $$PWD/src/mainwindow.h \
    $$PWD/src/medicationdialog.h \
    $$PWD/src/patientdialog.h \
    $$PWD/src/settingsdialog.h \
    $$PWD/src/vitaldialog.h \
    $$PWD/src/appointmentdialog.h \
    $$PWD/src/appointmentspage.h \
    $$PWD/src/reminderservice.h \
    $$PWD/src/smtpclient.h \
    $$PWD/src/smtpdialog.h \
    $$PWD/src/medassistant.h \
    $$PWD/src/icalexport.h \
    $$PWD/src/medlogdialog.h \
    $$PWD/src/protocoldialog.h \
    $$PWD/src/stockdialog.h \
    $$PWD/src/wounddialogs.h \
    $$PWD/src/upcoming.h \
    $$PWD/src/leaflet.h \
    $$PWD/src/manual.h \
    $$PWD/src/websitedialog.h \
    $$PWD/src/usbsyncdialog.h \
    $$PWD/src/glucoseprofiledialog.h \
    $$PWD/src/medsearchdialog.h \
    $$PWD/src/contactdialog.h

win32: SOURCES += $$PWD/src/mapi_win.cpp   # E-Mail-Versand mit Anhang über Simple MAPI (Outlook, Thunderbird …)
