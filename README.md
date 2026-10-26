# VitaParms

Tagebuch für Vitalwerte mit Verlauf, Grafiken, Druck (DIN A4), PDF- und Excel-Export.
Qt 6 / C++17 / qmake – MIT-Lizenz.

> **Kein Medizinprodukt.** VitaParms ist ein persönliches Tagebuch für gesundheitsbezogene
> Messwerte. Es stellt keine Diagnosen und ersetzt keine ärztliche Beratung.
> Die Verwendung erfolgt auf eigene Gefahr.

## Funktionen

| Bereich | Umsetzung |
|---|---|
| **Stammblatt** | Name, Vorname, Geburtsdatum, Geschlecht (m/w/d), Adresse, Telefon, Mobil, Diagnosen, Bemerkungen, Allergien, Notfallkontakt, Hausarztpraxis, Krankenkasse; Angaben zu fehlenden Gliedmaßen; Vitalwerte bei Aufnahme |
| **Medikamentenplan** | gegliedert in **Regel-, Bedarfs-, Notfallmedikation und Sonstiges**; Präparat, Wirkstoff, Stärke, Form, **PZN**, Dosierschema mo-mi-ab-na, Einheit, **Einnahme-Rhythmus** (täglich, alle N Tage, an Wochentagen, monatlich) mit **Bemerkung**, **„An Einnahme erinnern“** (Tage und Uhrzeiten, je Uhrzeit eine Terminserie in der Terminliste), Hinweise, Grund, **Zeitraum von–bis** (Standard: dauerhaft, ohne Enddatum; nach Ablauf des Enddatums wandert das Medikament automatisch in die **Historie** und aus dem aktuellen Plan); **Absetzen** mit Datum (auch vorgemerkt für ein künftiges Datum) und Grund – das Medikament wandert ins Archiv, die Historie bleibt erhalten, druckbar; per Klick auf die Spaltenköpfe **auf-/absteigend sortierbar** – nur innerhalb der jeweiligen Art, ohne Umverteilung |
| **Medikamente suchen** | **BtM**, **Schmerzmittel**, **Blutverdünner/Antikoagulanzien**, **Blutdrucksenker**, **kortisonhaltige Präparate** (oder Freitext) im aktuellen Plan – mit Dosierung, Einnahme, **Menge und Wirkstoffmenge pro Tag**; druckbar/PDF |
| **Beipackzettel** | je Medikament im Internet nachschlagen (Apotheken Umschau per PZN, Gelbe Liste, Websuche, EMA, openFDA) und anzeigen; Original-PDF per Link oder Datei laden und **verschlüsselt in der Krankenakte** speichern |
| **Benutzer** | beliebig viele, Auswahl über die Werkzeugleiste; **Hauptbenutzer 1 und 2** per `Strg+1` / `Strg+2`; *Benutzer → Drucken*: Stammblatt, Medikamentenliste, Erinnerungen (anstehende Termine) |
| **Messung erfassen** | Kopfzeile mit Name/Vorname/Geburtsdatum aus dem Stammblatt; Datum/Uhrzeit vorbelegt und änderbar; Messzeitpunkt für **Tagesprofile** (nüchtern, 2 h nach dem Frühstück …, Liste editierbar); „Speichern & weitere Messung“ für Serien; **Minutenuhr** neben „Atemfrequenz“ (Start- und Endton) zum Zählen der Atemzüge |
| **Vitalwerte** | Puls, Blutdruck sys/dia, Körpertemperatur, SpO₂, **Peak Flow** (l/min), Blutzucker (mg/dl oder mmol/l), Gewicht, Körpergröße (bei neuen Messungen mit der bekannten Größe vorbelegt), **BMI automatisch** – bei fehlenden Gliedmaßen korrigiert (Osterkamp 1995), Beobachtungen; Beispiel-Zusatzfelder Atemfrequenz und Schmerz (NRS) |
| **BZ-Tagesprofil** | *Messwerte → BZ-Tagesprofil…*: sieben Zeitpunkte (nüchtern vor dem Frühstück … vor dem Zubettgehen) mit einstellbaren Uhrzeiten; je Zeitpunkt ein Termin mit Erinnerung, „Messung erfassen…“ öffnet das Erfassungsfenster mit Betreff, Uhrzeit und Messzeitpunkt |
| **Journal / Verlauf** | alle Werte, nach Datum absteigend (aktuellste zuerst), mehrere Messungen pro Tag, Zeitraumfilter |
| **Grafik** | Tages-, Wochen-, Monats-, **Jahresverlauf** und freier Zeitraum (Wahl wird gespeichert); ab gut zwei Monaten **Tageswerte** (Mittelwert, Strich = Min–Max); Bereiche wählbar, **jede Kurve einzeln ein-/ausblendbar** (Klick auf die Legende oder Menü *Kurven*); Druck/PDF wahlweise **je Bereich eine Seite**; farblich unterscheidbar **und** mit unterschiedlichen Markerformen (auch bei Farbfehlsichtigkeit lesbar), Legende, Tooltips |
| **Ausgabe** | Druck und Druckvorschau auf **DIN A4**, **PDF**, **Excel (.xlsx)** mit eingebetteter Grafik, Kennwerten (Min/Max/Mittel) und Rohdaten |
| **Krankenakte** | PDF-Dokumente (Befunde, Laborwerte, Entlassbriefe …) je Benutzer nach Datum archivieren; Fotos/Scans (JPG, PNG, TIFF) werden beim Import in PDF umgewandelt; Drag & Drop; Vorschläge für Datum, Art und Titel; Dubletten-Erkennung; **Volltextsuche** im PDF-Inhalt; Vorschau; **selektiver Druck**; Inhaltsverzeichnis drucken |
| **Akte versenden** | ausgewählte/gefilterte/alle Dokumente plus Stammblatt, Medikamentenplan, Verlauf und Grafik als **ZIP mit Inhaltsverzeichnis**; optional **AES-256-verschlüsselt**; E-Mail mit Anhang im Standard-Mailprogramm |
| **Datenschutz** | Datenbank **vollständig verschlüsselt** (SQLite3 Multiple Ciphers, ChaCha20-Poly1305) inkl. aller Dokumente; Sicherungen ebenfalls verschlüsselt |
| **Anmeldekonten** | Anmeldung beim Start; beliebig viele Konten mit den Rollen **Administrator** und **Anwender**; nach korrekter Anmeldung wird die Datenbank ohne Schlüsseleingabe entsperrt; Wiederherstellungsschlüssel für den Notfall; Abmelden/Sperren (`Strg+L`) |
| **Termine** | **Erinnerung an den Quartalswechsel** (KV-Karte in den Praxen einlesen lassen: zwei Tage vor bis zwei Tage nach dem Wechsel, bis bestätigt; abschaltbar); Terminplaner je Benutzer (Tag/Woche/Monat/alle kommenden), Kalender mit Prioritätsmarken; **Prioritäten** niedrig/normal/hoch/dringend; Anlegen, Bearbeiten, Löschen, Erledigt; **Terminserien** (täglich, werktags, wöchentlich an Wochentagen, monatlich am Tag/n-ten/letzten Wochentag, jährlich; mit Intervall und Ende); **iCalendar-Export** (.ics) für Kalender-Apps; Administratoren sehen alle Termine, Anwender nur eigene; **beim Programmstart** Hinweisfenster mit den Terminen der gewählten Person für heute und morgen (abschaltbar) |
| **Erinnerungen** | Vorwarnung einstellbar (zum Termin … 1 Woche vorher), **optisch** (Hinweisfenster mit „Später erinnern“), **akustisch** (Gong) und/oder **per E-Mail** (SMTP mit STARTTLS/SSL); verpasste Erinnerungen erscheinen nach dem nächsten Anmelden |
| **Medikamente stellen** | Assistent liest die aktuelle Medikamentenliste mit **wählbarer Stimme (weiblich/männlich)** vor – Präparat, Dosis, Zeitpunkt; „**Weiter**“ zum nächsten, „**Wiederholen**“ für dasselbe Medikament; Filter nach Zeitpunkt (morgens …); am Ende optischer und akustischer Hinweis **„Medikamente sind gerichtet“**; **Protokoll „gestellt am/von“** mit Druck/PDF |
| **Automatische Sperre** | nach 10/20/30/60 Minuten ohne Benutzung (Einstellungen) |
| **Sprachen** | Deutsch und Englisch, Auswahl in den Einstellungen (oder Systemsprache) |
| **Themes** | alle Qt-Styles der Plattform sowie „Dark“ und „Visual Studio Code Dark“ aus AmigaED 4.0 – sofort umschaltbar |
| **Daten** | Sicherung/Wiederherstellung, mehrere Datenbanken möglich |
| **Demo** | *Hilfe → Demo-Datenbank starten*: zweites Fenster mit einer erfundenen Person und **365 Tagen** Messwerten bis heute, Medikamentenplan, Terminen und Befunden; eigene verschlüsselte Datenbank und eigene Einstellungen, bei jedem Start neu erzeugt (auch `VitaParms --demo`) |
| **Website** | *Extras → Website…*: die Daten **aller Personen** (gesamter Messwert-Verlauf mit Verlaufskurven, Medikamente, Termine, Berichte als PDF/Excel – **ohne Krankenakte**) auf der eigenen Website, geschützt durch ein Master-Passwort; Ende-zu-Ende-verschlüsselt (AES-256, Entschlüsselung erst im Browser), automatisch aktualisiert (*Datei → Website aktualisieren*, `Strg+Umschalt+U`). Siehe `website/README.md` |
| **Übertragen** | *Datei → Datenbank exportieren* (eine `.vpdb`-Datei mit Datenbank und Anmeldekonten) und **Import bei der Anmeldung** (*Datenbank importieren…*, auch beim ersten Start, am Computer und auf dem Tablet) – ohne Website, z. B. per USB-Kabel |
| **Tablet** | **Tablet-App für Android** (Qt Quick, `mobile/`): Messungen erfassen (Messzeitpunkt auch als Freitext), Verläufe, Kurven, Stammblatt, Medikamentenplan, Kontakte und Vorsorge bearbeiten, **Terminplaner** mit Serien, **BZ-Tagesprofil**, Quartalswechsel-Erinnerung und **Erinnerungen als Android-Benachrichtigung** – mit derselben verschlüsselten Datenbank; **Abgleich** über die eigene Website (*Datei → Datenbank abgleichen*, `Strg+Umschalt+A`) oder direkt **über das USB-Kabel** (*Datei → Mit Tablet über USB abgleichen…*, per `adb`; auch Computer → Tablet bzw. Tablet → Computer ersetzen), zeilenweises Zusammenführen. Siehe `mobile/README.md` |
| **Bestand** | *Benutzer → Medikamentenbestand und Bestellungen*: Packungen mit Stückzahl und **Verfallsdatum**, Abbuchung beim **Stellen** (zuerst ablaufende Packung), Reichweite nach Dosierschema und Rhythmus, **Bestellen bis** = Reichweite − Vorlauf (Standard 7 Tage, Wochenende → Freitag), Warnung „ist fast aufgebraucht – bitte Verordnung besorgen und bestellen!“, gespeicherte **Bestelllisten** (Druck/PDF), Erinnerung bis zur Bestätigung „bestellt“, Lieferung bucht ein |
| **Kontakte** | Im Stammblatt (*Weitere Angaben: Kontakte*): beliebig viele Kontakte – **Krankenkasse mit Versichertennummer**, Hausarztpraxis, Fachärzte, Kliniken, Pflegedienst, Apotheke, Notfallkontakt, Sonstige –, je mit Anschrift, Telefon, Mobil, E-Mail und Website; **scrollbare Liste mit Suchfeld**, *Hinzufügen / Bearbeiten / Entfernen*. Die früheren Zusatzfelder (Krankenkasse, Hausarztpraxis, Praxis-Telefon, Notfallkontakt) werden automatisch übernommen und für ältere Versionen weiter mitgeschrieben. Anzeige im Stammblatt-Ausdruck, auf Apotheken- und Bestellliste, auf der Website und im Tablet |
| **Vorsorge** | Im Stammblatt (*Pflege, Ausweis und Vorsorge*): **Patientenverfügung, Vorsorgevollmacht, Betreuungsvollmacht** – je vorhanden ja/nein, eingetragene Person mit Anschrift und Telefon, gültig bis; jeweils **PDF hinterlegbar** (verschlüsselt in der Krankenakte); Anzeige im Kopf, im Stammblatt-Ausdruck, auf der Website und im Tablet |
| **Apotheke** | *Medikationsliste für die Apotheke*: alle aktuellen Medikamente mit Wirkstoff und PZN, dazu Gewicht, Allergien, Diagnosen, Ankreuzliste für frei verkäufliche Mittel und Platz für Notizen – damit die Apotheke Wechselwirkungen prüft (VitaParms selbst prüft bewusst nicht; Hinweis auf die Medikationsberatung bei Polymedikation). Desktop, Website und Tablet |
| **Pflege** | **Pflegegrad** (mit nächster Überprüfung) und **Schwerbehindertenausweis** (GdB, Merkzeichen G, aG, B, H, Bl, Gl, TBl, RF, 1. Kl., Gültigkeit) im Stammblatt, im Kopf des Hauptfensters, im Ausdruck, auf der Website und in der Tablet-App |
| **Hilfe** | **Handbuch** unter *Hilfe → Handbuch* bzw. `F1` in der Programmsprache: unter Windows als **Windows-Hilfe (.chm)** mit Inhalt, Stichwortverzeichnis und Volltextsuche, sonst als PDF im Standardanzeiger; Dateien in `<Programmordner>/docs`. Kurzanleitung **Erste Schritte** (*Getting Started*) mit Beispielen unter *Hilfe → Erste Schritte*, im Anmeldedialog, beim ersten Start und in der Tablet-App (*Einstellungen*) – auch ohne Anmeldung. Beim allerersten Start wählt man die **Sprache** (Desktop und Tablet) |
| **Formulare** | Stammblatt, Messung und Medikament sind **Qt-Designer-Dateien, die zur Laufzeit geladen werden** – vom Benutzer veränderbar, inkl. eigener Felder ohne Neuübersetzen |

