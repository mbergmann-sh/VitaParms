# VitaParms Tablet

Touch-Version von VitaParms für Android-Tablets (iOS folgt). Sie verwendet denselben Kern wie
das Desktop-Programm (`../core.pri`): dieselbe verschlüsselte Datenbank, dieselben Berichte und
den Abgleich über die eigene Website.

**Stand 0.22:** Anmeldung, Personen, Übersicht, Messung erfassen (inkl. Minutenuhr für die
Atemfrequenz, Schmerzskala, freier Messzeitpunkt), Messwerte, Verlaufskurven, Stammblatt mit
Pflegegrad, Schwerbehindertenausweis, Kontakten und Vorsorge (Dokument aus der Krankenakte verknüpfen und öffnen), Medikamentenplan wie am Computer
(Einnahme-Rhythmus, Bedarfs-/Notfallmedikation, PZN, Kennzeichnung, „An Einnahme erinnern“),
Terminplaner mit Serien und Erinnerungen (Android-Benachrichtigungen), BZ-Tagesprofil,
Quartalswechsel-Erinnerung, Krankenakte (ansehen, suchen, mit der Kamera-App fotografieren –
mehrere Seiten werden ein PDF –, Dateien übernehmen, teilen als PDF oder verschlüsseltes ZIP),
Peak-Flow-Warnhinweise (Sollwert nach ERS 1994 bzw. persönlicher Bestwert, biologisches Geschlecht im Stammblatt), Stuhlgang nach der Bristol-Skala (Hinweis nach drei Tagen ohne), INR mit Zielbereich, Ein- und Ausfuhr mit Tagesbilanz, Ödeme, PDF/Excel-Ausgaben inkl. Stuhlprotokoll und BZ-Tagesprofilen, Assistent „Medikamente stellen“ mit Vorlesen (Tages- und Wochendosierer, Protokoll), Medikamentenbestand mit Packungen und Verfallsdatum, Bestelllisten (offen → bestellt → geliefert, PDF), Abgleich über Website und USB.
**Folgt:** Verwaltung (Konten, Sicherung), iOS.

## Voraussetzungen

* Qt **6.5 oder neuer** mit der Komponente *Android* (Qt Maintenance Tool → Qt 6.x → Android),
  dazu *Qt Multimedia* und *Qt TextToSpeech* (optional)
* Qt Creator mit eingerichtetem Android-SDK/NDK
  (*Bearbeiten → Einstellungen → Geräte → Android → „Set Up SDK“* lädt alles Nötige)
* ein Android-Tablet (Android 8 oder neuer) mit **Entwickleroptionen** und **USB-Debugging**:
  *Einstellungen → Über das Tablet → 7 × auf „Build-Nummer“ tippen*, danach
  *Einstellungen → System → Entwickleroptionen → USB-Debugging* einschalten

Die App wird nicht über Google Play verteilt, sondern direkt als APK installiert.

## Bauen und auf das Tablet bringen

1. `mobile/VitaParmsMobile.pro` in Qt Creator öffnen und das Kit **Android Qt 6.x arm64-v8a**
   wählen (für ältere 32-Bit-Tablets zusätzlich *armeabi-v7a*).
2. Das Android-Manifest liegt bei (`android/AndroidManifest.xml`, Grundlage: Qt-6.12-Vorlage):
   Paketname `de.mbergmann.vitaparms`, Symbol `@drawable/icon`, keine Google-Sicherung
   (`allowBackup=false`), dazu die Rechte und Empfänger für die Termin-Erinnerungen
   (`android/src/de/mbergmann/vitaparms/*.java`). **Nicht** mit *Create Templates* überschreiben.
3. Tablet per USB anschließen, die Rückfrage „USB-Debugging zulassen?“ bestätigen.
4. In Qt Creator **Ausführen** (Strg+R). Die App wird gebaut, installiert und gestartet.

**Umstieg von 0.13 (Paket `org.qtproject.example.VitaParms`):** Seit 0.14 heißt das Paket
`de.mbergmann.vitaparms`. Android installiert die neue App deshalb *neben* der alten; die Daten der
alten App übernimmt sie nicht. Vorgehen: (1) mit der alten App am Computer abgleichen (USB oder
Website), (2) die neue App installieren und *Über das USB-Kabel vom Computer übernehmen* bzw.
am Computer *Computer → Tablet…*, (3) die alte App deinstallieren.

**Termin-Erinnerungen:** Beim ersten Anmelden fragt Android (ab Version 13), ob VitaParms
Benachrichtigungen senden darf. Die App stellt für die Erinnerungen der nächsten 14 Tage je einen
Wecker (`AlarmManager`, Recht `USE_EXACT_ALARM`); Android zeigt sie auch an, wenn die App nicht
läuft, und stellt sie nach einem Neustart wieder (`BootReceiver`). Auf dem Sperrbildschirm steht nur
„VitaParms – Termin-Erinnerung“.

