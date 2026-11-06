#!/bin/bash
# VitaParms / VitaParms light – natives Debian-Paket (.deb) für die laufende Distribution bauen
#
#   installer/debian/build_deb.sh [full|light|all] [Optionen]
#
#   full            VitaParms                  → vitaparms_<V>-<R>~<Distribution>_<Arch>.deb
#   light           VitaParms light            → vitaparms-light_<V>-<R>~<Distribution>_<Arch>.deb
#   all             beide (Vorgabe)
#
#   --qt6 | --qt5   Qt-Version erzwingen (Vorgabe: Qt 6.4+ wenn vorhanden, sonst Qt 5.15)
#   --install-deps  fehlende Pakete zum Bauen mit apt-get installieren (fragt ggf. nach dem sudo-Passwort)
#   --check         nur die Voraussetzungen prüfen, nichts bauen
#   -j N            gleichzeitige Compiler-Aufrufe (Vorgabe: Anzahl der Prozessoren)
#   --out DIR       Ausgabeordner (Vorgabe: <Projekt>/dist-deb)
#   --revision N    Debian-Revision (Vorgabe: 1)
#   --keep          Build-Ordner behalten (sonst nach erfolgreichem Bau gelöscht)
#
# Das Paket wird gegen die Qt- und Systembibliotheken der Distribution gebaut, auf der das Skript
# läuft: Die Abhängigkeiten (Depends) ermittelt dpkg-shlibdeps aus den tatsächlich gelinkten
# Bibliotheken. Für Debian 12, Debian 13, Ubuntu 24.04 … daher jeweils auf (oder in einem Container
# mit) genau dieser Distribution bauen. Die Versionsnummer trägt die Distribution als Zusatz
# (z. B. 0.30.1-1~deb12), damit die Pakete der verschiedenen Distributionen unterscheidbar bleiben.
#
# Umgebungsvariablen: DEBFULLNAME, DEBEMAIL (Paketverwalter), VP_HOMEPAGE (Feld Homepage).
set -euo pipefail
umask 022
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)

EDITIONS="all"; QTWANT=auto; INSTALL_DEPS=0; CHECK_ONLY=0; KEEP=0; REV=1
JOBS=$(nproc 2>/dev/null || echo 2)
OUT="$ROOT/dist-deb"

die()  { echo "build_deb.sh: $*" >&2; exit 1; }
info() { echo "==> $*"; }
warn() { echo "Hinweis: $*" >&2; }

usage() { sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        full|light|all) EDITIONS="$1" ;;
        --qt6) QTWANT=6 ;;
        --qt5) QTWANT=5 ;;
        --install-deps) INSTALL_DEPS=1 ;;
        --check) CHECK_ONLY=1 ;;
        -j) shift; JOBS="${1:?-j braucht eine Zahl}" ;;
        -j*) JOBS="${1#-j}" ;;
        --out) shift; OUT="${1:?--out braucht einen Ordner}" ;;
        --revision) shift; REV="${1:?--revision braucht eine Zahl}" ;;
        --keep) KEEP=1 ;;
        -h|--help) usage 0 ;;
        *) echo "Unbekannte Angabe: $1" >&2; usage 1 ;;
    esac
    shift
done
[ "$EDITIONS" = all ] && EDITIONS="full light"

# ---------------------------------------------------------------- Distribution
[ -r /etc/os-release ] || die "/etc/os-release fehlt – keine Debian-artige Distribution?"
. /etc/os-release
command -v dpkg >/dev/null || die "dpkg fehlt – das Skript ist für Debian und seine Derivate gedacht"
DIST_ID="${ID:-linux}"; DIST_VER="${VERSION_ID:-}"; DIST_CODE="${VERSION_CODENAME:-}"
case "$DIST_ID" in
    debian) if [ -n "$DIST_VER" ]; then DIST_TAG="deb${DIST_VER%%.*}"; else DIST_TAG="deb${DIST_CODE:-sid}"; fi ;;
    *)      DIST_TAG="${DIST_ID}${DIST_VER:-$DIST_CODE}" ;;