## Handbuch

Zu VitaParms gibt es ein bebildertes Benutzerhandbuch als LaTeX-Projekt (pdfLaTeX, KOMA-Script),
auf Deutsch (*VitaParms-Handbuch*) und auf Englisch (*VitaParms-Manual*), jeweils mit fertigem PDF.

**Im Programm (ab 0.7.7):** *Hilfe → Handbuch* (`F1`) öffnet das Handbuch in der Programmsprache aus dem
Ordner `docs` neben dem Programm (macOS: `VitaParms.app/Contents/Resources/docs`, Linux nach `make install`:
`/usr/local/share/vitaparms/docs`). qmake kopiert `docs/` beim Bauen automatisch neben das Programm.

| Datei | Verwendung |
|---|---|
| `docs/VitaParms-Handbuch.pdf`, `docs/VitaParms-Manual.pdf` | alle Systeme, Standard-PDF-Anzeiger |
| `docs/VitaParms-Handbuch.chm`, `docs/VitaParms-Manual.chm` | Windows-Hilfe (`hh.exe`); fehlt sie, wird das PDF geöffnet |
| `docs/VitaParms-ErsteSchritte.pdf/.chm`, `docs/VitaParms-GettingStarted.pdf/.chm` | Kurzanleitung (*Hilfe → Erste Schritte*); die PDFs gehen auch ins Android-Paket (`assets:/docs`) |

