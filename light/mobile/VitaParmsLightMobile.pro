# ---------------------------------------------------------------------------
#  VitaParms light – App für Tablet und Smartphone (Android; Qt Quick)
#  Copyright (c) 2026 Michael Bergmann – MIT License (siehe ../../LICENSE)
#
#  Dieselbe Touch-Oberfläche wie die Tablet-App von VitaParms (../../mobile), mit dem Schalter
#  VP_LIGHT: eine Person, vereinfachte Messung (Blutdruck, Puls, Temperatur, SpO₂, Blutzucker, Gewicht,
#  Größe, BMI, Schmerz), Abgleich nur per USB-Kabel mit VitaParms light am Computer.
#  Eigenes Android-Paket: de.mbergmann.vitaparms.light (läuft neben VitaParms).
#
#  Bauen: Qt Creator → dieses Projekt öffnen → Kit "Android Qt 6.x arm64-v8a" → Ausführen.
#  Am Desktop lässt sich die App mit einem normalen Kit testen.
# ---------------------------------------------------------------------------
DEFINES += VP_LIGHT
TARGET = VitaParmsLight
ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
VP_MOBILE_DOCS = $$PWD/../docs/VitaParms-light-ErsteSchritte.pdf $$PWD/../docs/VitaParms-light-GettingStarted.pdf
VP_SOURCE_DOCS_DIR = $$clean_path($$PWD/../docs)
include(../../mobile/mobile.pri)
RESOURCES += ../light.qrc