esac
DIST_TAG=$(echo "$DIST_TAG" | tr -c 'A-Za-z0-9.+\n' '.')
ARCH=$(dpkg --print-architecture)
UPSTREAM=$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*=[[:space:]]*\([0-9][0-9.]*\).*/\1/p' "$ROOT/version.pri" | head -1)
[ -n "$UPSTREAM" ] || die "Version in version.pri nicht gefunden"
DEBVER="${UPSTREAM}-${REV}~${DIST_TAG}"
info "Distribution: ${PRETTY_NAME:-$DIST_ID} ($ARCH) – Paketversion $DEBVER"

# ---------------------------------------------------------------- Qt finden
qt_version() { "$1" -query QT_VERSION 2>/dev/null || true; }
find_qmake6() {
    for q in qmake6 /usr/lib/qt6/bin/qmake /usr/lib/qt6/bin/qmake6; do
        command -v "$q" >/dev/null 2>&1 || continue
        case "$(qt_version "$q")" in 6.*) echo "$(command -v "$q")"; return 0 ;; esac
    done
    return 1
}
find_qmake5() {
    for q in /usr/lib/qt5/bin/qmake qmake-qt5 qmake; do
        command -v "$q" >/dev/null 2>&1 || continue
        case "$(qt_version "$q")" in 5.15.*) echo "$(command -v "$q")"; return 0 ;; esac
    done
    return 1
}
ver_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]; }   # $1 >= $2

# Pakete zum Bauen (Namen gleich in Debian 12/13 und Ubuntu 24.04+)
PKGS_COMMON="build-essential dpkg-dev"
PKGS_QT6="qmake6 qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-l10n-tools libgl-dev"
PKGS_QT6_OPT="qt6-multimedia-dev qt6-speech-dev qt6-declarative-dev qt6-pdf-dev"
PKGS_QT5="qtbase5-dev qtbase5-dev-tools qttools5-dev qttools5-dev-tools qtchooser libgl-dev"
PKGS_QT5_OPT="qtmultimedia5-dev libqt5texttospeech5-dev"

