# VitaParms light – Installer (Qt Installer Framework)

Ein **Release-Build** von `light/VitaParmsLight.pro` bereitet alles automatisch vor – gesteuert von
`light_deploy.pri`:

| Schritt | Windows | Linux | macOS |
|---|---|---|---|
| Programm + Qt-Bibliotheken ins Paket `qtifw/packages/de.mbergmann.vitaparms.light/data` | `windeployqt` | `linux_deploy.sh` (per `ldd`: `lib/`, `plugins/`, `qt.conf`, Startskript `VitaParmsLight.sh`) | `macdeployqt` (`VitaParmsLight.app`) |
| Handbücher ins Paket `…light.docs/data/docs` | PDF + CHM | PDF | PDF |
| Installer (falls `binarycreator` gefunden) | `Output/VitaParms-light-<Version>-Setup.exe` | `Output/VitaParms-light-<Version>-Setup.run` | `Output/VitaParms-light-<Version>-Setup.dmg` |

Installationsordner (änderbar im Installer):

* Windows: **`C:\VPlight`** – Startmenü und Desktop-Verknüpfung
* macOS: `/Applications/VPlight`
* Linux: `~/VPlight` – Eintrag im Anwendungsmenü (`~/.local/share/applications`)

Komponenten: *Programm* (fest) und *Handbücher* (abwählbar). Deinstallieren und Aktualisieren über
das mitinstallierte Wartungsprogramm (`maintenancetool`). Daten und Einstellungen liegen getrennt
von VitaParms im Benutzerprofil (`VitaParmsLight`) und bleiben bei Deinstallation und Update erhalten.

## Werkzeuge

* **Qt Installer Framework 4.x** – im Qt Maintenance Tool unter *Developer and Designer Tools*.
  Gesucht über `VP_BINARYCREATOR` (Pfad zur Datei), `QTIFWDIR` und
  `<Qt>/Tools/QtInstallerFramework/<Version>/bin`. Werkzeuge werden beim **qmake-Lauf** gesucht.
* Linux: `ldd` und `patchelf` sind nicht nötig; `linux_deploy.sh` kopiert die Qt-Bibliotheken und setzt
  `LD_LIBRARY_PATH` im Startskript.
* macOS: `hdiutil` (gehört zum System) für das Disk-Image.

Installer nicht bei jedem Release-Build erzeugen: `qmake CONFIG+=vp_no_installer`. Von Hand:

```sh
binarycreator --offline-only -c qtifw/config/config.xml -p qtifw/packages Output/VitaParms-light-Setup.run
```

Die Versionsnummer kommt aus `version.pri`; qmake schreibt sie in `config.xml` und die
`package.xml`-Dateien (Vorlagen: `*.in`). **Änderungen bitte in den `.in`-Dateien vornehmen.**

## Android (Tablet und Smartphone)

`light/mobile/VitaParmsLightMobile.pro` baut die App mit dem Paketnamen
`de.mbergmann.vitaparms.light` (grünes Symbol). Die Android-Dateien in `light/mobile/android` erzeugt
`tools/make_light_android.py` aus `mobile/android`. Signieren wie bei der großen App – Keystore und
Passwörter gehören **nicht** ins Projekt.

## Getestet

Der Linux-Installer wurde mit IFW 4.8.1 erzeugt und installiert (Programm, Handbücher, Menüeintrag,
Deinstallation). Windows- und macOS-Installer sowie die Android-App sind vorbereitet, aber noch nicht
auf echten Systemen geprüft.
