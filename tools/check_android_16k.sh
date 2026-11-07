#!/bin/bash
# VitaParms – prüft ein fertiges Android-Paket (.apk oder .aab) auf 16-KB-Speicherseiten
#
# Google Play verlangt seit 01.11.2025 für 64-Bit-Geräte (arm64-v8a, x86_64), dass jede native
# Bibliothek (.so) mit Segmenten auf 16-KB-Grenzen gebaut ist (ELF-LOAD-Ausrichtung ≥ 0x4000) und
# im APK unkomprimiert auf 16-KB-Grenzen liegt (zipalign -P 16).
#
# Aufruf:  tools/check_android_16k.sh <Paket.apk|Paket.aab>
# Braucht: unzip, readelf (binutils) oder llvm-readelf (NDK); für APKs zusätzlich zipalign aus den
#          Android-Build-Tools 35 oder neuer (wird in $ANDROID_HOME bzw. $ANDROID_SDK_ROOT gesucht).
# Ergebnis: Exit 0 = alles in Ordnung, 1 = mindestens eine Bibliothek passt nicht, 2 = Aufruffehler
# SPDX-License-Identifier: MIT
set -u
PKG=${1:-}
if [ -z "$PKG" ] || [ ! -f "$PKG" ]; then
    echo "Aufruf: $0 <Paket.apk|Paket.aab>" >&2; exit 2
fi
READELF=$(command -v llvm-readelf || command -v readelf || true)
if [ -z "$READELF" ]; then
    for d in "${ANDROID_NDK_ROOT:-}" "${ANDROID_NDK_HOME:-}"; do
        [ -n "$d" ] && f=$(ls "$d"/toolchains/llvm/prebuilt/*/bin/llvm-readelf 2>/dev/null | head -1) && [ -n "$f" ] && READELF=$f
    done
fi
[ -z "$READELF" ] && { echo "readelf bzw. llvm-readelf nicht gefunden (Paket binutils oder Android-NDK)" >&2; exit 2; }

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
unzip -q "$PKG" '*.so' -d "$TMP" 2>/dev/null
fail=0; n=0
while IFS= read -r so; do
    rel=${so#"$TMP"/}
    case "$rel" in */arm64-v8a/*|*/x86_64/*) ;; *) continue ;; esac   # 32-Bit-Bibliotheken: keine Vorgabe
    n=$((n + 1))
    min=$("$READELF" -lW "$so" | awk '$1 == "LOAD" { print $NF }' | sort -u | while read -r a; do printf '%d\n' "$a"; done | sort -n | head -1)
    if [ -z "$min" ] || [ "$min" -lt 16384 ]; then
        printf 'FEHLER  %-60s Ausrichtung %s\n' "$rel" "$(printf '0x%x' "${min:-0}")"; fail=1
    else
        printf 'ok      %-60s Ausrichtung %s\n' "$rel" "$(printf '0x%x' "$min")"
    fi
done < <(find "$TMP" -name '*.so' | sort)
echo "$n 64-Bit-Bibliotheken geprüft."

case "$PKG" in
*.apk)
    ZA=$(command -v zipalign || true)
    if [ -z "$ZA" ]; then
        for sdk in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Android/Sdk" "$HOME/Library/Android/sdk"; do
            [ -n "$sdk" ] && f=$(ls -d "$sdk"/build-tools/*/zipalign 2>/dev/null | sort -V | tail -1) && [ -n "$f" ] && { ZA=$f; break; }
        done
    fi
    if [ -n "$ZA" ]; then
        if "$ZA" -c -P 16 -v 4 "$PKG" > "$TMP/zipalign.txt" 2>&1; then
            echo "ok      zipalign: Bibliotheken liegen auf 16-KB-Grenzen"
        else
            grep -iE "lib/.*\.so.*BAD|Verification FAILED" "$TMP/zipalign.txt" | head -20
            echo "FEHLER  zipalign -c -P 16: Paket nicht auf 16 KB ausgerichtet (Build-Tools 35+ und AGP 8.5+ verwenden)"; fail=1
        fi
    else
        echo "Hinweis: zipalign nicht gefunden – Lage im APK nicht geprüft (Android-Build-Tools 35 oder neuer)"
    fi ;;
*.aab)
    echo "Hinweis: App Bundle – die Lage im Paket legt Google Play beim Ausliefern fest; geprüft wurden die Bibliotheken." ;;
esac
[ $fail -eq 0 ] && echo "Ergebnis: 16-KB-Speicherseiten unterstützt." || echo "Ergebnis: NICHT bereit für 16-KB-Speicherseiten."
exit $fail
