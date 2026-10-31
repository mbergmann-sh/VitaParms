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

TARGET = VitaParms
include(mobile.pri)
