# VitaParms – Website

Diese Website zeigt die mit VitaParms erfassten Daten **aller Personen** zum Ansehen:
persönliche Angaben, den gesamten Verlauf der Messwerte (Tabelle und Verlaufskurven, dazu die
Stuhlgang-Häufigkeit als Tagesbalken mit Hinweis nach drei Tagen ohne Stuhlgang),
Medikamentenplan, Termine und Berichte zum Herunterladen. Die Krankenakte wird nicht übertragen.

*English summary below.*

## So sind die Daten geschützt

- VitaParms verschlüsselt alle Daten **auf dem eigenen Rechner** mit dem Master-Passwort,
  bevor sie hochgeladen werden (PBKDF2-SHA256, 210 000 Iterationen → AES-256-CTR + HMAC-SHA256).
- Auf dem Server liegen nur verschlüsselte Dateien (`data/<paket>/*.vpw`) und ein
  unverschlüsseltes Manifest (`data/current.json`) mit Salz und Prüfwert – **keine** Messwerte,
  keine Namen.
- Entschlüsselt wird erst im Browser (Web Crypto API), nachdem das Passwort eingegeben wurde.
  Der Schlüssel bleibt nur für die Dauer der Browser-Sitzung erhalten; nach 30 Minuten ohne
  Benutzung meldet die Seite ab.
- Hochladen darf nur, wer den **Upload-Schlüssel** kennt (getrennt vom Master-Passwort). Der
  Server speichert davon nur einen Hash (`data/.uploadkey.php`).
- `.htaccess` erzwingt HTTPS, verbietet Verzeichnislisten, setzt eine strenge Content Security
  Policy und hält Suchmaschinen fern (`noindex`).

## Voraussetzungen

- Webspace mit **Apache** (oder kompatibel, `.htaccess` wird ausgewertet) und **PHP ≥ 7.4**
- **HTTPS** (Let's Encrypt genügt) – die Web Crypto API funktioniert nur über HTTPS
- keine Datenbank, keine weiteren Module

**Eigener Server im Heimnetz** (z. B. Raspberry Pi mit Apache2 und PHP) geht ebenso: Zertifikat
für eine Subdomain der eigenen Domain per Let's-Encrypt-DNS-Prüfung (der DNS-Eintrag zeigt auf die
Heimnetz-Adresse), `AllowOverride All`, `a2enmod rewrite headers ssl`, `data/` und `sync/` für
`www-data` beschreibbar; unterwegs per VPN. Einzelheiten: Handbuch, Kapitel „Die eigene Website“.

## Installation

1. Subdomain anlegen, z. B. `vitaparms.example.de`, und auf einen **leeren Ordner** zeigen
   lassen (z. B. `vitaparms/`). HTTPS-Zertifikat aktivieren.
2. Den **Inhalt** dieses Ordners per (S)FTP dorthin hochladen:
   ```
   .htaccess  index.html  robots.txt  README.md
   api/upload.php  api/sync.php  api/files.php  api/lib.php  api/.htaccess
   assets/…   (app.js, app.css, Schriften, Symbole)
   data/.htaccess  data/index.html
   sync/.htaccess  sync/index.html
   ```
   Versteckte Dateien (`.htaccess`) im FTP-Programm einblenden, damit sie mit übertragen werden.
3. Ordner `data/` und `sync/` beschreibbar machen (Rechte `755`, bei manchen Anbietern `775`).
4. In VitaParms als Administrator *Extras → Website…*:
   *Website verwenden* anhaken, Adresse (`https://vitaparms.example.de`) und Master-Passwort
   eintragen, **Verbindung testen**, dann **Jetzt hochladen**.
5. Die Adresse im Browser öffnen und mit dem Master-Passwort anmelden.

Ab dann aktualisiert VitaParms die Website automatisch etwa eine Minute nach Änderungen und
beim Beenden (abschaltbar), von Hand mit *Datei → Website aktualisieren* (`Strg+Umschalt+U`).

## Abgleich mit der Tablet-App

`api/sync.php` verwahrt die verschlüsselte Datenbank (SQLite3 Multiple Ciphers) und die
verschlüsselte Schlüsseldatei der Anmeldekonten in `sync/` (per `.htaccess` gesperrt, Abruf
nur über die Schnittstelle mit Upload-Schlüssel). Zusammengeführt wird auf den Geräten; der
Server zählt nur eine Revision hoch und lehnt veraltete Uploads ab (HTTP 409). Die
Website-Ansicht selbst ist davon unabhängig.

Ab VitaParms 0.27 merkt sich `sync.php` das höchste Datenbankformat (`schema` in
`sync/state.json`) und lehnt Uploads von Geräten mit älterem Format – auch von Versionen vor 0.27,
die gar kein Format mitsenden – mit **HTTP 426** ab. Ein nicht aktualisiertes Gerät kann so keine
Tabellen überschreiben, die es nicht kennt. **Nach dem Update auf 0.27 den Ordner `api/` auf dem
Server ersetzen** und alle Geräte (Desktop und Tablet) aktualisieren, sonst ist kein Abgleich möglich.

**0.28 (Datenbankformat 12):** neue Spalte `vitals.pain` (Schmerzstärke). Die Website-Ansicht
zeigt sie als eigene Kurve und im Verlauf; `api/` ist unverändert, der Versionsschutz verlangt aber,
dass **alle Geräte auf 0.28** aktualisiert werden. `assets/app.js` bitte mit hochladen.
VitaParms light hat keinen Website-Abgleich.

