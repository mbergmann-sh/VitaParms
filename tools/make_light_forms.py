#!/usr/bin/env python3
"""VitaParms light – vereinfachte Formulare aus den Standardformularen erzeugen.

Erzeugt forms/light/patientform.ui und forms/light/vitalform.ui aus forms/*.ui, indem Bereiche
entfernt werden, die VitaParms light nicht anbietet. Nach Änderungen an den Standardformularen
erneut ausführen:  python3 tools/make_light_forms.py
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def parents(tree):
    return {c: p for p in tree.iter() for c in p}


def find_widget(tree, name):
    for w in tree.iter('widget'):
        if w.get('name') == name:
            return w
    for l in tree.iter('layout'):
        if l.get('name') == name:
            return l
    raise SystemExit(f'Element {name} nicht gefunden')


def remove(tree, name):
    """Element samt umgebendem <item> entfernen."""
    par = parents(tree)
    el = find_widget(tree, name)
    node = el
    if par[el].tag == 'item':
        node = par[el]
    par[node].remove(node)


def remove_form_rows(tree, layout_name, names):
    """Zeilen eines QFormLayout entfernen (Label und Feld) und neu nummerieren."""
    lay = find_widget(tree, layout_name)
    rows = set()
    for item in list(lay):
        if item.tag != 'item':
            continue
        names_in = {e.get('name') for e in item.iter() if e.get('name')}
        if names_in & set(names):
            rows.add(item.get('row'))
    for item in list(lay):
        if item.tag == 'item' and item.get('row') in rows:
            lay.remove(item)
    order = sorted({int(i.get('row')) for i in lay if i.tag == 'item'})
    remap = {str(o): str(n) for n, o in enumerate(order)}
    for i in lay:
        if i.tag == 'item':
            i.set('row', remap[i.get('row')])


def set_prop(el, prop, value):
    for p in el.findall('property'):
        if p.get('name') == prop:
            p.find('*').text = value
            return
    raise SystemExit(f'Eigenschaft {prop} fehlt')


def set_title(tree, name, title):
    w = find_widget(tree, name)
    for a in w.findall('attribute'):
        if a.get('name') == 'title':
            a.find('string').text = title
            return
    set_prop(w, 'title', title)


def tabstops(tree):
    names = {w.get('name') for w in tree.iter('widget')}
    ts = tree.find('tabstops')
    if ts is not None:
        for t in list(ts):
            if t.text not in names:
                ts.remove(t)


def header(text):
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<!--\n' + text +
            '\n  Erzeugt mit tools/make_light_forms.py aus dem Standardformular – Änderungen dort vornehmen.\n-->\n')


def write(tree, path, comment):
    tabstops(tree)
    ET.indent(tree, space=' ')
    body = ET.tostring(tree.getroot(), encoding='unicode')
    Path(path).write_text(header(comment) + body + '\n', encoding='utf-8')


def patient():
    t = ET.parse(ROOT / 'forms/patientform.ui')
    root = t.getroot()
    tabs = find_widget(t, 'tabWidget')
    # Pflege/Ausweis/Vorsorge entfällt
    tabs.remove(find_widget(t, 'careTab'))
    # Kontakte (Ärzte, Angehörige …) auf eine eigene Seite hinter „Person“
    par = parents(t)
    extra = find_widget(t, 'extraGroup')
    item = par[extra]
    par[item].remove(item)
    ctab = ET.Element('widget', {'class': 'QWidget', 'name': 'contactsTab'})
    a = ET.SubElement(ctab, 'attribute', {'name': 'title'})
    ET.SubElement(a, 'string').text = 'Ärzte && Angehörige'
    lay = ET.SubElement(ctab, 'layout', {'class': 'QVBoxLayout', 'name': 'contactsTabLayout'})
    lay.append(item)
    set_prop(extra, 'title', 'Ärzte, Angehörige und weitere Kontakte')
    children = list(tabs)
    pos = children.index(find_widget(t, 'personTab')) + 1
    tabs.insert(pos, ctab)
    # Körper & Aufnahme: nur Gliedmaßen (BMI) und Vitalwerte bei Aufnahme
    for n in ('bioSexGroup', 'peakFlowGroup', 'targetsRow'):
        remove(t, n)
    set_title(t, 'bodyTab', 'Aufnahme')
    for p in root.iter('property'):
        if p.get('name') == 'formVersion':
            p.find('number').text = '1'
    write(t, ROOT / 'forms/light/patientform.ui',
          '  VitaParms light – Stammblatt: Person, Ärzte und Angehörige, Diagnosen, Medikamente, Aufnahme.')


def vital():
    t = ET.parse(ROOT / 'forms/vitalform.ui')
    root = t.getroot()
    remove_form_rows(t, 'valuesForm', ['peakFlowSpin', 'inrSpin'])
    # Schmerzstärke als letzte Zeile der Vitalwerte, rechte Spalte entfällt (einspaltig, übersichtlich)
    add = find_widget(t, 'additionalForm')
    vals = find_widget(t, 'valuesForm')
    last = max(int(i.get('row')) for i in vals if i.tag == 'item')
    for item in list(add):
        if item.tag != 'item':
            continue
        names = {e.get('name') for e in item.iter() if e.get('name')}
        if names & {'painLabel', 'painSpin'}:
            add.remove(item)
            item.set('row', str(last + 1))
            vals.append(item)
    remove(t, 'rightColumn')
    for p in root.iter('property'):
        if p.get('name') == 'formVersion':
            p.find('number').text = '1'
    left = {e.get('name') for e in t.iter() if e.get('name')}
    for n in ('peakFlowSpin', 'inrSpin', 'fluidInSpin', 'stoolCombo', 'edemaCombo', 'respRateSpin', 'additionalGroup'):
        if n in left:
            raise SystemExit(f'{n} noch vorhanden')
    write(t, ROOT / 'forms/light/vitalform.ui',
          '  VitaParms light – Messung: Blutdruck, Puls, Temperatur, SpO₂, Blutzucker, Gewicht, Größe, BMI, Schmerz.')


if __name__ == '__main__':
    patient()
    vital()
    print('forms/light/patientform.ui, forms/light/vitalform.ui erzeugt')