### Erste Schritte / Getting Started

Die Kurzanleitung ist ein eigenes LaTeX-Projekt je Sprache: `VitaParms-ErsteSchritte` (`erste-schritte.tex`)
und `VitaParms-GettingStarted` (`getting-started.tex`), jeweils mit `Makefile` und `latexmkrc` (`make` bzw.
`latexmk -pdf`). Sie arbeitet mit der Demo-Datenbank und den Beispielpersonen Erika und Max Mustermann;
die nummerierten Markierungen in den Bildschirmfotos (`gs_*.png`) erzeugt der Testlauf.

Die CHM-Dateien entstehen aus denselben LaTeX-Quellen wie das PDF – ein Kapitel je Seite, Inhaltsverzeichnis,
Stichwortverzeichnis aus den `\index`-Einträgen, Volltextsuche; TikZ-Grafiken und Formeln als Bild:

```
python3 tools/make_chm.py ../VitaParms-Handbuch      # → docs/VitaParms-Handbuch.chm
python3 tools/make_chm.py ../VitaParms-Manual        # → docs/VitaParms-Manual.chm
python3 tools/make_chm.py ../VitaParms-ErsteSchritte # → docs/VitaParms-ErsteSchritte.chm
python3 tools/make_chm.py ../VitaParms-GettingStarted # → docs/VitaParms-GettingStarted.chm
```

Benötigt werden pandoc, pdflatex, pdftoppm und ein CHM-Compiler: `chmcmd` (Free Pascal, Debian/Ubuntu:
`sudo apt install fp-utils`) oder `hhc.exe` aus dem Microsoft HTML Help Workshop. Hinweis: Windows sperrt
CHM-Dateien aus dem Internet oder von Netzlaufwerken – ZIP vor dem Entpacken unter *Eigenschaften →
Zulassen* entsperren.

**Peak Flow (ab 0.7.4):** fester Messwert nach SpO₂ (l/min) mit eigenem Diagrammbereich. Werte aus einem
früheren Zusatzfeld „Peak-Flow“ (Formular-Eigenschaft `vpField`) werden beim ersten Öffnen der Datenbank
(Schema 6) übernommen.

**Englische Eingaben (ab 0.7.3):** Die Medikamentensuche erkennt auch englische bzw. internationale
Wirkstoffnamen (z. B. *oxycodone*, *acetaminophen*, *acetylsalicylic acid*), englische Einheiten
(*IU*, *tablets*, *puffs* …) und Darreichungsformen (*drops*, *prolonged-release*); Gründe wie *pain*
oder *migraine* werden berücksichtigt. Beim Import der Krankenakte werden Datumsangaben wie
30/09/2026, 2026-09-30, „30 September 2026“ und „September 30, 2026“ sowie englische Dokumentarten
(*discharge*, *laboratory*, *referral* …) erkannt. Die Beispielfelder der Standardformulare
(Atemfrequenz, Krankenkasse, Notfallkontakt …) erscheinen in der gewählten Sprache.

## Bauen

Voraussetzung: **Qt ≥ 6.4** mit den Modulen *Widgets, PrintSupport, UiTools* sowie *Linguist-Werkzeuge*
(`lrelease`, für die eingebettete Übersetzung). UiTools und Linguist gehören zu `qttools` und sind im
Qt-Online-Installer standardmäßig enthalten. Das Qt-Modul *SQL* wird **nicht** mehr benötigt: Die
verschlüsselnde SQLite-Variante liegt als Quelltext in `3rdparty/sqlite3mc` und wird mitübersetzt
(der erste Build dauert dadurch etwa eine Minute länger).

Empfohlen: **Qt PDF** (`pdf`, `pdfwidgets`, LGPLv3) – im Qt-Online-Installer unter
„Additional Libraries → Qt PDF“, unter Debian/Ubuntu `qt6-pdf-dev`. Damit gibt es Vorschau,
Volltextsuche und direkten Druck der Dokumente. Ohne Qt PDF baut das Projekt trotzdem
(qmake meldet die Variante); Dokumente werden dann im Standard-PDF-Programm geöffnet.

```sh
qmake6 VitaParms.pro      # bzw. qmake aus der Qt-6-Installation
make -j                   # Windows/MSVC: nmake oder jom
```

Ebenfalls optional:

* **Qt TextToSpeech** (`texttospeech`, LGPLv3) für das Vorlesen im Medikamenten-Assistenten.
  Gesprochen wird mit den Stimmen des Betriebssystems (Windows: SAPI/WinRT, macOS: AVSpeech,
  Linux: speech-dispatcher oder flite). Fehlt eine deutsche Stimme, liest das System mit der
  vorhandenen Stimme – der Assistent weist darauf hin. Ohne das Modul zeigt der Assistent nur an.
* **Qt Multimedia** (`multimedia`) für die Signaltöne; sonst Systempiepton.

```
Debian/Ubuntu: sudo apt install qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
               qt6-pdf-dev qt6-speech-dev qt6-multimedia-dev qt6-declarative-dev qmake6
               (deutsche Stimmen: speech-dispatcher-espeak-ng; qt6-declarative-dev nur wegen
                eines Header-Fehlers der Qt-6.4-Pakete von TextToSpeech)
```

**Weitergabe**

* Windows: läuft automatisch beim Build (siehe `installer/README.md`) – `windeployqt` nach jedem Build,
  beim Release-Build Installationsverzeichnis `installer/install_src` sowie, falls installiert,
  **Inno-Setup-Installer** und **Qt-Installer-Framework-Installer** in `installer/Output`
  (Ziel `C:\VitaParms`, Sprachauswahl, Lizenz, Komponenten Programm/Handbücher, Update und Deinstallation)
* macOS: `macdeployqt VitaParms.app -dmg`
* Linux: Distributionspakete oder z. B. linuxdeploy mit Qt-Plugin

Externe Bibliotheken werden **nicht** benötigt: Diagramme (QPainter), ZIP- und XLSX-Erzeugung
sind selbst implementiert.

## Bedienung in Kürze

| Aktion | Tastenkürzel |
|---|---|
| Neue Messung | `Strg+M` / `Strg+N` |
| Messung bearbeiten / löschen | `F2` / `Entf` |
| Stammblatt | `Strg+E` |
| Neuer Benutzer | `Strg+Umschalt+N` |
| Hauptbenutzer 1 / 2 | `Strg+1` / `Strg+2` |
| Drucken / Druckvorschau | `Strg+P` / `Strg+Umschalt+P` |
| Excel-Export | `Strg+Umschalt+E` |
| Krankenakte anzeigen | `Strg+3` |
| Terminplaner / neuer Termin | `Strg+4` / `Strg+T` |
| Medikamente stellen (Assistent) | `Strg+Umschalt+M` |
| Medikamentenbestand und Bestellungen | `Strg+Umschalt+L` |
| BtM im Medikamentenplan suchen | `Strg+Umschalt+B` |
| Abmelden / Sperren | `Strg+L` |
| Dokumente hinzufügen | `Strg+D` |
| Akte packen / senden | `Strg+Umschalt+S` |
| Handbuch | `F1` |

