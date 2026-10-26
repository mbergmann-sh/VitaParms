# VitaParms – Website

Diese Website zeigt die mit VitaParms erfassten Daten **aller Personen** zum Ansehen:
persönliche Angaben, den gesamten Verlauf der Messwerte (Tabelle und Verlaufskurven),
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

Ab VitaParms 0.19 gehen die **Inhalte der Krankenakte** (PDF) getrennt über `api/files.php`:
einzeln, nur die fehlenden, auf dem Gerät mit AES-256 verschlüsselt, abgelegt in `sync/files/`
unter einer Kennung, die nichts über das Dokument verrät. Nicht mehr gebrauchte Inhalte werden
nach einem Tag gelöscht. Ohne `api/files.php` überträgt VitaParms die Datenbank wie bisher
vollständig (alle Geräte sollten dieselbe Version verwenden).

## Wartung

| Aufgabe | Vorgehen |
|---|---|
| neuen Upload-Schlüssel verwenden | in VitaParms *Neu* klicken, auf dem Server `data/.uploadkey.php` löschen, hochladen |
| Master-Passwort ändern | in VitaParms ändern und hochladen – das alte Passwort gilt danach nicht mehr |
| Website leeren | alle Unterordner von `data/` und `data/current.json` löschen |
| Abgleich zurücksetzen | Inhalt von `sync/` löschen (außer `.htaccess`), danach am Computer abgleichen |
| Fehler „post_max_size“ | `upload_max_filesize` und `post_max_size` beim Anbieter erhöhen (z. B. 64M) |
| endlose Umleitung | in `.htaccess` den Block `mod_rewrite` auskommentieren |

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

License: MIT (VitaParms); font: SIL Open Font License 1.1.
