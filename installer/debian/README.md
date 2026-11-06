# VitaParms und VitaParms light als Debian-Paket (.deb)

`build_deb.sh` baut **native Installationspakete** für Debian und seine Derivate (Ubuntu, Linux Mint,
Raspberry Pi OS, MX Linux, Pop!_OS …). Das Paket wird mit dem Qt der Distribution übersetzt und
gegen deren Systembibliotheken gelinkt – es bringt **keine eigenen Qt-Bibliotheken** mit, startet ohne
Startskript und wird mit `apt` installiert, aktualisiert und entfernt.

Weil sich Bibliotheksversionen zwischen den Distributionen unterscheiden, gilt:
**Ein Paket gehört zu genau der Distribution, auf der es gebaut wurde.** Für Debian 12 und Debian 13
also je einmal bauen (auf dem Rechner selbst, in einer virtuellen Maschine oder in einem Container).
Die Abhängigkeiten (`Depends:`) ermittelt `dpkg-shlibdeps` automatisch aus den tatsächlich
gelinkten Bibliotheken, die Distribution steht in der Versionsnummer:

    vitaparms_0.30.1-1~deb12_amd64.deb            VitaParms für Debian 12
    vitaparms-light_0.30.1-1~deb13_amd64.deb      VitaParms light für Debian 13
    vitaparms_0.30.1-1~ubuntu24.04_amd64.deb      VitaParms für Ubuntu 24.04

Das Bauen ist bewusst **nicht** in den qmake-Build eingebunden, sondern wird von Hand gestartet.

## 1. Voraussetzungen (Pakete zum Bauen)

VitaParms braucht **Qt 6.4 oder neuer**; auf älteren Distributionen ersatzweise **Qt 5.15**
(dann ohne Qt PDF, siehe unten).

| Distribution | Qt | Bauen mit |
|---|---|---|
| Debian 12 „bookworm“, Raspberry Pi OS 12, MX Linux 23 | 6.4.2 | Qt 6 |
| Debian 13 „trixie“ | 6.8.2 | Qt 6 |
| Ubuntu 24.04 LTS, Linux Mint 22, Pop!_OS 24.04 und neuer | 6.4.2 oder neuer | Qt 6 |
| Debian 11 „bullseye“, Ubuntu 22.04 LTS, Linux Mint 21 | Qt 6 fehlt bzw. 6.2 (zu alt) | Qt 5.15 mit `--qt5` |

### Qt 6 (Debian 12/13, Ubuntu 24.04 und neuer)

```sh
sudo apt install build-essential dpkg-dev \
     qmake6 qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-l10n-tools libgl-dev \
     qt6-multimedia-dev qt6-speech-dev qt6-declarative-dev qt6-pdf-dev
```

| Paket | wofür |
|---|---|
| `build-essential` | C/C++-Compiler (g++) und `make` |
| `dpkg-dev` | `dpkg-shlibdeps` (Abhängigkeiten ermitteln); `dpkg-deb` gehört zu `dpkg` |
| `qmake6`, `qt6-base-dev`, `qt6-base-dev-tools` | Qt Core/Gui/Widgets/Network/PrintSupport, moc, uic, rcc |
| `qt6-tools-dev` | Qt UiTools (anpassbare Formulare) |
| `qt6-l10n-tools` | `lrelease` – englische Übersetzung wird ins Programm eingebettet |
| `libgl-dev` | OpenGL-Header, die Qt Gui verlangt |
| `qt6-multimedia-dev` | *optional:* Signaltöne bei Erinnerungen (sonst Systempiepton) |
| `qt6-speech-dev` | *optional:* Sprachausgabe im Medikamenten-Assistenten |
| `qt6-declarative-dev` | nur zusammen mit `qt6-speech-dev`: die Qt-6.4-Pakete von TextToSpeech binden einen Header aus Qt Declarative ein (Debian 12, Ubuntu 24.04) |
| `qt6-pdf-dev` | *optional:* PDF-Vorschau, Volltextsuche und Direktdruck in der Krankenakte (Teil von Qt WebEngine, nicht auf jeder Prozessorarchitektur vorhanden) |

### Qt 5.15 (Debian 11, Ubuntu 22.04, Linux Mint 21)

```sh
sudo apt install build-essential dpkg-dev \
     qtbase5-dev qtbase5-dev-tools qttools5-dev qttools5-dev-tools qtchooser libgl-dev \
     qtmultimedia5-dev libqt5texttospeech5-dev
```

Mit Qt 5 gibt es kein Qt PDF: Dokumente der Krankenakte öffnen im PDF-Programm des Systems,
Volltextsuche und Direktdruck aus der Akte entfallen.

### Optional

`lintian` prüft das fertige Paket auf Verstöße gegen die Debian-Richtlinien; ist es installiert,
legt `build_deb.sh` den Bericht neben das Paket (`….lintian.txt`).