**Release-Build:** Android installiert nur signierte Pakete. Ein *Debug*-Build wird automatisch mit
dem Debug-Schlüssel signiert; ein *Release*-Build bleibt unsigniert, bis ein eigener Schlüssel
eingestellt ist – sonst meldet `adb` beim Installieren
`INSTALL_PARSE_FAILED_NO_CERTIFICATES`. Einmalig unter *Projekte → Build (Release) → Build Android
APK → Application Signature*: *Keystore → Erstellen…* (Datei z. B. `vitaparms-release.keystore`
außerhalb des Projektordners, Alias `vitaparms`, Gültigkeit 25 Jahre), dann *Sign package*
anhaken. Keystore und Passwörter gehören **nicht** in das Projekt oder in die Versionsverwaltung;
gut aufbewahren – nur mit demselben Schlüssel lassen sich spätere Versionen als Update installieren.
War vorher eine Debug-Version installiert, diese zuerst auf dem Tablet deinstallieren (anderer
Schlüssel; die Datenbank auf dem Tablet geht dabei verloren – vorher abgleichen oder exportieren).

**Debug läuft, Release stürzt beim Start ab:** Qt Creator speichert die Einstellungen *je
Build-Konfiguration*. Unter *Projekte → Build (Release)* prüfen:
* *Build-Schritte → qmake → ABIs*: dieselbe Architektur wie im Debug-Build (für aktuelle Tablets
  **arm64-v8a**). Wird für eine andere Architektur gebaut als installiert, fehlt im Paket die
  Programmbibliothek, und die App beendet sich sofort („dlopen failed … libVitaParms_….so“).
* danach *Erstellen → Bereinigen* und neu erstellen.

Die Fehlermeldung eines Release-Builds erscheint nicht in Qt Creator, sondern nur im Protokoll des
Tablets: `adb logcat -d -t 1500 > vitaparms-log.txt` (im Ordner `…\Android\Sdk\platform-tools`),
direkt nachdem die App abgestürzt ist.

**HTTPS (Abgleich über die Website):** Android bringt kein OpenSSL für Qt mit. VitaParms liefert
die Bibliotheken deshalb selbst mit (`3rdparty/android_openssl`, OpenSSL 3.1.8 von KDAB,
Apache-2.0-Lizenz); `VitaParmsMobile.pro` nimmt sie für Debug und Release ins Paket – ein Download
in Qt Creator ist nicht nötig. Beim qmake-Lauf erscheinen „OpenSSL fuer Android: …/openssl.pri“
und die einzelnen Bibliotheken („OpenSSL-Bibliothek: …/ssl_3/arm64-v8a/libssl_3.so“). Den Haken
*Include prebuilt OpenSSL libraries* in *Android-APK erstellen* **nicht** zusätzlich setzen (sonst
wären die Bibliotheken doppelt eingebunden). Eine andere OpenSSL-Version: Umgebungsvariable
`VP_ANDROID_OPENSSL` auf einen Ordner mit `openssl.pri` setzen (*Projekte → Build →
Build-Umgebung*).

In der App zeigt *Einstellungen → Abgleich über die Website* unter den Knöpfen, ob TLS bereit ist
(„TLS bereit: OpenSSL 3.1.8 …“) oder was fehlt. Nach einer Änderung an OpenSSL: *Erstellen → Alles
neu erstellen* und die App neu installieren. Ohne OpenSSL schlägt nur der Abgleich über die Website
fehl; alles andere – auch der USB-Abgleich – funktioniert.

Die Meldung *„QML import could not be resolved in any of the import paths: VitaParms“* ist
harmlos: Der Typ `Chart` wird erst beim Programmstart registriert (`qmlRegisterType`), die
Prüfung beim Bauen kennt ihn deshalb nicht.

Ohne Kabel: *Build → Build Android APK* erzeugt
`android-build/build/outputs/apk/debug/android-build-debug.apk`. Diese Datei auf das Tablet
kopieren und dort öffnen (vorher *Installation aus unbekannten Quellen* für den Dateimanager
erlauben). Für eine dauerhafte Installation die APK mit einem eigenen Schlüssel signieren
(*Build Android APK → Sign package*), sonst lässt sich ein Update später nicht über eine ältere
Version installieren.

## Krankenakte: Kamera, Teilen, Vorschau

* **Fotos** macht die Kamera-App des Geräts (`CaptureActivity` startet `ACTION_IMAGE_CAPTURE`,
  das Bild kommt über den FileProvider der App zurück). VitaParms braucht dafür keine
  Kamera-Berechtigung; im Manifest steht nur die Abfrage (`<queries>`) der Kamera-App.
* **Teilen** über das Android-Teilen-Menü (`Capture.share`, FileProvider). Die Freigaben des
  FileProviders stehen in `android/res/xml/qtprovider_paths.xml` (ersetzt die Datei der
  Qt-Vorlage und enthält deren Pfade).
* **Vorschau und Volltext** der PDFs mit Qt PDF, falls installiert (Qt Maintenance Tool:
  *Additional Libraries → Qt PDF*); sonst öffnet „In PDF-App öffnen“ das Dokument.