Hauptbenutzer werden über *Benutzer → Aktuellen Benutzer als Hauptbenutzer 1/2 festlegen* oder
*Extras → Einstellungen* belegt. Drucken/PDF/Excel beziehen sich auf den gerade sichtbaren Reiter
(Journal oder Grafik).

Kommandozeile: `VitaParms --database <datei.sqlite>` öffnet eine bestimmte Datenbank, `VitaParms --demo` die Demo-Datenbank.

## Termine und Erinnerungen

Reiter **Termine** bzw. Menü *Termine*. Jeder Termin gehört einem **Anmeldekonto** („Zuständig“) und kann
einer Person zugeordnet werden. Anwender sehen und bearbeiten nur ihre eigenen Termine; Administratoren
sehen alle (Filter „Alle Konten“/„Konto: …“) und können Termine anderen Konten zuweisen.

Die Erinnerung wird zum eingestellten Vorlauf ausgelöst, solange VitaParms läuft und das zuständige
Konto angemeldet ist; verpasste Erinnerungen (bis 6 Stunden nach Terminbeginn) folgen nach der Anmeldung.

* **optisch** – Hinweisfenster unten rechts, immer im Vordergrund, Farbbalken nach Priorität;
  „Später erinnern“ (5–60 min), „Öffnen…“, „Erledigt“
* **akustisch** – Gong (`resources/sounds/reminder.wav`, austauschbar)
* **per E-Mail** – ein Administrator richtet unter *Extras → Einstellungen → E-Mail-Versand* den
  Postausgangsserver ein (Zugangsdaten liegen verschlüsselt in der Datenbank, Test-E-Mail möglich).
  Mit Passwort wird nur über STARTTLS/SSL angemeldet. E-Mails selbst sind in der Regel **nicht**
  Ende-zu-Ende-verschlüsselt; deshalb enthalten sie standardmäßig nur Titel, Zeit und Priorität.

### Erinnerung an die Einnahme von Medikamenten

Im Medikament-Formular (*Stammblatt → Medikamente*, Tablet: *Medikamente*) legt der Haken **„An Einnahme
erinnern“** für jede gewählte Uhrzeit (morgens, mittags, abends, zur Nacht – vorbelegt nach dem
Dosierschema, Uhrzeiten änderbar) eine Terminserie mit Erinnerung zum Zeitpunkt an. Bei täglicher
Einnahme sind die **Wochentage** wählbar, bei anderem Rhythmus (alle N Tage, wöchentlich, monatlich)
gelten die Einnahmetage des Rhythmus. Ändert sich das Medikament, werden die künftigen Termine erneuert;
Haken entfernen, Archivieren oder Löschen beendet sie (vergangene bleiben stehen). Für Bedarfs- und
Notfallmedikation gibt es keine Erinnerung. Die Tablet-App legt die Termine an, die Erinnerung selbst
zeigt das Programm am Computer.

### Termine beim Start, Erinnerungen drucken

Beim Programmstart zeigt VitaParms die offenen Termine der gewählten Person für **heute und morgen**
(einschließlich Medikamententermine) – mit *Drucken…* und *Terminplaner öffnen*. Abschaltbar im Fenster
selbst oder unter *Einstellungen*; jederzeit erneut über *Termine → Termine heute und morgen…*.

*Benutzer → Drucken → Erinnerungen…* druckt die anstehenden, offenen Termine der Person (heute und morgen,
7/14/30/90 Tage oder alle kommenden; Medikamententermine wahlweise) nach Tagen gegliedert mit Zeit,
Priorität, Ort, Notiz, Zuständigkeit und Erinnerung – als Vorschau/Druck oder PDF.

### Terminserien

Im Termindialog unter **Wiederholung**: täglich, **alle … Tage** (z. B. alle 2 Tage), werktags (Mo–Fr), wöchentlich an beliebigen Wochentagen,
**alle … Wochen (am Wochentag)** (z. B. alle 3 Wochen am Mittwoch),
monatlich (am gleichen Tag, am n-ten oder am letzten Wochentag), jährlich – jeweils mit Intervall
(„alle 2 Wochen“) und Ende (ohne Ende, bis Datum, nach Anzahl). Die Vorschau zeigt die nächsten Termine.

Jedes Vorkommen ist ein eigener Termin mit eigener Erinnerung und eigenem „Erledigt“. Serien ohne Ende
werden laufend etwa 13 Monate im Voraus angelegt. Beim Ändern oder Löschen eines Serientermins fragt
VitaParms nach dem Umfang: **nur dieser Termin**, **dieser und alle folgenden** oder **alle Termine der
Serie**. In der Liste sind Serientermine mit ↻ markiert.

### Export als iCalendar (.ics)

*Termine → Termine exportieren* bzw. Schaltfläche **iCal-Export…**: ausgewählter Termin/Serie, aktuelle
Ansicht, alle kommenden oder alle Termine – als Datei speichern oder als Anhang im Mailprogramm. Die Datei
folgt RFC 5545 und lässt sich in Outlook, Thunderbird, Apple- und Google-Kalender importieren:
Serien als Wiederholungsregel (RRULE) mit Ausnahmen (EXDATE) und einzeln geänderten Terminen
(RECURRENCE-ID), Zeitzone mit Sommerzeitregeln (VTIMEZONE), Vorwarnungen als Kalender-Alarm.
Ort, Person und Notiz werden nur auf Wunsch übernommen – die Datei ist **nicht verschlüsselt**.
Ein erneuter Import aktualisiert die Termine (feste UIDs), statt sie zu verdoppeln.

## Medikamentenplan: Art, Rhythmus, Bemerkung

Jedes Medikament gehört zu einer **Art**: *Regelmedikation* (feste Einnahme nach Plan), *Bedarfsmedikation*,
*Notfallmedikation* oder *Sonstiges* (z. B. Salben, Augentropfen, Nahrungsergänzung, Impfungen, Hilfsmittel). Stammblatt, Ausdruck und Assistent sind danach gegliedert; die Notfallmedikation
erscheint zusätzlich rot im Kopf des Hauptfensters.

Für die Regelmedikation gibt es den **Einnahme-Rhythmus**: täglich, alle N Tage, an bestimmten Wochentagen
(auch „alle 2 Wochen“), monatlich – gezählt ab einem Starttag. Dazu eine **Bemerkung** (z. B. „21 Tage Einnahme,
dann 7 Tage Pause“, „nur mittwochs! Folsäure am Donnerstag“); bei Bedarfs- und Notfallmedikation dient sie als
„Anlass“. Der Dialog zeigt die nächsten Einnahmetage an.

