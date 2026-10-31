#!/usr/bin/env python3
"""VitaParms light – Android-Paketdateien aus denen der Tablet-App erzeugen.

Kopiert mobile/android nach light/mobile/android und passt an: Paketname
de.mbergmann.vitaparms.light, Name „VitaParms light“, grünes Programmsymbol. Die Java-Klassen
bleiben im Paket de.mbergmann.vitaparms (die App ruft sie über diesen Namen auf).
Nach Änderungen an mobile/android erneut ausführen:  python3 tools/make_light_android.py
"""
import shutil
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / 'mobile/android'
DST = ROOT / 'light/mobile/android'


def recolor(path):
    im = Image.open(path).convert('RGBA')
    px = im.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, a = px[x, y]
            if a and b > r + 40 and b > g:          # Blau → Blaugrün wie das Desktop-Symbol
                t = (255 - r) / 255.0
                px[x, y] = (int(255 - 255 * t), int(255 - (255 - 0x8A) * t), int(255 - (255 - 0x63) * t), a)
    im.save(path)


if DST.exists():
    shutil.rmtree(DST)
shutil.copytree(SRC, DST)
m = (DST / 'AndroidManifest.xml').read_text(encoding='utf-8')
m = m.replace('package="de.mbergmann.vitaparms"', 'package="de.mbergmann.vitaparms.light"')
m = m.replace('android:label="VitaParms"', 'android:label="VitaParms light"')
m = m.replace('<!-- VitaParms Tablet – Android-Manifest', '<!-- VitaParms light – Android-Manifest (erzeugt mit tools/make_light_android.py)')
assert 'de.mbergmann.vitaparms.light' in m and 'VitaParms light' in m
(DST / 'AndroidManifest.xml').write_text(m, encoding='utf-8')
for icon in DST.glob('res/drawable-*/icon.png'):
    recolor(icon)
print('light/mobile/android erzeugt')