apt_install() {
    local want=() p SUDO=""
    [ "$(id -u)" -eq 0 ] || SUDO="sudo"
    $SUDO apt-get update
    for p in "$@"; do
        # nur Pakete anfordern, die es in dieser Distribution gibt (z. B. qt6-pdf-dev nicht auf jeder Architektur)
        if apt-cache show "$p" >/dev/null 2>&1; then want+=("$p"); else warn "Paket $p gibt es hier nicht – übersprungen"; fi
    done
    [ ${#want[@]} -gt 0 ] || return 0
    info "Installiere: ${want[*]}"
    $SUDO apt-get install -y --no-install-recommends "${want[@]}"
}

QMAKE=""
pick_qt() {
    QMAKE=""; QTMAJOR=""
    if [ "$QTWANT" != 5 ]; then
        if Q=$(find_qmake6) && ver_ge "$(qt_version "$Q")" 6.4.0; then QMAKE=$Q; QTMAJOR=6; fi
    fi
    if [ -z "$QMAKE" ] && [ "$QTWANT" != 6 ]; then
        if Q=$(find_qmake5); then QMAKE=$Q; QTMAJOR=5; fi
    fi
}
pick_qt
if [ -z "$QMAKE" ] && [ "$INSTALL_DEPS" = 1 ]; then
    if [ "$QTWANT" = 5 ]; then apt_install $PKGS_COMMON $PKGS_QT5 $PKGS_QT5_OPT
    else apt_install $PKGS_COMMON $PKGS_QT6 $PKGS_QT6_OPT; fi
    pick_qt
fi
if [ -z "$QMAKE" ]; then
    echo "Kein passendes Qt gefunden (VitaParms braucht Qt 6.4 oder neuer, ersatzweise Qt 5.15)." >&2
    echo "Installieren mit:" >&2
    echo "  sudo apt install $PKGS_COMMON $PKGS_QT6 $PKGS_QT6_OPT" >&2
    echo "oder (ältere Distributionen, z. B. Debian 11, Ubuntu 22.04):" >&2
    echo "  sudo apt install $PKGS_COMMON $PKGS_QT5 $PKGS_QT5_OPT   und dann  build_deb.sh --qt5" >&2
    echo "oder das Skript mit --install-deps aufrufen." >&2
    exit 1
fi
QTVER=$(qt_version "$QMAKE")
info "Qt $QTVER ($QMAKE)"

# ---------------------------------------------------------------- Voraussetzungen prüfen
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
probe_modules() {
    mkdir -p "$TMP/probe"
    cat > "$TMP/probe/probe.pro" <<'EOF'
TEMPLATE = aux
for(m, $$list(widgets printsupport uitools network multimedia texttospeech pdfwidgets)) {
    !qtHaveModule($$m): VP_MISSING += $$m
}
message(VP_MISSING=$$VP_MISSING)
message(VP_BINS=$$[QT_HOST_BINS] $$[QT_INSTALL_BINS] $$[QT_HOST_LIBEXECS] $$[QT_INSTALL_LIBEXECS])
EOF
    (cd "$TMP/probe" && "$QMAKE" probe.pro 2>&1) | sed -n 's/.*Project MESSAGE: //p'
}
check_prereqs() {
    local missing_tools="" t
    for t in make g++ dpkg-deb dpkg-shlibdeps strip gzip; do
        command -v "$t" >/dev/null 2>&1 || missing_tools="$missing_tools $t"
    done
    local probe; probe=$(probe_modules)
    MISSING_MODS=$(echo "$probe" | sed -n 's/^VP_MISSING=//p')
    local bins; bins=$(echo "$probe" | sed -n 's/^VP_BINS=//p')
    LRELEASE=""
    for d in $bins; do [ -x "$d/lrelease" ] && { LRELEASE="$d/lrelease"; break; }; done
    REQ_MISSING=""; OPT_MISSING=""
    for m in $MISSING_MODS; do
        case "$m" in multimedia|texttospeech|pdfwidgets) OPT_MISSING="$OPT_MISSING $m" ;; *) REQ_MISSING="$REQ_MISSING $m" ;; esac
    done
    [ -z "$missing_tools" ] && [ -z "$REQ_MISSING" ] && [ -n "$LRELEASE" ]
    local ok=$?
    if [ $ok -ne 0 ]; then
        echo "Es fehlt etwas zum Bauen:" >&2
        [ -n "$missing_tools" ] && echo "  Werkzeuge:$missing_tools" >&2
        [ -n "$REQ_MISSING" ] && echo "  Qt-Module:$REQ_MISSING" >&2
        [ -z "$LRELEASE" ] && echo "  lrelease (Qt-Übersetzungswerkzeug)" >&2
    fi
    return $ok
}
if ! check_prereqs; then
    if [ "$INSTALL_DEPS" = 1 ]; then
        if [ "$QTMAJOR" = 6 ]; then apt_install $PKGS_COMMON $PKGS_QT6 $PKGS_QT6_OPT
        else apt_install $PKGS_COMMON $PKGS_QT5 $PKGS_QT5_OPT; fi
        check_prereqs || die "Voraussetzungen fehlen weiterhin"
    else
        if [ "$QTMAJOR" = 6 ]; then echo "Installieren mit:  sudo apt install $PKGS_COMMON $PKGS_QT6 $PKGS_QT6_OPT" >&2
        else echo "Installieren mit:  sudo apt install $PKGS_COMMON $PKGS_QT5 $PKGS_QT5_OPT" >&2; fi
        echo "(oder build_deb.sh --install-deps)" >&2
        exit 1
    fi
fi
if [ -n "$OPT_MISSING" ]; then
    if [ "$INSTALL_DEPS" = 1 ]; then
        if [ "$QTMAJOR" = 6 ]; then apt_install $PKGS_QT6_OPT; else apt_install $PKGS_QT5_OPT; fi
        check_prereqs || true
    fi
    [ -n "$OPT_MISSING" ] && warn "ohne die Qt-Module$OPT_MISSING – das Paket wird trotzdem gebaut, aber ohne:
      multimedia   → Signaltöne (statt dessen Systempiepton)
      texttospeech → Sprachausgabe im Medikamenten-Assistenten
      pdfwidgets   → PDF-Vorschau, Volltextsuche und Direktdruck in der Krankenakte
    Nachinstallieren: $( [ "$QTMAJOR" = 6 ] && echo "$PKGS_QT6_OPT" || echo "$PKGS_QT5_OPT" )"
