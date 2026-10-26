# VitaParms – Windows-Installer

Ein Release-Build unter Windows (Qt Creator oder Konsole, MinGW oder MSVC) erledigt alles
automatisch – gesteuert von `installer/windows_deploy.pri`, das `VitaParms.pro` unter Windows einbindet:

| Schritt | Ergebnis |
|---|---|
| `windeployqt` nach **jedem** Build (Debug und Release) | Qt-DLLs, Plugins und Übersetzungen neben `VitaParms.exe` – das Programm startet direkt aus dem Build-Ordner |
| Installationsverzeichnis (nur Release) | `installer/install_src`: `VitaParms.exe`, alle DLLs und windeployqt-Unterordner, `docs/` (Handbücher PDF + CHM), `LICENSE.txt`, `README.md` – ohne Makefiles, Objektdateien, `.qmake.stash` |
| Inno Setup (falls installiert) | `installer/Output/VitaParms-<Version>-Setup.exe` |
| Qt Installer Framework (falls installiert) | `installer/Output/VitaParms-<Version>-Setup-IFW.exe` |

Die Versionsnummer kommt aus `VERSION` in `VitaParms.pro`; qmake schreibt sie bei jedem Lauf in
`VitaParms.iss` und die IFW-Dateien `config.xml`/`package.xml` (Vorlagen: `*.in`).
**Änderungen bitte immer in den `.in`-Dateien vornehmen.**

## Werkzeuge

* **Inno Setup 6.3 oder neuer** – https://jrsoftware.org/isinfo.php
  Gesucht in `C:\Program Files (x86)\Inno Setup 6`, `C:\Program Files\Inno Setup 6` und
  `%LOCALAPPDATA%\Programs\Inno Setup 6`; sonst Umgebungsvariable `VP_ISCC` auf `ISCC.exe` setzen.
* **Qt Installer Framework 4.x** – im Qt Maintenance Tool unter *Developer and Designer Tools*.
  Gesucht in `<Qt>\Tools\QtInstallerFramework\<neueste Version>\bin` und über `QTIFWDIR`;
  sonst Umgebungsvariable `VP_BINARYCREATOR` auf `binarycreator.exe` setzen.

Werkzeuge werden beim **qmake-Lauf** gesucht (Qt Creator: *Erstellen → qmake ausführen*) – die
Meldungen „Inno Setup gefunden …“ bzw. „… nicht gefunden“ stehen in der Ausgabe.
Installer nicht bei jedem Release-Build erzeugen: `qmake CONFIG+=vp_no_installer`; dann
`VitaParms.iss` von Hand übersetzen (Inno Setup öffnen, F9) bzw.
`binarycreator --offline-only -c qtifw\config\config.xml -p qtifw\packages Output\VitaParms-Setup-IFW.exe`.

Die Befehle verwenden Pfade mit `/` in Anführungszeichen und kopieren mit `qmake -install qinstall`
statt `copy`/`xcopy`/`cp` – sie funktionieren deshalb unverändert mit `cmd.exe` und mit einer `sh.exe`,
die eine fremde Toolchain (z. B. ein Cross-Compiler) in den `PATH` bringt.

## Inno-Setup-Installer (`VitaParms.iss`)

* Sprachauswahl Deutsch/Englisch, Willkommensseite mit Bild, Lizenz und Haftungshinweis zum Annehmen
* Zielordner wählbar, Vorgabe **`C:\VitaParms`**; Komponenten: Programm (fest) und Handbücher (abwählbar)
* Startmenü: VitaParms, VitaParms Demo (`--demo`), Handbuch (CHM in Installationssprache), Deinstallieren;
  Desktop-Verknüpfung wählbar; am Ende „VitaParms starten“ bzw. „Demo öffnen“
* **Updater:** Ist VitaParms schon installiert, fragt das Setup: *Aktualisieren bzw. neu installieren*
  (gleiche AppId → Update im bisherigen Ordner) oder *Deinstallieren*
* **Deinstaller:** entfernt Programm, Verknüpfungen und die Demo-Datenbank; die eigenen
  Datenbanken, Schlüsseldateien und Einstellungen bleiben erhalten
* Getestet: Übersetzen mit ISCC 6.5.4, stille Installation, Update-Abfrage, Willkommens- und Lizenzseite
  (unter Wine). Ein Test auf einem echten Windows-Rechner steht noch aus.

## Qt-Installer-Framework-Installer (`qtifw/`)

* Komponenten *VitaParms* (Pflicht) und *Handbücher* (abwählbar), Lizenz und Haftungshinweis
* Zielordner Vorgabe **`C:\VitaParms`**, Startmenü- und Desktop-Verknüpfungen, Demo-Verknüpfung
* Wartungsprogramm `VitaParms-Wartung.exe` (Komponenten ändern, deinstallieren)
* **Update mit einem neueren Offline-Installer:** Das Steuerskript erkennt eine vorhandene Installation,
  entfernt sie nach Rückfrage mit dem Wartungsprogramm (`purge`) und installiert die neue Version in
  denselben Ordner. Daten und Einstellungen liegen im Benutzerprofil und bleiben erhalten.
* `config.xml` ist gegen die Elementliste des IFW-Quelltexts geprüft, die Skripte gegen dessen Skript-API.
* Schlägt der Bau eines Installers fehl, wird das gemeldet ("Error … (ignored)"), der Build selbst gilt aber als erfolgreich.

## Installergröße

* **Schlanke Bereitstellung:** `windeployqt` lässt weg, was eine reine Widgets-Anwendung nicht braucht:
  `opengl32sw.dll`, `d3dcompiler_47.dll`, ab Qt 6.8 `dxcompiler.dll`/`dxil.dll` sowie das
  FFmpeg-Multimedia-Backend mit den FFmpeg-DLLs (die Signaltöne spielt das Windows-Media-Backend);
  Qt-Übersetzungen nur Deutsch. Das verkleinert **beide** Installer deutlich.
  Alles wie von windeployqt vorgesehen bereitstellen: `qmake CONFIG+=vp_full_deploy`.
* **Kompression:** Inno Setup `lzma2/ultra64`, Qt Installer Framework `--compression 9` (7z Ultra).
* **Bleibender Unterschied:** Ein IFW-Installer enthält seine eigene Laufzeit (`installerbase`, ein
  statisch gelinktes Qt mit Assistent, Paketverwaltung und Wartungsprogramm) – je nach IFW-Version
  rund 10–20 MB, die nicht weiter schrumpfen. Der Inno-Setup-Lader ist nur gut 2 MB groß. Der
  IFW-Installer bleibt deshalb immer etwas größer; dafür bringt er das Wartungsprogramm mit.

## Hinweise

* Nach einem Wechsel der Qt-Version baut der nächste Release-Build `install_src` und die IFW-Paketdaten
  vollständig neu auf (alte DLLs werden vorher gelöscht).
* Die Bilder `wizard_*.bmp` (100 %/200 %) und `qtifw/config/*.png` sind aus dem Programmsymbol erzeugt
  und können durch eigene ersetzt werden (Inno: 164 × 314 und 55 × 55 Pixel).
* Windows sperrt heruntergeladene Hilfedateien (.chm): Installer bzw. ZIP vor dem Ausführen unter
  *Eigenschaften → Zulassen* entsperren, falls die Hilfe „Die Navigation zu der Webseite wurde abgebrochen“ meldet.