* Beim Abgleich (Website, USB) gehen die Inhalte getrennt von der Datenbank – nur die fehlenden.

## Erster Start auf dem Tablet

* **Daten vom Computer übernehmen:** Am Computer *Extras → Website…* öffnen, „Datenbank mit
  anderen Geräten abgleichen“ einschalten, *Jetzt abgleichen*. Danach am Tablet Adresse der
  Website und Upload-Schlüssel eingeben – oder die am Computer mit *Kopplungsdatei speichern…*
  erzeugte Datei öffnen. Anschließend mit einem Anmeldekonto des Computers anmelden.
* **Über das USB-Kabel vom Computer übernehmen:** USB-Debugging einschalten (siehe oben), Tablet
  anschließen, am Computer *Datei → Mit Tablet über USB abgleichen…*, den Code vom Tablet
  eingeben, *Verbinden*, *Computer → Tablet…*. Danach mit einem Konto des Computers anmelden.
* **Datenbank-Datei übernehmen (ohne Website):** am Computer *Datei → Datenbank exportieren (für ein
  anderes Gerät)…* erzeugt eine `.vpdb`-Datei (Datenbank + Anmeldekonten); per USB oder Cloud aufs
  Tablet bringen und hier auswählen. Auch im Anmeldefenster: *Andere Datenbank importieren…*
* **Neu beginnen:** legt eine eigene Datenbank auf dem Tablet an (Administrator-Konto und
  Wiederherstellungsschlüssel wie am Computer).

## Abgleich

Tablet und Computer tauschen die verschlüsselte SQLite-Datenbank über `api/sync.php` der eigenen
Website aus. Zusammengeführt wird auf dem Gerät, Zeile für Zeile: neue Einträge beider Seiten
bleiben erhalten, bei gleichzeitiger Änderung derselben Angabe gilt die jüngere, Löschungen
werden übertragen. Gerätebezogen bleiben Einstellungen und „schon erinnert“ bei Terminen.
Abgeglichen wird automatisch nach Änderungen (nach einer Minute), beim Öffnen der App, alle zehn
Minuten und beim Sperren.

**Über das USB-Kabel** (ohne Website): am Tablet *Einstellungen → USB-Abgleich mit dem Computer*
öffnen (zeigt einen Code), Tablet anschließen, am Computer *Datei → Mit Tablet über USB
abgleichen…*, Code eingeben, *Verbinden*, *Abgleichen*. Es gelten dieselben Regeln. Der Computer
braucht das Android-Hilfsprogramm `adb` (SDK Platform-Tools); es wird im Android-SDK
(`%LOCALAPPDATA%\Android\Sdk`, `C:\Android\Sdk`, `ANDROID_SDK_ROOT`) und im Suchpfad gesucht oder
im Dialog ausgewählt. Auf dem Tablet muss USB-Debugging eingeschaltet sein. Die App nimmt
Verbindungen nur auf `127.0.0.1` an, also nur über `adb forward` – nicht aus dem WLAN.

## Am Computer testen

Die App lässt sich mit einem normalen Desktop-Kit bauen und starten (Fenstergröße wie ein
Tablet im Querformat). Für Bildschirmfotos und automatische Prüfungen gibt es einen Testbau:

```
qmake CONFIG+=autopilot VitaParmsMobile.pro && make
VP_AUTOPILOT_DB=… VP_AUTOPILOT_USER=… VP_AUTOPILOT_PASSWORD=… VP_AUTOPILOT_OUT=shots ./VitaParms
```

## Aufbau

```
VitaParmsMobile.pro   Projekt (bindet ../core.pri ein)
src/main.cpp          Start, Schrift, Lebenszyklus (Sperre/Abgleich beim Wechsel in den Hintergrund)
src/appcontroller.*   Brücke Kern ↔ QML (Personen, Messungen, Medikamente, Ausgaben, Abgleich)
src/appcontroller_appts.cpp  Terminplaner (Liste, Eingabe, Serien – Logik aus ../src/apptcore.*)
src/tabletreminders.* Termin-Erinnerungen planen (→ Java/AlarmManager)
src/chartitem.*       Verlaufskurven als QML-Element (gleiche Zeichenroutine wie am Desktop)
qml/Theme.qml         Farben, Schrift, Maße (hell/dunkel)
qml/components/       Bedienelemente (große Trefferflächen, Ziffernfelder, Chips …)
qml/pages/            Seiten (Einrichtung, Anmeldung, Übersicht, Messung, Verlauf …)
fonts/                Atkinson Hyperlegible (SIL Open Font License 1.1)
android/AndroidManifest.xml  Manifest (Paketname, Rechte, Empfänger für Erinnerungen)
android/src/          Java: Erinnerungen (Reminders, ReminderReceiver, BootReceiver)
android/res/          App-Symbole, Symbol für die Statusleiste
translations/         Englisch
```