Ab VitaParms 0.19 gehen die **Inhalte der Krankenakte** (PDF) getrennt über `api/files.php`:
einzeln, nur die fehlenden, auf dem Gerät mit AES-256 verschlüsselt, abgelegt in `sync/files/`
unter einer Kennung, die nichts über das Dokument verrät. Nicht mehr gebrauchte Inhalte werden
nach einem Tag gelöscht. Ohne `api/files.php` überträgt VitaParms die Datenbank wie bisher
vollständig (alle Geräte sollten dieselbe Version verwenden).

## Zugriffsprotokoll

Die Website führt ein Protokoll der Zugriffe: **Benutzer** (der bei der Anmeldung eingegebene
Name), **Datum und Uhrzeit** und **IP-Adresse** – bei Anmeldung, fehlgeschlagener Anmeldung,
Abmeldung, Herunterladen von Berichten/CSV und beim Ansehen oder Herunterladen des Protokolls.
Im Web-Interface öffnet **Zugriffsprotokoll** (oben rechts) die Liste (neueste zuerst, Filter,
Suche) mit **Herunterladen (CSV)**.

* Ablage: `data/.access-log.php` (von außen nicht abrufbar, beginnt mit einer PHP-Sperrzeile,
  eine JSON-Zeile je Eintrag). Ab 1 MB wird es nach `data/.access-log.1.php` verschoben; die
  ältere Datei wird dabei ersetzt.
* Lesen dürfen nur Personen mit dem Master-Passwort: Der Browser leitet daraus einen Zugriffswert
  ab, den der Server mit dem Prüfwert in `data/.access.php` vergleicht. VitaParms legt diesen
  Prüfwert beim Hochladen ab (ab Version 0.26 – vorher einmal *Website aktualisieren*).
* Fehlgeschlagene Anmeldungen werden auch ohne Passwort protokolliert, je IP-Adresse höchstens
  20 pro Stunde.
* Der Name wird nicht geprüft (es gibt ein gemeinsames Master-Passwort). Ist die Website zusätzlich
  per HTTP-Authentifizierung geschützt, wird auch deren Anmeldename vermerkt („Server-Anmeldung“).
* Hinter einem Proxy des Anbieters wird die Adresse aus `X-Forwarded-For` zusätzlich vermerkt
  („über …“); sie kann gefälscht sein.
* Datenschutz: IP-Adressen sind personenbezogene Daten. Weisen Sie Nutzer der Website auf das
  Protokoll hin und löschen Sie es bei Bedarf (siehe Wartung).

## Wartung

| Aufgabe | Vorgehen |
|---|---|
| neuen Upload-Schlüssel verwenden | in VitaParms *Neu* klicken, auf dem Server `data/.uploadkey.php` löschen, hochladen |
| Master-Passwort ändern | in VitaParms ändern und hochladen – das alte Passwort gilt danach nicht mehr |
| Website leeren | alle Unterordner von `data/` und `data/current.json` löschen |
| Abgleich zurücksetzen | Inhalt von `sync/` löschen (außer `.htaccess`), danach am Computer abgleichen |
| Fehler „post_max_size“ | `upload_max_filesize` und `post_max_size` beim Anbieter erhöhen (z. B. 64M) |
| endlose Umleitung | in `.htaccess` den Block `mod_rewrite` auskommentieren |
| Zugriffsprotokoll löschen | `data/.access-log.php` und `data/.access-log.1.php` löschen |

Auf dem Server liegen jeweils das aktuelle und das vorige Paket; ältere werden beim Hochladen
gelöscht.

## Aufbau

```
index.html          Anmeldung und Anwendungsrahmen
assets/app.js       Entschlüsselung, Darstellung (Übersicht, Verlaufskurven, Messwerte,
                    Medikamente, Termine, Berichte), Deutsch/Englisch, hell/dunkel
assets/app.css      Gestaltung, Druck (DIN A4), Smartphone
assets/fonts/       Atkinson Hyperlegible (SIL Open Font License 1.1, OFL.txt)
api/upload.php      Annahme eines neuen Pakets (POST) und Zustand (GET ?status=1)
api/sync.php        Datenbank-Abgleich: Zustand, Herunterladen, Hochladen mit Revisionsprüfung
api/files.php       Inhalte der Krankenakte beim Abgleich (einzeln, verschlüsselt)
api/log.php         Zugriffsprotokoll: Eintrag schreiben (POST), anzeigen (GET, mit Zugriffswert)
api/lib.php         gemeinsame Hilfsfunktionen (nicht direkt abrufbar)
data/               Ablage der verschlüsselten Pakete; current.json zeigt auf das aktuelle
sync/               verschlüsselte Datenbank und Dokumentinhalte (files/) für den Abgleich
                    (nie direkt abrufbar)
```

---

## English summary

This website shows the data of **all persons** recorded in VitaParms (no medical records),
encrypted end-to-end: VitaParms encrypts everything with the master password before uploading;
the browser decrypts it after sign-in. Requirements: Apache with PHP ≥ 7.4 and HTTPS, no database.

Installation: upload the contents of this folder (including the hidden `.htaccess` files) to the
document root of your (sub)domain, make `data/` and `sync/` writable (755/775), then in VitaParms choose
*Tools → Website…*, enter the address and master password, click **Test connection** and
**Upload now**. To accept a new upload key, delete `data/.uploadkey.php` on the server.

Access log: the website records who (the name entered at sign-in), when and from which IP
address it was accessed (sign-in, failed sign-in, sign-out, downloads). **Access log** (top right)
shows the list and downloads it as CSV. Only users with the master password can read it (VitaParms
0.26 or later stores the verifier when uploading). Stored in `data/.access-log.php`; delete it to
clear the log.

License: MIT (VitaParms); font: SIL Open Font License 1.1.