Statt die Pakete von Hand zu installieren, kann das Skript das selbst erledigen:
`build_deb.sh --install-deps` (installiert mit `apt-get`, fragt nach dem sudo-Passwort; nicht
vorhandene optionale Pakete werden übersprungen).

## 2. Bauen

Im entpackten Projektordner:

```sh
installer/debian/build_deb.sh --check        # nur prüfen, ob alles da ist
installer/debian/build_deb.sh                # VitaParms und VitaParms light
installer/debian/build_deb.sh full           # nur VitaParms
installer/debian/build_deb.sh light          # nur VitaParms light
installer/debian/build_deb.sh --qt5          # mit Qt 5.15 (ältere Distributionen)
```

Weitere Angaben: `-j N` (gleichzeitige Compiler-Aufrufe), `--out ORDNER` (Vorgabe: `dist-deb/`
im Projekt), `--revision N` (Debian-Revision, Vorgabe 1), `--keep` (Build-Ordner behalten).
Als Paketverwalter steht „Michael Bergmann“ im Paket; anders mit den üblichen Variablen
`DEBFULLNAME` und `DEBEMAIL`. `VP_HOMEPAGE` füllt das Feld *Homepage*.

Gebaut wird außerhalb des Quellordners (`dist-deb/build/…`); der Quellbaum bleibt sauber.
Ein Durchlauf für beide Ausgaben dauert je nach Rechner etwa 5–15 Minuten.

## 3. Installieren, aktualisieren, entfernen

```sh
sudo apt install ./dist-deb/vitaparms_0.30.1-1~deb12_amd64.deb
sudo apt install ./dist-deb/vitaparms-light_0.30.1-1~deb12_amd64.deb
```

`apt` (nicht `dpkg -i`) installiert die fehlenden Qt-Laufzeitpakete gleich mit. Eine neuere
Version wird genauso installiert und ersetzt die alte; entfernen mit `sudo apt remove vitaparms`
bzw. `vitaparms-light`. **Daten und Einstellungen bleiben dabei erhalten** – sie liegen im
Benutzerordner (`~/.local/share/VitaParms/…`, `~/.config/VitaParms/…`), nicht im Paket.

VitaParms und VitaParms light lassen sich nebeneinander installieren.

## 4. Inhalt des Pakets

| Pfad | Inhalt |
|---|---|
| `/usr/bin/vitaparms`, `/usr/bin/vitaparms-light` | Programm (Option `--demo` startet die Demo-Datenbank) |
| `/usr/share/applications/*.desktop` | Eintrag im Anwendungsmenü (Kategorie Büro), Zusatzaktion „Demo-Datenbank“ |
| `/usr/share/icons/hicolor/<Größe>/apps/` | Programmsymbol in 16–256 Pixeln |
| `/usr/share/metainfo/de.mbergmann.vitaparms*.metainfo.xml` | Beschreibung für Software-Center (AppStream) |
| `/usr/share/vitaparms/docs/`, `/usr/share/vitaparms-light/docs/` | Handbücher als PDF (F1, Menü Hilfe) |
| `/usr/share/man/man1/`, `/usr/share/man/de/man1/` | Manpage englisch und deutsch |
| `/usr/share/doc/<paket>/copyright` | Lizenzen (MIT; SQLite3 Multiple Ciphers und Bestandteile) |

Zur Laufzeit lädt Qt einige Teile nur bei Bedarf; das Paket **empfiehlt** sie (`Recommends:`,
von `apt` standardmäßig mitinstalliert): deutsche Qt-Texte (`qt6-translations-l10n`),
Wayland-Unterstützung (`qt6-wayland`) und eine Sprachausgabe (`qt6-speech-speechd-plugin` oder
`qt6-speech-flite-plugin`).

## 5. Für jede Distribution einmal bauen – Container

Ohne passenden Rechner geht es mit einem Container, z. B. mit Docker oder Podman (Projektordner
als `/src` eingebunden, Pakete landen in `dist-deb/`):

```sh
docker run --rm -it -v "$PWD":/src -w /src debian:12 \
    sh -c 'apt-get update && apt-get install -y sudo && installer/debian/build_deb.sh --install-deps'
docker run --rm -it -v "$PWD":/src -w /src debian:13      sh -c '…wie oben…'
docker run --rm -it -v "$PWD":/src -w /src ubuntu:24.04   sh -c '…wie oben…'
```

Das im Container gebaute Paket gehört dem Benutzer root; mit `sudo chown -R $USER dist-deb`
zurückholen.

## Hinweise

- Die Pakete der Windows-, macOS- und Qt-Installer-Framework-Installer (`installer/`,
  `light/installer/`) bleiben unverändert; das Debian-Paket ist ein zusätzlicher Weg für Linux.
- Ein Debian-Paket enthält immer genau eine Prozessorarchitektur – auf einem Raspberry Pi (arm64)
  gebaut, entsteht `…_arm64.deb`.
