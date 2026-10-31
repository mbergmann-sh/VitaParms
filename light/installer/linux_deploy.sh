#!/bin/sh
# VitaParms light – Linux: Programm mit allen Qt-Bibliotheken und Plugins in einen Ordner stellen
#
#   linux_deploy.sh <Programm> <qmake> <Zielordner> [<Symbol.png>]
#
# Ergebnis (läuft ohne installiertes Qt):
#   <Ziel>/VitaParmsLight          Programm
#   <Ziel>/VitaParmsLight.sh       Startskript (setzt den Bibliothekspfad)
#   <Ziel>/lib/                    Qt-Bibliotheken (und ICU), aus dem Qt-Bibliotheksordner
#   <Ziel>/plugins/                Plattform-, Bild-, Druck-, TLS-, Multimedia-Plugins …
#   <Ziel>/translations/           Qt-Übersetzungen (deutsch)
#   <Ziel>/qt.conf                 Pfade für Qt
# Systembibliotheken (glibc, X11/xcb, fontconfig, OpenGL, CUPS …) werden nicht kopiert – sie gehören
# zu jedem Linux-Desktop.
set -e
EXE="$1"; QMAKE="$2"; DEST="$3"; ICON="$4"
[ -x "$EXE" ] || { echo "linux_deploy.sh: Programm $EXE fehlt" >&2; exit 1; }
QTLIBS=$(readlink -f "$("$QMAKE" -query QT_INSTALL_LIBS)")
QTPLUGINS=$("$QMAKE" -query QT_INSTALL_PLUGINS)
QTTRANS=$("$QMAKE" -query QT_INSTALL_TRANSLATIONS)
NAME=$(basename "$EXE")
mkdir -p "$DEST/lib" "$DEST/plugins" "$DEST/translations"
cp "$EXE" "$DEST/$NAME"
[ -n "$ICON" ] && [ -f "$ICON" ] && cp "$ICON" "$DEST/vitaparms-light.png"

# Plugins, die eine Widgets-Anwendung mit Druck, PDF, Netzwerk und Tönen braucht
for d in platforms platforminputcontexts platformthemes xcbglintegrations imageformats iconengines \
         printsupport tls networkinformation multimedia texttospeech wayland-shell-integration \
         wayland-decoration-client wayland-graphics-integration-client; do
    [ -d "$QTPLUGINS/$d" ] || continue
    mkdir -p "$DEST/plugins/$d"
    cp -L "$QTPLUGINS/$d"/*.so "$DEST/plugins/$d/" 2>/dev/null || true
done
# Multimedia: das FFmpeg-Backend ist groß und für die zwei Signaltöne nicht nötig
rm -f "$DEST/plugins/multimedia/"*ffmpeg* 2>/dev/null || true

# Bibliotheken aus dem Qt-Bibliotheksordner, die Programm und Plugins brauchen (wiederholt, bis nichts dazukommt)
copy_deps() {
    ldd "$1" 2>/dev/null | awk '/=>/ { print $3 }' | while read -r l; do
        lib=$(readlink -f "$l" 2>/dev/null || echo "$l")
        case "$lib" in
            "$QTLIBS"/*)
                base=$(basename "$l")
                if [ ! -e "$DEST/lib/$base" ]; then cp -L "$lib" "$DEST/lib/$base"; echo new; fi ;;
        esac
    done
}
changed=1
while [ "$changed" -gt 0 ]; do
    changed=0
    for f in "$DEST/$NAME" "$DEST"/lib/*.so* $(find "$DEST/plugins" -name '*.so'); do
        [ -f "$f" ] || continue
        n=$(copy_deps "$f" | wc -l)
        changed=$((changed + n))
    done
done
# Bei einem Qt aus der Paketverwaltung liegen auch Fremdbibliotheken im Qt-Ordner (/usr/lib/<arch>):
# nur Qt und ICU mitnehmen, den Rest stellt das System bereit
for f in "$DEST"/lib/*; do
    case "$(basename "$f")" in
        libQt*|libicu*|libdouble-conversion*|libb2*|libpcre2-16*|libmd4c*) ;;
        *) rm -f "$f" ;;
    esac
done

for q in qtbase_de.qm qt_de.qm qtmultimedia_de.qm; do
    [ -f "$QTTRANS/$q" ] && cp "$QTTRANS/$q" "$DEST/translations/" || true
done

cat > "$DEST/qt.conf" <<CONF
[Paths]
Prefix = .
Libraries = lib
Plugins = plugins
Translations = translations
CONF
cat > "$DEST/$NAME.sh" <<SH
#!/bin/sh
# Startet $NAME mit den mitgelieferten Qt-Bibliotheken
HERE=\$(dirname "\$(readlink -f "\$0")")
export LD_LIBRARY_PATH="\$HERE/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec "\$HERE/$NAME" "\$@"
SH
chmod +x "$DEST/$NAME.sh" "$DEST/$NAME"
echo "linux_deploy.sh: $DEST bereit ($(ls "$DEST/lib" | wc -l) Bibliotheken, $(find "$DEST/plugins" -name '*.so' | wc -l) Plugins)"