**An Einnahme erinnern:** siehe [Erinnerung an die Einnahme von Medikamenten](#erinnerung-an-die-einnahme-von-medikamenten).
Medikamente mit nicht täglichem Rhythmus, die vor 0.10.2 automatisch in der Terminliste standen, behalten ihre Erinnerung.

## Medikamente suchen: BtM, Schmerzmittel, Blutverdünner, Blutdrucksenker, Kortison

*Benutzer → Medikamente suchen* (BtM: `Strg+Umschalt+B`) oder im Stammblatt *Medikamentenplan → Suchen…*
listet alle in Frage kommenden Medikamente des aktuellen Plans – gegliedert nach Regel-, Bedarfs-,
Notfallmedikation und Sonstigem – mit Dosierschema, Rhythmus/Bemerkung, **Menge pro Tag** (z. B. „3 Stück pro Tag“) und
**Wirkstoffmenge** (Stärke × Menge, z. B. „1.800 mg pro Tag“; bei Intervall-Medikamenten „pro Einnahmetag“,
bei Bedarf „je Bedarfsfall“). Optional mit abgesetzten Medikamenten, Freitextsuche, Druck und PDF.

Die Zuordnung erfolgt anhand von Wirkstoff- **und** Handelsnamen (z. B. Oxycodon/Targin, Apixaban/Eliquis,
Phenprocoumon/Marcumar):

* **BtM** nach Anlage III BtMG einschließlich der *ausgenommenen Zubereitungen*: Codein/Dihydrocodein bis
  100 mg je Einheit, Tilidin nur als Retardtablette mit Naloxon, Diazepam bis 10 mg, Lorazepam bis 2,5 mg,
  Zolpidem bis 8,5 mg u. a. sind kein BtM. Medizinal-Cannabis und Dronabinol sind seit dem MedCanG (04/2024)
  keine BtM mehr, Nabilon weiterhin.
* **Schmerzmittel**: Opioide, NSAR/Coxibe, Metamizol, Paracetamol, Triptane u. a.; zusätzlich Medikamente, deren
  Grund/Indikation „Schmerz“, „Migräne“ oder „Neuralgie“ nennt (als „ggf.“).
* **Blutverdünner**: Vitamin-K-Antagonisten, DOAK, Heparine, Fondaparinux, Thrombozytenaggregationshemmer
  (ASS bis 160 mg als TAH, höher dosiert als Schmerzmittel).
* **Blutdrucksenker**: ACE-Hemmer, Sartane (auch unbekannte Wirkstoffe auf „-pril“/„-sartan“), Calciumantagonisten,
  Betablocker, Thiazide, Aliskiren u. a.; Schleifen-/kaliumsparende Diuretika, Alphablocker, Clonidin, Minoxidil
  und Sacubitril als „ggf.“ (auch andere Anwendungsgebiete), ebenso Grund „Bluthochdruck“.
* **Kortisonhaltige Präparate** (Glukokortikoide) mit Anwendungsart aus der Darreichungsform: systemisch,
  inhalativ, nasal, äußerlich (Haut), Augen/Ohren, rektal.

„ggf.“ markiert Fälle, die von Zubereitung oder Stärke abhängen. Jede Zuordnung lässt sich im
Medikamentendialog unter **Kennzeichnung** korrigieren (gespeichert wird nur die Abweichung). Hinweise:
mehrere NSAR, mehrere Gerinnungshemmer bzw. mehrere RAS-Hemmstoffe gleichzeitig werden markiert; bei systemischem Kortison Hinweis auf Notfallausweis/nicht eigenmächtig absetzen. Ohne Gewähr.

## Beipackzettel

Im Medikamentenplan (Stammblatt), im Medikamentendialog und im Assistenten: **Beipackzettel…**

* **PZN** von der Packung eintragen (Prüfziffer wird kontrolliert) – damit findet *Apotheken Umschau* den
  Beipackzettel genau dieses Präparats (öffnet im Browser, mit Link zur Original-Gebrauchsinformation).
* *Gelbe Liste* und *Websuche* nach Name; eigene Quellen als URL-Vorlage unter *Einstellungen*.
* **EMA-Produktinformation** (PDF mit Packungsbeilage, nur zentral zugelassene Arzneimittel) und
  **openFDA** (US-Information, englisch) werden direkt im Fenster angezeigt.
* **Adresse laden… / Aus Datei…**: Original-PDF (z. B. den Link „Original Beipackzettel“) abrufen, anzeigen,
  drucken und **in der Krankenakte speichern** – verschlüsselt, mit dem Medikament verknüpft, offline verfügbar.

Übertragen werden nur Name, Stärke, Wirkstoff bzw. PZN – keine Personendaten. Das Nachschlagen im Internet
lässt sich in den Einstellungen abschalten. Maßgeblich ist immer der Beipackzettel in der Packung.

## Medikamente stellen

*Benutzer → Medikamente stellen (Assistent)…* liest die am gewählten **Tag fälligen** Medikamente der
Regelmedikation (nicht archiviert, gültig, Rhythmus beachtet) nacheinander vor – Datum wählbar, z. B. Tag für
Tag beim Befüllen eines Wochendosierers. Nicht fällige, Bedarfs- und Notfallmedikamente werden darüber
angezeigt. Vorgelesen werden – in der Reihenfolge des Tages (morgens zuerst):
Präparat, Stärke, Form, die Dosis je Zeitpunkt sowie Rhythmus und Bemerkung. Kürzel werden ausgeschrieben (mg → Milligramm,
IE → internationale Einheiten, ½ → eine halbe, s.c. → unter die Haut).

* **Weiter ▶** (Leertaste/Eingabe/→) – nächstes Medikament; erst dann wird weitergelesen
* **↻ Wiederholen** (R) – dasselbe Medikament nochmals
* **◀ Zurück** (←) – vorheriges Medikament
* „Zeitpunkt“ beschränkt auf morgens/mittags/abends/zur Nacht, z. B. für einen Wochendosierer
* Stimme: „weiblich/männlich (automatisch)“ wählt eine installierte Systemstimme der Oberflächensprache;
  zusätzlich lässt sich jede installierte Stimme gezielt auswählen (im Assistenten oder unter
  *Einstellungen*, dort auch das Sprechtempo). Da manche Sprachausgaben das Geschlecht ihrer Stimmen
  falsch melden (flite: alle „männlich“), erkennt VitaParms bekannte Stimmen zusätzlich am Namen.
  Fehlt eine Stimme des gewünschten Geschlechts, wird die Tonhöhe der vorhandenen Stimme verändert und
  der Assistent weist darauf hin – eine echte männliche Stimme muss im Betriebssystem installiert sein.

Nach dem letzten Medikament erscheint groß **„✔ Medikamente sind gerichtet“** mit Signalton und Ansage.

**Protokoll:** Jeder Durchgang wird mit Zeitpunkt, angemeldetem Konto, Tageszeit und den gestellten
Medikamenten samt Dosis protokolliert; dazu eine optionale Bemerkung (z. B. „Wochendosierer KW 43“).
Wird der Assistent vorzeitig beendet, steht der Eintrag als „abgebrochen (n von m)“ im Protokoll.
Der Assistent zeigt, wann zuletzt gestellt wurde; *Benutzer → Protokoll Medikamente stellen…* listet alle
Einträge (30 Tage … gesamter Zeitraum) zum Drucken oder als PDF. Löschen dürfen nur Administratoren.

## Website

VitaParms kann die Daten aller Personen auf einer eigenen Website zum **Ansehen** bereitstellen –
für Angehörige, die Hausarztpraxis oder unterwegs. Die Website ist statisch (HTML/JavaScript plus
eine kleine PHP-Datei zum Hochladen) und braucht **keine Datenbank** auf dem Server.

- **Inhalt:** persönliche Angaben, Notfallmedikation, der **gesamte** Verlauf der Messwerte als
  Tabelle (CSV-Download) und als **Verlaufskurven** (Tag/Woche/Monat/Jahr/Zeitraum, Kurven ein-
  und ausblendbar, Legende, druckbar), Medikamentenplan, Termine sowie Stammblatt,
  Medikamentenplan und Verlauf als PDF/Excel zum Herunterladen. Die **Krankenakte wird nicht
  übertragen.**
- **Schutz:** VitaParms verschlüsselt alles **vor dem Hochladen** mit dem Master-Passwort
  (PBKDF2-SHA256 mit 210 000 Iterationen → AES-256-CTR + HMAC-SHA256). Der Server speichert nur
  Chiffrat; entschlüsselt wird im Browser (Web Crypto API). Ein getrennter **Upload-Schlüssel**
  berechtigt zum Hochladen. Automatische Abmeldung nach 30 Minuten.
- **Aktualisierung:** automatisch etwa eine Minute nach Änderungen (im Hintergrund) und beim
  Beenden, oder von Hand mit *Datei → Website aktualisieren* (`Strg+Umschalt+U`).

Einrichtung: Inhalt von `website/` per (S)FTP in das Wurzelverzeichnis der (Sub-)Domain
hochladen, HTTPS einschalten, in VitaParms *Extras → Website…* Adresse und Master-Passwort
eintragen, *Jetzt hochladen*. Details in `website/README.md` und im Handbuch (Kapitel „Die eigene
Website“).

## Tablet-App und Abgleich

Die Tablet-App (`mobile/VitaParmsMobile.pro`, Qt Quick) teilt sich mit dem Desktop-Programm den
Kern (`core.pri`: Datenbank, Verschlüsselung, Fachlogik, Berichte, Website, Abgleich). Sie wird
mit Qt Creator für Android gebaut und als APK direkt installiert (Entwicklermodus, keine
Verteilung über Google Play) – Anleitung in `mobile/README.md`.

Abgleich: Beide Geräte tauschen die **verschlüsselte SQLite-Datenbank** samt Schlüsseldatei über
`api/sync.php` der eigenen Website aus. Jede Zeile trägt eine geräteübergreifende Kennung und
einen Änderungszeitpunkt (Trigger, Schema 7), Löschungen hinterlassen Vermerke. Beim
Zusammenführen bleiben neue Zeilen beider Seiten erhalten, bei gleicher Zeile gewinnt die jüngere
Änderung, Löschvermerke gewinnen gegen ältere Änderungen. Terminvorkommen einer Serie haben feste
Kennungen (Serie@Zeitpunkt). Der Server zählt eine Revision hoch und weist veraltete Uploads ab
(HTTP 409 → erneut einarbeiten).

Abgleich über das USB-Kabel (`usbsync.*`, `usbsyncdialog.*`): Die Tablet-App wartet auf
`127.0.0.1:47811` (nur solange die Seite „USB-Abgleich“ offen ist) und zeigt einen sechsstelligen
Code. Das Desktop-Programm leitet mit `adb forward tcp:0 tcp:47811` einen Port über das Kabel
weiter und verbindet sich. Ob beide dieselbe Datenbank verwenden, zeigt ein HMAC-Nachweis über
einen Zufallswert (der Schlüssel wird nicht übertragen). *Abgleichen*: Tablet-Datenbank holen,
hier einarbeiten (`DbMerge::mergeFrom`), Ergebnis zurückschicken, dort ebenso einarbeiten;
Anmeldekonten gelten vom Gerät mit der jüngeren `.keys`-Datei. *Ersetzen*: `.vpdb`-Paket in die
eine oder andere Richtung (bisheriger Stand bleibt als Kopie). Zum Testen ohne adb:
`VP_USBSYNC_DIRECT=127.0.0.1:47811`.

## Anmeldung, Rollen und Verschlüsselung

**Erster Start / neue Datenbank:** VitaParms legt einen zufälligen 256-Bit-Datenbankschlüssel an,
verschlüsselt damit die Datenbank und fragt nach dem ersten **Administrator-Konto** (Name + Passwort).
Anschließend wird der **Wiederherstellungsschlüssel** angezeigt (drucken, speichern, kopieren). Eine
unverschlüsselte Datenbank einer älteren Version wird beim ersten Öffnen verschlüsselt.

**Anmeldung:** Konto + Passwort. Bei korrekter Anmeldung wird die Datenbank automatisch entsperrt –
niemand muss den Datenbankschlüssel kennen. Mehrere Konten (auch mehrere Administratoren) teilen sich
dieselbe Datenbank.

| Rolle | darf |
|---|---|
| **Anwender** | Personen anlegen/bearbeiten, Messungen und Dokumente erfassen, drucken, exportieren, Akte versenden, eigenes Passwort ändern, Einstellungen |
| **Administrator** | zusätzlich: Anmeldekonten verwalten (anlegen, Passwort setzen, Rolle wechseln, löschen), Wiederherstellungsschlüssel anzeigen, Personen löschen, Datenbanken anlegen/öffnen, Sicherung/Wiederherstellung, Formulare anpassen |

Es muss immer mindestens ein Administrator-Konto bestehen. Vergisst ein Anwender sein Passwort,
setzt ein Administrator ein neues. Vergessen alle Administratoren ihr Passwort, hilft der
Wiederherstellungsschlüssel (Anmeldedialog → *Wiederherstellungsschlüssel…*).

**Technik:**

* Datenbank: SQLite3 Multiple Ciphers 2.6.1 (MIT), Verschlüsselung ChaCha20-Poly1305 je Seite,
  sichere Löschung (`secure_delete`), temporäre Daten nur im Arbeitsspeicher.
* Schlüsseldatei `<datenbank>.keys` (neben der Datenbank): je Konto der Datenbankschlüssel,
  verschlüsselt mit PBKDF2-HMAC-SHA256 (200 000 Runden) + AES-256-CTR + HMAC-SHA256. Die Kontenliste
  ist gegen Manipulation mit einem HMAC geschützt; eine von außen veränderte Liste führt zur
  Anmeldung mit Anwenderrechten.
* Der Wiederherstellungsschlüssel ist der Datenbankschlüssel selbst (Base32 mit Prüfsumme). Er
  funktioniert auch ohne `.keys`-Datei – entsprechend sicher aufbewahren.
* **Sicherungen** bestehen aus zwei Dateien: `…sqlite` (verschlüsselt) und `…sqlite.keys`.

**Grenzen (ehrlich benannt):** Die Rollen sind eine Regel innerhalb von VitaParms. Wer mit einem
Anwenderkonto die Datenbank entsperren kann, könnte sie mit eigenen Werkzeugen auch vollständig
auslesen – die Verschlüsselung schützt vor Personen *ohne* Konto (verlorenes Notebook, Kopie der
Datei). Exporte (PDF, Excel, unverschlüsselte ZIPs) und Dateien, die zur Ansicht an andere Programme
übergeben werden, sind nicht verschlüsselt. Bei SSDs können Reste einer früher unverschlüsselten
Datenbank physisch erhalten bleiben.

## Sprache und Erscheinungsbild

*Extras → Einstellungen → Darstellung*

* **Sprache:** Systemsprache, Deutsch oder English (wirksam nach Neustart, VitaParms bietet ihn an).
  Übersetzungsdatei: `translations/vitaparms_en.ts` (Qt Linguist). Neue Texte:
  `lupdate -no-obsolete VitaParms.pro`, dann übersetzen (Linguist oder `translations/en_dict.py` +
  `python3 tools/apply_translations.py`). Die `.qm`-Datei erzeugt der Build automatisch.
  Eingegebene Daten (Diagnosen, Messzeitpunkte, Kategorien bestehender Dokumente) werden nicht übersetzt.
* **Theme:** „Systemstandard“, alle Qt-Styles der Plattform (z. B. Fusion, Windows, windowsvista/
  windows11, macOS) sowie **Dark (AmigaED)** und **Visual Studio Code Dark (AmigaED)** mit den
  Paletten und Stylesheets aus AmigaED 4.0. Die Umschaltung wirkt sofort; die Diagramme am
  Bildschirm passen sich dunklen Themes an, Ausdrucke bleiben auf weißem Papier.

## Digitale Krankenakte

Reiter **Dokumente (Krankenakte)**: Liste nach Dokumentdatum (neueste zuerst), Filter nach Art
und Jahr, Suchfeld über Titel, Art, Absender, Notiz, Dateiname, Datum und – sofern das PDF Text
enthält – den gesamten Inhalt. Mehrere Suchbegriffe müssen alle vorkommen; die Spalte
„Fundstelle“ zeigt den Treffer im Text.

**Import:** *Hinzufügen…* oder Dateien auf das Fenster ziehen. VitaParms schlägt vor:
Datum aus Dateiname (`2026-09-30_…`, `20260930`, `30.09.2026`) oder Briefkopf (jüngstes
plausibles Datum, Geburtsdatum ausgeschlossen) oder PDF-Erstellungsdatum; Art anhand von
Stichwörtern (Entlass…, Labor…, Befund …); Titel aus PDF-Titel oder Dateiname. Identische
Dateien werden über SHA-256 erkannt.

**Scans ohne Text** sind nur über Titel, Art, Absender und Notiz auffindbar (Spalte „Text“ leer).
Eine Texterkennung (OCR) ist nicht eingebaut – entweder die Scan-Software mit OCR verwenden
(viele Scanner/Apps erzeugen „durchsuchbare PDFs“) oder Stichworte in die Notiz schreiben.

**Drucken:** gewünschte Dokumente markieren (Strg/Umschalt+Klick) → *Drucken…* öffnet eine
Vorschau aller Seiten in Reihenfolge der Liste. *Liste drucken…* gibt das Inhaltsverzeichnis aus.

**Akte packen / senden:** erzeugt ein ZIP

```
Krankenakte_Mustermann_Erika/
  00_Inhaltsverzeichnis.pdf
  01_Stammblatt.pdf  02_Medikamentenplan.pdf  03_Verlauf_Vitalwerte.pdf  04_Verlaufsgrafik.pdf
  Dokumente/001_2026-09-30_Laborwerte_Blutbild.pdf …
```

* **Verschlüsselung:** AES-256 im WinZip-AES-Format (AE-2), Schlüsselableitung PBKDF2-HMAC-SHA1,
  Integritätsprüfung per HMAC – eigene Implementierung ohne Fremdbibliothek, mit 7-Zip geprüft.
  Öffnen mit 7-Zip, WinRAR, WinZip, PeaZip, Keka, The Unarchiver oder Linux-Archivprogrammen.
  **Nicht** mit dem Windows-Explorer oder dem macOS-Archivierungsprogramm allein.
  Das Passwort gehört nicht in dieselbe E-Mail.
* **E-Mail:** Windows über Simple MAPI (Outlook, Thunderbird, eM Client …), macOS über Apple Mail,
  Linux über `xdg-email`. Gelingt das nicht, öffnet VitaParms eine neue Nachricht ohne Anhang
  und zeigt die ZIP-Datei im Dateimanager. Die Nachricht wird nie automatisch gesendet. Die
  Versandkopie liegt in `Dokumente/VitaParms-Versand` und kann danach gelöscht werden.

**Speicherung:** Die PDFs liegen verschlüsselt als BLOB in der Datenbank (eigene Tabelle). Damit ist
die Akte in jeder Sicherung enthalten und bleibt mit dem Benutzer verknüpft. Gelöschte Dokumente
geben Platz in der Datei erst nach einer Sicherung frei (die Sicherung ist kompakt).

## Formulare anpassen

*Extras → Formulare anpassen…* legt eine bearbeitbare Kopie im Benutzer-Formularordner an und
öffnet sie in Qt Designer. Nach dem Speichern gilt die Änderung beim nächsten Öffnen des Dialogs.

Suchreihenfolge für `<name>.ui`:

1. Benutzer-Formularordner (`<AppData>/VitaParms/forms`, z. B. `~/.local/share/VitaParms/VitaParms/forms`)
2. `<Programmordner>/forms` (portable Installation, z. B. für ein ganzes Team)
3. die im Programm eingebettete Standardversion

**Regeln**

* Die `objectName`s der Standardfelder müssen erhalten bleiben (Tabelle unten). Fehlt ein Feld,
  wird es einfach nicht bearbeitet – vorhandene Werte bleiben in der Datenbank unverändert.
* Pflichtfelder (fehlen sie, wird die Standardversion verwendet): `lastNameEdit` (Stammblatt),
  `dateTimeEdit` (Messung), `nameEdit` (Medikament), `titleEdit` (Dokument).
* Der Elementtyp darf getauscht werden (z. B. `QLineEdit` → `QPlainTextEdit`).
* Leere Messwerte: Spinboxen mit `specialValueText` (z. B. „—“) gelten beim Minimalwert als
  „nicht gemessen“. Bereiche/Schrittweiten sind frei einstellbar. (Ausnahme: Bereich und
  Nachkommastellen des Blutzuckerfelds setzt das Programm passend zur gewählten Einheit.)
* **Eigene Felder:** Im Designer am Eingabeelement über „+“ → *Zeichenkette* eine dynamische
  Eigenschaft **`vpField`** anlegen und als Wert den Feldnamen eintragen (z. B. `Bauchumfang`).
  Unterstützt: QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox,
  QCheckBox, QDateEdit, QTimeEdit, QDateTimeEdit, QSlider. Zusatzfelder der Messung erscheinen
  automatisch als eigene Spalte im Journal, im Ausdruck und im Excel-Export.
* Die Eigenschaft `formVersion` am Wurzelelement nicht entfernen – darüber erkennt VitaParms,
  wenn eine Anpassung auf einer älteren Vorlage beruht.

| Formular | ausgewertete Elemente |
|---|---|
| `patientform.ui` | `lastNameEdit` `firstNameEdit` `birthDateEdit` `sexCombo` `streetEdit` `zipEdit` `cityEdit` `phoneEdit` `mobileEdit` `diagnosesEdit` `notesEdit` `armLeftCombo` `armRightCombo` `legLeftCombo` `legRightCombo` `amputationInfoLabel` `medicationTable` `addMedicationButton` `editMedicationButton` `archiveMedicationButton` `deleteMedicationButton` `showArchivedCheck` `printMedicationButton` `admissionLabel` `admissionButton` |
| `vitalform.ui` | `headerNameLabel` `headerFirstNameLabel` `headerBirthDateLabel` `dateTimeEdit` `nowButton` `contextCombo` `systolicSpin` `diastolicSpin` `pulseSpin` `temperatureSpin` `spo2Spin` `peakFlowSpin` `glucoseSpin` `glucoseUnitLabel` `weightSpin` `heightSpin` `bmiValueLabel` `bmiInfoLabel` `observationsEdit` |
| `documentform.ui` | `dateEdit` `categoryCombo` `titleEdit` `issuerEdit` `notesEdit` `fileInfoLabel` `textInfoLabel` `previewLabel` |
| `medicationform.ui` | `nameEdit` `substanceEdit` `strengthEdit` `formCombo` `morningEdit` `noonEdit` `eveningEdit` `nightEdit` `unitCombo` `reasonEdit` `notesEdit` `startDateEdit` `endDateCheck` `endDateEdit` `archivedCheck` |

Die Auswahllisten `sexCombo`, `armLeftCombo` usw. sowie `contextCombo` füllt das Programm selbst.

## BMI bei fehlenden Gliedmaßen

Das gemessene Gewicht wird auf den vollständigen Körper hochgerechnet:
`G_korr = G / (1 − p/100)`, `BMI = G_korr / (Größe in m)²`.
Anteile `p` nach Osterkamp LK, *J Am Diet Assoc* 1995;95:215–218:

| fehlt | Anteil |
|---|---|
| Hand | 0,7 % |
| Unterarm mit Hand | 2,3 % |
| ganzer Arm | 5,0 % |
| Fuß | 1,5 % |
| Unterschenkel mit Fuß | 5,9 % |
| ganzes Bein | 16,0 % |

Ist bei einer Messung keine Größe eingetragen, wird die zuletzt erfasste Größe verwendet.
Der BMI wird mit der Messung gespeichert; ändern sich die Gliedmaßen-Angaben, fragt das
Programm, ob ältere Messungen neu berechnet werden sollen.

## Projektaufbau

```
VitaParms.pro            qmake-Projekt
forms/*.ui               Standardformulare (zur Laufzeit geladen, als Ressource eingebettet)
resources/               Symbole, .qrc
src/
  main.cpp               Start, Haftungshinweis, Datenbank öffnen
  mainwindow.*           Hauptfenster: Benutzerwahl, Journal, Grafik, Menüs
  database.*             SQLite-Schema (PRAGMA user_version), Zugriffe, Verschlüsselung, Sicherung
  sqlquery.*             schlanke Qt-Hülle um die SQLite-C-API
  keystore.*             Anmeldekonten, Schlüsselbund (.keys), Wiederherstellungsschlüssel
  session.*              Anmeldung, Ersteinrichtung, Migration, Rollen
  accountdialogs.*       Anmelde-, Konto- und Wiederherstellungsdialoge
  themes.*               Plattform-Styles + Dark-Themes aus AmigaED 4.0
  i18n.*                 Sprachwahl, Übersetzungen, Neustart
  datatypes.h            Patient, VitalRecord, Medication, Amputations
  formloader.*           Laden/Prüfen der .ui-Dateien, typunabhängiger Feldzugriff (FormIO)
  patientdialog.*        Stammblatt inkl. Medikamentenplan und Aufnahmewerte
  vitaldialog.*          Erfassung einer Messung
  medicationdialog.*     ein Medikament
  bmi.*                  BMI und Amputationskorrektur
  vitaldefs.*            Beschriftungen, Einheiten, Farben, Diagramm-Bereiche
  chartwidget.*          Diagramme (Bildschirm, Druck, PDF, Excel-Bild – ein Renderer)
  journalmodel.*         Tabellenmodell für Journal/Verlauf
  reportprinter.*        Druck/Vorschau/PDF (A4) und Excel-Export
  xlsxwriter.*, zipwriter.*  .xlsx/.zip-Erzeugung ohne Fremdbibliothek (ZIP optional AES-256)
  aes256.*               AES-256 (FIPS-197) für verschlüsselte ZIP-Archive
  documentspage.*        Reiter „Dokumente“: Liste, Suche, Vorschau, Import, Druck
  documentdialog.*       Angaben zu einem Dokument
  pdftools.*             Text/Seiten auslesen, Datum/Art vorschlagen, Bild→PDF, PDF-Druck
  akteexport.*           Krankenakte als ZIP (+ Inhaltsverzeichnis) und Versanddialog
  appointmentspage.*     Reiter „Termine“: Kalender, Liste, Filter
  appointmentdialog.*    Termin bearbeiten (Formular forms/appointmentform.ui)
  reminderservice.*      Fälligkeitsprüfung, Hinweisfenster, Ton, E-Mail
  smtpclient.*, smtpdialog.*  SMTP-Versand (STARTTLS/SSL) und Einrichtung
  medassistant.*         Assistent „Medikamente stellen“
  medschedule.*          Art, Einnahme-Rhythmus, Übernahme in die Terminliste
  drugclass.*            Zuordnung BtM/Schmerzmittel/Blutverdünner/Blutdruck/Kortison, Mengenberechnung
  medsearchdialog.*      Suche im Medikamentenplan
  leaflet.*              Beipackzettel nachschlagen, anzeigen, in der Krankenakte speichern
  medlogdialog.*         Protokoll „gestellt am/von“
  demodata.*             Demo-Datenbank (365 Tage Beispieldaten, eigener Einstellungsbereich)
  dbsync.*               Abgleich zwischen Geräten: Zusammenführen, Server-Protokoll (api/sync.php)
  usbsync.*              Abgleich über das USB-Kabel: Protokoll, Tablet-Seite (wartet), Computer-Seite
  usbsyncdialog.*        Datei → Mit Tablet über USB abgleichen… (adb suchen, Port weiterleiten)
  quarterreminder.*      Erinnerung an den Quartalswechsel (Zeitraum, Bestätigung)
  glucoseprofile.*       BZ-Tagesprofil: Zeitpunkte, Termine anlegen (Dialog: glucoseprofiledialog.*)
  apptcore.*             Termine: Texte, Wiederholungsauswahl, Speichern/Löschen mit Serien-Umfang (Desktop und Tablet)
  careinfo.*             Pflegegrad, Schwerbehindertenausweis (Merkzeichen, Anzeigetexte)
  websync.*              Website: Paket bauen (Hintergrund-Thread), verschlüsseln (VPW1), hochladen
  websitedialog.*        Einstellungen der Website (Adresse, Master-Passwort, Upload-Schlüssel)
  manual.*               Hilfe → Handbuch (F1): PDF bzw. Windows-Hilfe (.chm) aus docs/ öffnen
  recurrence.*           Wiederholungsregeln (Teilmenge von RFC 5545)
  icalexport.*           iCalendar-Export und Exportdialog
  speech.*, sounds.*     Sprachausgabe (Qt TextToSpeech) und Signaltöne
  inactivity.*           automatische Sperre bei Nichtbenutzung
  mailer.*, mapi_win.cpp E-Mail mit Anhang (MAPI / Apple Mail / xdg-email)
  settingsdialog.*, formcustomizedialog.*, appsettings.*
core.pri                 gemeinsamer Kern für Desktop und Tablet; version.pri: Versionsnummer
mobile/                  Tablet-App (Qt Quick): src/, qml/, android/, translations/
website/                 Website (index.html, assets/, api/upload.php, data/) – auf den Webspace hochladen
docs/                    Handbücher (PDF, CHM) – werden neben das Programm kopiert
installer/               Windows: windows_deploy.pri (Build-Schritt), VitaParms.iss.in (Inno Setup),
                         qtifw/ (Qt Installer Framework), Lizenztexte, Assistenten-Bilder
tools/
  make_chm.py            Windows-Hilfe (.chm) aus dem LaTeX-Handbuch erzeugen
  apply_translations.py  Übersetzungen aus translations/en_dict.py übernehmen
```

## Lizenzhinweise

VitaParms steht unter der MIT-Lizenz (siehe `LICENSE`). Qt (einschließlich Qt PDF) wird unter
LGPLv3 dynamisch gelinkt (ebenso Qt TextToSpeech und Qt Multimedia). SQLite3 Multiple Ciphers: MIT-Lizenz (`3rdparty/sqlite3mc/LICENSE`),
SQLite selbst ist gemeinfrei.
Bewusst **nicht** verwendet wurden *Qt Charts* und *Qt Graphs*, da diese Module in der
Open-Source-Ausgabe nur unter GPLv3 verfügbar sind.
Die Website verwendet die Schrift *Atkinson Hyperlegible* (Braille Institute, SIL Open Font
License 1.1, `website/assets/fonts/OFL.txt`).
