#!/usr/bin/env python3
"""Fill translations/vitaparms_en.ts from translations/en_dict.py.

Usage (from the project root):
    lupdate -no-obsolete VitaParms.pro
    python3 tools/apply_translations.py
    lupdate -no-obsolete mobile/VitaParmsMobile.pro
    python3 tools/apply_translations.py mobile/translations/vitaparms_en.ts
Strings without an entry stay 'unfinished' and are listed.
"""
import os, sys
import xml.etree.ElementTree as ET

root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(root_dir, 'translations'))
from en_dict import EN, CONTEXT

ts = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root_dir, 'translations', 'vitaparms_en.ts')
tree = ET.parse(ts)
missing = []
for ctx in tree.getroot().findall('context'):
    cname = ctx.find('name').text
    for msg in ctx.findall('message'):
        src = msg.find('source').text
        tr = msg.find('translation')
        en = CONTEXT.get((cname, src), EN.get(src))
        if en is None:
            missing.append((cname, src))
            continue
        tr.text = en
        tr.attrib.pop('type', None)
tree.write(ts, encoding='utf-8', xml_declaration=True)
for c, s in missing:
    print('MISSING', c, repr(s))
print('missing:', len(missing))