fi
info "Voraussetzungen vollständig (lrelease: $LRELEASE)"
[ "$CHECK_ONLY" = 1 ] && exit 0
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)

# ---------------------------------------------------------------- Bauen und packen
MAINT_NAME="${DEBFULLNAME:-Michael Bergmann}"
MAINT_MAIL="${DEBEMAIL:-mbergmann-sh@users.noreply.github.com}"
DATE_R=$(date -R)
DATE_ISO=$(date +%Y-%m-%d)
DATE_MAN=$(date +"%B %Y")
HAS() { case " $MISSING_MODS " in *" $1 "*) return 1 ;; *) return 0 ;; esac; }

build_edition() {
    local ed="$1" SYNOPSIS PKG BIN TARGET PRO NAME APPID SETTINGS DOCDIR SUM_EN SUM_DE DESC_EN DESC_DE
    if [ "$ed" = full ]; then
        PKG=vitaparms; BIN=vitaparms; TARGET=VitaParms; PRO="$ROOT/VitaParms.pro"; NAME="VitaParms"
        APPID=de.mbergmann.vitaparms; SETTINGS=VitaParms; DOCDIR="$ROOT/docs"
        SUM_EN="Vital signs diary for several persons"
        SYNOPSIS="vital signs diary for several persons"
        SUM_DE="Vitalwerte-Tagebuch für mehrere Personen"
        DESC_EN="records vital signs (blood pressure, pulse, temperature, oxygen saturation, blood sugar, weight, pain and more) for several persons, with master record, medication plan, vaccination record, appointments and reminders, charts, statistics, print, PDF and Excel export, synchronisation with the tablet app and an optional website."
        DESC_DE="erfasst Vitalwerte (Blutdruck, Puls, Temperatur, Sauerstoffsättigung, Blutzucker, Gewicht, Schmerz u. a.) für mehrere Personen, mit Stammblatt, Medikamentenplan, Impfpass, Terminen und Erinnerungen, Grafiken, Statistiken, Druck, PDF- und Excel-Export, Abgleich mit der Tablet-App und optionaler Website."
    else
        PKG=vitaparms-light; BIN=vitaparms-light; TARGET=VitaParmsLight; PRO="$ROOT/light/VitaParmsLight.pro"; NAME="VitaParms light"
        APPID=de.mbergmann.vitaparms.light; SETTINGS=VitaParmsLight; DOCDIR="$ROOT/light/docs"
        SUM_EN="Simple vital signs diary for one person"
        SYNOPSIS="simple vital signs diary for one person (VitaParms light)"
        SUM_DE="Einfaches Vitalwerte-Tagebuch für eine Person"
        DESC_EN="is the simplified edition of VitaParms for one person: vital signs with charts and statistics, master record with contacts, diagnoses, vaccination record and medication plan, day profiles, appointments and reminders, print, PDF and Excel export and synchronisation with the tablet or phone app."
        DESC_DE="ist die vereinfachte Ausgabe von VitaParms für eine Person: Vitalwerte mit Grafiken und Statistiken, Stammblatt mit Kontakten, Diagnosen, Impfpass und Medikamentenplan, Tagesprofile, Termine und Erinnerungen, Druck, PDF- und Excel-Export sowie Abgleich mit der App für Tablet oder Smartphone."
    fi
    local B="$OUT/build/$PKG-qt$QTMAJOR" P="$OUT/build/$PKG-root"
    info "$NAME: übersetzen (Qt $QTVER, $JOBS Prozesse) in $B"
    rm -rf "$B" "$P"; mkdir -p "$B"
    (cd "$B" && "$QMAKE" "$PRO" CONFIG+=release CONFIG-=debug CONFIG+=vp_no_installer > qmake.log 2>&1) \
        || { cat "$B/qmake.log" >&2; die "qmake für $NAME fehlgeschlagen"; }
    (cd "$B" && make -j"$JOBS" > make.log 2>&1) || { tail -40 "$B/make.log" >&2; die "Übersetzen von $NAME fehlgeschlagen (Protokoll: $B/make.log)"; }
    local EXE="$B/$TARGET"
    [ -x "$EXE" ] || EXE=$(find "$B" -maxdepth 2 -type f -name "$TARGET" -perm -u+x | head -1)
    [ -x "$EXE" ] || die "Programmdatei $TARGET nicht gefunden"

    info "$NAME: Paketinhalt zusammenstellen"
    install -Dm755 "$EXE" "$P/usr/bin/$BIN"
    strip --strip-unneeded --remove-section=.comment --remove-section=.note "$P/usr/bin/$BIN"
    install -Dm644 "$HERE/$BIN.desktop" "$P/usr/share/applications/$BIN.desktop"
    local s
    for s in 16 24 32 48 64 128 256; do
        install -Dm644 "$HERE/icons/$BIN/$s.png" "$P/usr/share/icons/hicolor/${s}x${s}/apps/$BIN.png"
    done
    # Handbücher (PDF); das Programm sucht sie unter /usr/share/<programm>/docs (F1, Menü Hilfe)
    local n=0 f
    for f in "$DOCDIR"/*.pdf; do
        [ -f "$f" ] || continue
        install -Dm644 "$f" "$P/usr/share/$BIN/docs/$(basename "$f")"; n=$((n + 1))
    done
    [ "$n" -gt 0 ] || warn "keine Handbücher (PDF) in $DOCDIR – Paket ohne Handbuch"
    # Platzhalter in Vorlagen ersetzen (| als Trenner: Texte enthalten /)
    subst() {
        sed -e "s|@APPID@|$APPID|g" -e "s|@NAME@|$NAME|g" -e "s|@BIN@|$BIN|g" -e "s|@BINUP@|$(echo "$BIN" | tr a-z A-Z)|g" \
            -e "s|@DESKTOP@|$BIN.desktop|g" -e "s|@VERSION@|$UPSTREAM|g" -e "s|@DATE@|$1|g" -e "s|@SETTINGS@|$SETTINGS|g" \
            -e "s|@SUMMARY_EN@|$SUM_EN|g" -e "s|@SUMMARY_DE@|$SUM_DE|g" -e "s|@DESC_EN@|$NAME $DESC_EN|g" -e "s|@DESC_DE@|$NAME $DESC_DE|g"
    }
    mkdir -p "$P/usr/share/metainfo" "$P/usr/share/man/man1" "$P/usr/share/man/de/man1" "$P/usr/share/doc/$PKG"
    subst "$DATE_ISO" < "$HERE/metainfo.xml.in" \
        | sed "s|  <!--HOMEPAGE-->|$( [ -n "${VP_HOMEPAGE:-}" ] && echo "  <url type=\"homepage\">$VP_HOMEPAGE</url>" )|" \
        | sed '/^$/d' > "$P/usr/share/metainfo/$APPID.metainfo.xml"
    # Manpage: Beschreibung beginnt mit dem fett gesetzten Programmnamen (.B), daher dort ohne Namen
    subst "$DATE_MAN" < "$HERE/man/vitaparms.1.in" | sed "s|^$NAME $DESC_EN|$DESC_EN|" | gzip -9n > "$P/usr/share/man/man1/$BIN.1.gz"
    subst "$DATE_MAN" < "$HERE/man/vitaparms.de.1.in" | sed "s|^$NAME $DESC_DE|$DESC_DE|" | gzip -9n > "$P/usr/share/man/de/man1/$BIN.1.gz"
    install -m644 "$HERE/copyright" "$P/usr/share/doc/$PKG/copyright"
    printf '%s (%s) %s; urgency=medium\n\n  * Build of VitaParms %s for %s (Qt %s).\n\n -- %s <%s>  %s\n' \
        "$PKG" "$DEBVER" "${DIST_CODE:-unstable}" "$UPSTREAM" "${PRETTY_NAME:-$DIST_ID}" "$QTVER" \
        "$MAINT_NAME" "$MAINT_MAIL" "$DATE_R" | gzip -9n > "$P/usr/share/doc/$PKG/changelog.Debian.gz"

    # Abhängigkeiten aus den gelinkten Bibliotheken (dpkg-shlibdeps braucht ein debian/control)
    info "$NAME: Abhängigkeiten ermitteln"
    mkdir -p "$TMP/shl/debian"
    printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$PKG" "$PKG" > "$TMP/shl/debian/control"
    local DEPS
    DEPS=$(cd "$TMP/shl" && dpkg-shlibdeps -O "-e$P/usr/bin/$BIN" 2>"$TMP/shl/warn.log" | sed -n 's/^shlibs:Depends=//p')
    [ -n "$DEPS" ] || { cat "$TMP/shl/warn.log" >&2; die "dpkg-shlibdeps lieferte keine Abhängigkeiten"; }
    # Was nicht gelinkt, sondern zur Laufzeit geladen wird: Übersetzungen, Wayland, Sprach-/Ton-Plugins
    local RECS
    if [ "$QTMAJOR" = 6 ]; then
        RECS="qt6-translations-l10n, qt6-wayland"
        HAS texttospeech && RECS="$RECS, qt6-speech-speechd-plugin | qt6-speech-flite-plugin"
    else
        RECS="qttranslations5-l10n, qtwayland5"
        HAS multimedia && RECS="$RECS, libqt5multimedia5-plugins"
        HAS texttospeech && RECS="$RECS, qtspeech5-speechd-plugin | qtspeech5-flite-plugin"
    fi
    local SIZE; SIZE=$(du -sk --apparent-size "$P/usr" | cut -f1)
    mkdir -p "$P/DEBIAN"
    {
        echo "Package: $PKG"
        echo "Version: $DEBVER"
        echo "Architecture: $ARCH"
        echo "Maintainer: $MAINT_NAME <$MAINT_MAIL>"
        echo "Installed-Size: $SIZE"
        echo "Depends: $DEPS"
        echo "Recommends: $RECS"
        echo "Section: misc"
        echo "Priority: optional"
        [ -n "${VP_HOMEPAGE:-}" ] && echo "Homepage: $VP_HOMEPAGE"
        echo "Description: $SYNOPSIS"
        echo "$NAME $DESC_EN" | fold -s -w 72 | sed 's/ *$//; s/^/ /'
        echo " ."
        echo " This is a diary for health-related data, not a medical device. Use at"
        echo " your own risk. Built for ${PRETTY_NAME:-$DIST_ID} with Qt $QTVER."
    } > "$P/DEBIAN/control"
    find "$P" -type d -exec chmod 755 {} +
    (cd "$P" && find usr -type f -print0 | LC_ALL=C sort -z | xargs -0 md5sum > DEBIAN/md5sums)
    chmod 644 "$P/DEBIAN/md5sums"
    local DEB="$OUT/${PKG}_${DEBVER}_${ARCH}.deb"
    dpkg-deb --root-owner-group -Zxz --build "$P" "$DEB" >/dev/null
    info "$NAME: $DEB"
    if command -v lintian >/dev/null 2>&1; then
        local LA=""; [ "$(id -u)" -eq 0 ] && LA="--allow-root"
        lintian $LA --tag-display-limit 0 "$DEB" > "$OUT/${PKG}_${DEBVER}_${ARCH}.lintian.txt" 2>&1 || true
        local e w
        e=$(grep -c '^E:' "$OUT/${PKG}_${DEBVER}_${ARCH}.lintian.txt" || true)
        w=$(grep -c '^W:' "$OUT/${PKG}_${DEBVER}_${ARCH}.lintian.txt" || true)
        info "$NAME: lintian – $e Fehler, $w Warnungen (siehe ${PKG}_${DEBVER}_${ARCH}.lintian.txt)"
    fi
    [ "$KEEP" = 1 ] || rm -rf "$B" "$P"
}

for ed in $EDITIONS; do build_edition "$ed"; done
[ "$KEEP" = 1 ] || rmdir "$OUT/build" 2>/dev/null || true
echo
info "Fertig. Installieren mit:  sudo apt install $OUT/<paket>.deb"
ls -1 "$OUT"/*.deb
