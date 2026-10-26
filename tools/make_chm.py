#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
VitaParms – Windows-Hilfedatei (.chm) aus dem LaTeX-Handbuch erzeugen.

Eine CHM-Datei ist kompiliertes HTML (Microsoft HTML Help). Das Skript setzt dieselben
LaTeX-Quellen, aus denen das PDF entsteht, in HTML um – ein Kapitel je Seite, mit
Inhaltsverzeichnis, Stichwortverzeichnis (aus den \\index-Einträgen) und Volltextsuche –
und kompiliert das Ergebnis.

Aufruf:
    python3 tools/make_chm.py ../VitaParms-Handbuch            → docs/VitaParms-Handbuch.chm
    python3 tools/make_chm.py ../VitaParms-Manual              → docs/VitaParms-Manual.chm
    python3 tools/make_chm.py ../VitaParms-ErsteSchritte       → docs/VitaParms-ErsteSchritte.chm
    python3 tools/make_chm.py ../VitaParms-GettingStarted      → docs/VitaParms-GettingStarted.chm
    python3 tools/make_chm.py <Handbuchordner> -o <Datei.chm> [--keep]

Benötigt:
    pandoc (≥ 2.19)       https://pandoc.org
    pdflatex + pdftoppm   für die TikZ-Abbildungen (TeX Live/MiKTeX, Poppler)
    CHM-Compiler:         chmcmd (Free Pascal, Linux/macOS: Paket fp-utils)
                          oder hhc.exe (Microsoft HTML Help Workshop, Windows)
"""
import argparse
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# ----------------------------------------------------------------------------------------------
#  Sprachabhängige Texte
# ----------------------------------------------------------------------------------------------
LANG = {
    'de': dict(name='VitaParms-Handbuch', title='VitaParms – Handbuch', lcid='0x407 German (Germany)',
               fig='Abb.', tab='Tabelle', chapter='Kapitel', appendix='Anhang', contents='Inhalt',
               start='Startseite', q=('„', '“'), prev='Zurück', next='Weiter', top='Inhalt',
               intro='Benutzerhandbuch', version='Version', see='siehe',
               hint='Drücken Sie in VitaParms jederzeit <kbd>F1</kbd>, um diese Hilfe zu öffnen.'),
    'en': dict(name='VitaParms-Manual', title='VitaParms – User Manual', lcid='0x809 English (United Kingdom)',
               fig='Fig.', tab='Table', chapter='Chapter', appendix='Appendix', contents='Contents',
               start='Start page', q=('“', '”'), prev='Previous', next='Next', top='Contents',
               intro='User manual', version='Version', see='see',
               hint='Press <kbd>F1</kbd> in VitaParms at any time to open this help.'),
}

# Kurzanleitung „Erste Schritte“ / „Getting Started“
GUIDE = {
    'de': dict(name='VitaParms-ErsteSchritte', title='VitaParms – Erste Schritte', intro='Kurzanleitung mit Beispielen',
               hint='In VitaParms öffnen Sie diese Kurzanleitung unter <i>Hilfe → Erste Schritte</i>, '
                    'das ausführliche Handbuch mit <kbd>F1</kbd>.'),
    'en': dict(name='VitaParms-GettingStarted', title='VitaParms – Getting Started', intro='Quick guide with examples',
               hint='In VitaParms, open this quick guide via <i>Help → Getting Started</i>, '
                    'and the full user manual with <kbd>F1</kbd>.'),
}

PU = '\uE000'          # Markierungen im Text, die nach pandoc durch HTML ersetzt werden
def mk(tag):           # ⟨tag⟩ … ⟨/⟩
    return f'{PU}{tag}{PU}'
END = mk('/')


def die(msg):
    sys.exit('make_chm: ' + msg)


# ----------------------------------------------------------------------------------------------
#  Hilfsfunktionen für LaTeX-Argumente
# ----------------------------------------------------------------------------------------------
def group(s, i, open_='{', close='}'):
    """s[i] == open_ → (Inhalt, Index nach der schließenden Klammer)."""
    assert s[i] == open_, (s[i:i + 40])
    depth, j = 0, i
    while j < len(s):
        c = s[j]
        if c == '\\':
            j += 2
            continue
        if c == open_:
            depth += 1
        elif c == close:
            depth -= 1
            if depth == 0:
                return s[i + 1:j], j + 1
        j += 1
    die('Klammer nicht geschlossen: ' + s[i:i + 60])


def skip_ws(s, i):
    while i < len(s) and s[i] in ' \t\n':
        i += 1
    return i


def replace_macro(s, name, nargs, func, opt=False, star=False):
    """Ersetzt \\name[opt]{a1}…{an} durch func(opt, *args)."""
    pat = re.compile(r'\\' + re.escape(name) + (r'\*?' if star else '') + r'(?![A-Za-z])')
    out, pos = [], 0
    while True:
        m = pat.search(s, pos)
        if not m:
            break
        i = m.end()
        o = None
        if opt:
            j = skip_ws(s, i)
            if j < len(s) and s[j] == '[':
                o, i = group(s, j, '[', ']')
        args = []
        for _ in range(nargs):
            i = skip_ws(s, i)
            if i >= len(s) or s[i] != '{':
                die(f'Argument fehlt: \\{name} … {s[m.start():m.start() + 60]!r}')
            a, i = group(s, i)
            args.append(a)
        out.append(s[pos:m.start()])
        out.append(func(o, *args) if opt else func(*args))
        pos = i
    out.append(s[pos:])
    return ''.join(out)


def replace_env(s, name, func):
    """Ersetzt \\begin{name}[opt]…\\end{name} durch func(opt, body)."""
    pat = re.compile(r'\\begin\{' + re.escape(name) + r'\}')
    out, pos = [], 0
    while True:
        m = pat.search(s, pos)
        if not m:
            out.append(s[pos:])
            return ''.join(out)
        i = m.end()
        o = None
        if i < len(s) and s[i] == '[':
            o, i = group(s, i, '[', ']')
        e = s.index('\\end{' + name + '}', i)
        out.append(s[pos:m.start()])
        out.append(func(o, s[i:e]))
        pos = e + len('\\end{' + name + '}')


# ----------------------------------------------------------------------------------------------
#  1. Quellen einlesen
# ----------------------------------------------------------------------------------------------
SKIP_INPUTS = re.compile(r'(schmutztitel|halftitle|titelseite|titlepage|impressum|imprint|rueckseite|backcover)')


def strip_comments(t):
    return re.sub(r'(?<!\\)%.*', '', t)


def read_document(src: Path, main: Path):
    text = strip_comments(main.read_text(encoding='utf-8'))
    body = text[text.index('\\begin{document}') + len('\\begin{document}'):text.index('\\end{document}')]

    def expand(t):
        def inp(m):
            name = m.group(1)
            if SKIP_INPUTS.search(name):
                return ''
            p = src / (name if name.endswith('.tex') else name + '.tex')
            return expand(strip_comments(p.read_text(encoding='utf-8')))
        return re.sub(r'\\input\{([^}]+)\}', inp, t)
    body = expand(body)
    # \ifxyz … \fi-Schalter der Titelei
    body = re.sub(r'\\if[a-z]+\s*(.*?)\\fi', r'\1', body)
    return body


def drop_newcommands(body, keep=('Markierung', 'MarkierungOben')):
    """Layout-Makros, die Kapitel lokal definieren (z. B. \\Frage), aus dem Text entfernen."""
    out, pos = [], 0
    for m in re.finditer(r'\\(?:re)?newcommand\*?\{\\(\w+)\}(\[\d\])?(\[[^\]]*\])?\{', body):
        if m.start() < pos or m.group(1) in keep:
            continue
        _, e = group(body, m.end() - 1)
        out.append(body[pos:m.start()])
        pos = e
    out.append(body[pos:])
    return ''.join(out)


# ----------------------------------------------------------------------------------------------
#  2. TikZ-Abbildungen als Bild setzen
# ----------------------------------------------------------------------------------------------
def render_tikz(body, src: Path, preamble: str, work: Path, babel: str):
    colors = '\n'.join(re.findall(r'\\definecolor\{[^}]+\}\{[^}]+\}\{[^}]+\}', preamble))
    libs = re.search(r'\\usetikzlibrary\{([^}]+)\}', preamble).group(1)
    gpath = re.search(r'\\graphicspath\{\{([^}]+)\}\}', preamble).group(1)
    defs = []
    for m in re.finditer(r'\\newcommand\*?\{\\(Markierung\w*)\}\[\d\]\{', body):
        _, e = group(body, m.end() - 1)
        defs.append(body[m.start():e])
    body = re.sub(r'\\newcommand\*?\{\\Markierung\w*\}\[\d\]\{', '\\\\VPDROPDEF{', body)
    body = replace_macro(body, 'VPDROPDEF', 1, lambda a: '')
    n = 0

    def tikz(m, scale=True, cls='tikz'):
        nonlocal n
        n += 1
        code = m.group(0).replace('\\textwidth', '16cm')
        tex = work / f'tikz{n}.tex'
        tex.write_text('\n'.join([
            r'\documentclass[border=6pt]{standalone}',
            r'\usepackage[T1]{fontenc}\usepackage[utf8]{inputenc}',
            r'\usepackage[' + babel + ']{babel}',
            r'\usepackage[sc,osf]{mathpazo}\usepackage[scaled=.90]{helvet}',
            r'\usepackage[table]{xcolor}\usepackage{graphicx}\usepackage{amsmath,amssymb}',
            r'\graphicspath{{' + str((src / gpath).resolve()) + '/}}',
            r'\usepackage{tikz}\usetikzlibrary{' + libs + '}',
            colors, *defs,
            r'\begin{document}', code, r'\end{document}']), encoding='utf-8')
        r = subprocess.run(['pdflatex', '-interaction=nonstopmode', '-halt-on-error', tex.name],
                           cwd=work, capture_output=True, text=True)
        if r.returncode:
            die(f'TikZ-Abbildung {n} ließ sich nicht setzen:\n' + r.stdout[-2000:])
        size = ['-scale-to-x', '1300', '-scale-to-y', '-1'] if scale else ['-r', '130']
        subprocess.run(['pdftoppm', '-png', '-singlefile', *size, f'tikz{n}.pdf', f'tikz{n}'], cwd=work, check=True)
        if not scale:   # Formel: eigener Absatz, natürliche Größe
            return '\n\n' + mk('math') + r'\includegraphics{VPGEN/tikz%d.png}' % n + END + '\n\n'
        return r'\includegraphics[width=\textwidth]{VPGEN/tikz%d.png}' % n
    body = re.sub(r'\\begin\{tikzpicture\}.*?\\end\{tikzpicture\}', tikz, body, flags=re.S)
    # Abgesetzte Formeln (\\[ … \\]) ebenfalls als Bild – die Windows-Hilfe kennt kein MathML
    body = re.sub(r'\\\[(.*?)\\\]', lambda m: tikz(re.match(r'(?s).*', r'\begin{tikzpicture}\node{$\displaystyle ' + m.group(1).strip() + r'$};\end{tikzpicture}'), scale=False), body, flags=re.S)
    return body, n


# ----------------------------------------------------------------------------------------------
#  3. Nummerierung (Kapitel, Abschnitte, Abbildungen, Tabellen) und Verweisziele
# ----------------------------------------------------------------------------------------------
TOKEN = re.compile(r'\\(frontmatter|mainmatter|appendix|backmatter)\b'
                   r'|\\(chapter|section|subsection)(\*?)(?=\s*[\[{])'
                   r'|\\begin\{(figure|table)\}|\\(Bild|BildHier|BildGross)\b|\\captionof\{figure\}'
                   r'|\\label\{([^}]+)\}')


def number_things(body, L):
    labels, mode, ch, sec, sub, fig, tab = {}, 'front', 0, 0, 0, 0, 0
    last = {'sec': '', 'fig': '', 'tab': ''}
    marks = []                        # (pos, Art, Nummer) für das spätere Einfügen
    for m in TOKEN.finditer(body):
        if m.group(1):
            mode = {'frontmatter': 'front', 'mainmatter': 'main', 'appendix': 'app', 'backmatter': 'back'}[m.group(1)]
            if mode == 'app':
                ch = 0
        elif m.group(2):
            kind, starred = m.group(2), bool(m.group(3))
            if starred or mode in ('front', 'back'):
                continue
            if kind == 'chapter':
                ch += 1; sec = sub = fig = tab = 0
                num = chr(64 + ch) if mode == 'app' else str(ch)
            elif kind == 'section':
                sec += 1; sub = 0
                num = f'{last["chap"]}.{sec}'
            else:
                sub += 1
                num = f'{last["chap"]}.{sec}.{sub}'
            if kind == 'chapter':
                last['chap'] = num
            last['sec'] = num
            marks.append((m.start(), 'head', num))
        elif m.group(4) == 'figure' or m.group(5) or m.group(0).startswith('\\captionof'):
            fig += 1
            last['fig'] = f'{last.get("chap", "")}.{fig}' if mode in ('main', 'app') else str(fig)
            marks.append((m.start(), 'fig', last['fig']))
            if m.group(5):            # \Bild…{datei}{text}{label}: Label steckt im 3. Argument
                i = m.end()
                j = skip_ws(body, i)
                if body[j] == '[':
                    _, j = group(body, j, '[', ']')
                for k in range(3):
                    j = skip_ws(body, j)
                    a, j = group(body, j)
                labels['fig:' + a] = last['fig']
        elif m.group(4) == 'table':
            tab += 1
            last['tab'] = f'{last.get("chap", "")}.{tab}'
            marks.append((m.start(), 'tab', last['tab']))
        elif m.group(6):
            lab = m.group(6)
            kind = 'fig' if lab.startswith('fig:') else 'tab' if lab.startswith('tab:') else 'sec'
            labels[lab] = last[kind]
    return labels, marks


# ----------------------------------------------------------------------------------------------
#  4. LaTeX für pandoc vereinfachen
# ----------------------------------------------------------------------------------------------
def simplify(body, L, labels, preamble, src: Path, gen_dir: str):
    # Mehrere \\label hintereinander: HTML kennt nur ein id je Element – Verweise auf den ersten umleiten
    alias = {}
    for first, second in re.findall(r'\\label\{([^}]+)\}\s*\\label\{([^}]+)\}', body):
        body = body.replace('\\label{%s}' % second, '', 1)
        alias[second] = first
    # Verweismakros aus der Präambel (\Abb, \Kap, \Abs) ohne Seitenzahl
    for name in ('Abb', 'Kap', 'Abs'):
        d = re.search(r'\\newcommand\*?\{\\' + name + r'\}\[1\]\{(.*)\}\s*$', preamble, re.M).group(1)
        d = re.sub(r'\s*\([^()]*\\pageref\{#1\}\)', '', d)
        body = replace_macro(body, name, 1, lambda a, d=d: d.replace('#1', a))
    body = re.sub(r'\s*\((S\.|p\.)~\\pageref\{[^}]+\}\)', '', body)
    body = re.sub(r'\\pageref\{[^}]+\}', '', body)

    def ref(lab):
        lab = alias.get(lab, lab)
        if lab not in labels:
            print(f'  Warnung: Verweisziel {lab} unbekannt', file=sys.stderr)
        return r'\hyperref[%s]{%s}' % (lab, labels.get(lab, '?'))
    body = replace_macro(body, 'ref', 1, ref)

    # Stichwörter → Anker
    index = []

    def idx(term):
        index.append(term)
        return mk(f'idx{len(index) - 1}')
    body = replace_macro(body, 'index', 1, idx)

    return body, index


def expand_figures(body, preamble, src: Path, gen_dir: str):
    """\\Bild, \\BildHier, \\Rahmenbild → figure/includegraphics (vor der Nummerierung)."""
    gpath = re.search(r'\\graphicspath\{\{([^}]+)\}\}', preamble).group(1).rstrip('/')

    def img(width, f):
        w = width or r'\textwidth'
        p = f if f.startswith('VPGEN/') else f'{gpath}/{f}'
        if not Path(p).suffix:
            p += '.png' if (src / (p + '.png')).exists() else '.jpg'
        p = p.replace('VPGEN/', gen_dir + '/')
        return r'\includegraphics[width=%s]{%s}' % (w, p)

    def bild(opt, f, cap, lab):
        return ('\n\\begin{figure}\n' + img(opt, f) + '\n\\caption{' + cap + '}\\label{fig:' + lab + '}\n\\end{figure}\n')
    body = replace_macro(body, 'Bild', 3, bild, opt=True)
    body = replace_macro(body, 'BildHier', 3, bild, opt=True)
    body = replace_macro(body, 'BildGross', 3, bild, opt=True)
    body = replace_macro(body, 'RahmenbildGross', 2, lambda w, f: img(w, f))
    body = replace_macro(body, 'Rahmenbild', 2, lambda w, f: img(w, f))
    body = re.sub(r'\\includegraphics\[([^\]]*)\]\{(?!VPGEN|' + re.escape(gpath) + r'|' + re.escape(gen_dir) + r')([^}]+)\}',
                  lambda m: img(re.search(r'width=([^,\]]+)', m.group(1)).group(1) if 'width' in m.group(1) else None, m.group(2)), body)
    body = body.replace('VPGEN/', gen_dir + '/')
    body = replace_macro(body, 'raisebox', 2, lambda a, b: b)
    return body


def simplify_rest(body, L, preamble):
    q1, q2 = L['q']

    # Tabellenköpfe vereinfachen
    def colspec(spec):
        spec = re.sub(r'[@<>!]\{(?:[^{}]|\{[^{}]*\})*\}', '', spec)
        spec = re.sub(r'[pmb]\{[^}]*\}', 'l', spec)
        return re.sub(r'[^lcr]', lambda m: 'l' if m.group(0) in 'LXY' else '', spec)
    body = re.sub(r'\\begin\{tabularx\}\{[^}]*\}', r'\\begin{tabular}', body)
    body = body.replace('\\end{tabularx}', '\\end{tabular}')
    body = re.sub(r'\\begin\{(tabular|longtable)\}\{((?:[^{}]|\{(?:[^{}]|\{[^{}]*\})*\})*)\}',
                  lambda m: '\\begin{%s}{%s}' % (m.group(1), colspec(m.group(2))), body)
    body = replace_macro(body, 'multicolumn', 3, lambda n, s, t: t + ' &' * (int(n) - 1))
    body = replace_macro(body, 'rowcolor', 1, lambda c: '')

    # Kästen
    for env, default in re.findall(r'\\newtcolorbox\{(\w+)\}\[1\]\[([^\]]*)\]', preamble):
        def box(o, b, env=env, default=default):
            t = o or default
            colon = '' if t.rstrip().endswith(('?', ':', '…')) else ':'
            return '\\begin{%s}' % env + PU + 'box-title' + PU + t + colon + END + ' ' + b + '\\end{%s}' % env
        body = replace_env(body, env, box)
    body = replace_env(body, 'tcolorbox', lambda o, b: '\\begin{licensebox}' + b + '\\end{licensebox}')
    body = replace_env(body, 'schritte', lambda o, b: '\\begin{schritte}\\begin{enumerate}' + b + '\\end{enumerate}\\end{schritte}')
    for env in ('center', 'flushright', 'minipage', 'scope'):
        body = re.sub(r'\\(begin|end)\{' + env + r'\}(\[[^\]]*\])?(\{[^}]*\})?', '', body)

    # Inline-Auszeichnungen
    body = replace_macro(body, 'Knopf', 1, lambda a: mk('knopf') + a + END)
    body = replace_macro(body, 'Feld', 1, lambda a: mk('feld') + a + END)
    body = replace_macro(body, 'Reiter', 1, lambda a: mk('reiter') + a + END)
    body = replace_macro(body, 'Datei', 1, lambda a: mk('datei') + a + END)
    body = replace_macro(body, 'schrittnr', 1, lambda a: mk('nr') + a + END)
    body = replace_macro(body, 'Nr', 1, lambda a: mk('nrm') + a + END)   # Markierung im Bildschirmfoto
    body = replace_macro(body, 'Taste', 1, lambda a: '+'.join(mk('kbd') + k + END for k in a.split('+')))
    body = replace_macro(body, 'Menue', 1, lambda a: mk('menu') + (' ' + mk('arr') + ' ').join(
        mk('mi') + x.strip() + END for x in a.split(',')) + END)
    body = replace_macro(body, 'enquote', 1, lambda a: q1 + a + q2)
    body = replace_macro(body, 'Frage', 1, lambda a: '\n\\paragraph{' + a + '}\n')
    body = replace_macro(body, 'footnote', 1, lambda a: ' (' + a + ')')
    body = replace_macro(body, 'textsf', 1, lambda a: a)
    body = replace_macro(body, 'color', 1, lambda a: '')
    body = re.sub(r'\\VP(\{\})?', 'VitaParms', body)
    body = re.sub(r'\\Version(\{\})?(?![A-Za-z])', L['version_str'], body)
    body = re.sub(r'\\(addcontentsline\{[^}]*\}\{[^}]*\}|markboth)\{[^}]*\}(\{[^}]*\})?', '', body)
    body = re.sub(r'\\(sffamily|bfseries|mdseries|itshape|ttfamily|small|footnotesize|Large|large|normalsize'
                  r'|medskip|bigskip|smallskip|nopagebreak|noindent|raggedright|centering|hfill|par)\b(?![A-Za-z])\s?',
                  lambda m: '\n\n' if m.group(1) == 'par' else ' ' if m.group(1) == 'hfill' else '', body)
    body = re.sub(r'\\vspace\*?\{[^}]*\}', '', body)
    body = re.sub(r'\\(frontmatter|mainmatter|appendix|backmatter|printindex|tableofcontents|clearpage|cleardoublepage)\b', '', body)
    return body


def number_headings(body, labels):
    """Nummern in die Überschriften schreiben (pandoc nummeriert Anhänge nicht als A, B …)."""
    out, pos, mode, ch, sec, sub = [], 0, 'front', 0, 0, 0
    pat = re.compile(r'\\(frontmatter|mainmatter|appendix|backmatter)\b|\\(chapter|section|subsection)(\*?)\s*\{')
    chap = ''
    for m in pat.finditer(body):
        if m.group(1):
            mode = {'frontmatter': 'front', 'mainmatter': 'main', 'appendix': 'app', 'backmatter': 'back'}[m.group(1)]
            if mode == 'app':
                ch = 0
            continue
        if m.group(3) or mode in ('front', 'back'):
            continue
        if m.group(2) == 'chapter':
            ch += 1; sec = sub = 0
            chap = num = chr(64 + ch) if mode == 'app' else str(ch)
        elif m.group(2) == 'section':
            sec += 1; sub = 0; num = f'{chap}.{sec}'
        else:
            sub += 1; num = f'{chap}.{sec}.{sub}'
        out.append(body[pos:m.end()])
        out.append(mk('num') + num + END + ' ')
        pos = m.end()
    out.append(body[pos:])
    return ''.join(out)


def number_captions(body, L):
    """„Abb. 8.5:“ bzw. „Tabelle 4.1:“ vor die Bildunterschriften setzen."""
    mode, ch, fig, tab, chap = 'front', 0, 0, 0, ''
    out, pos = [], 0
    pat = re.compile(r'\\(frontmatter|mainmatter|appendix|backmatter)\b|\\chapter(\*?)\s*\{|\\begin\{(figure|table)\}|\\caption\{')
    env = 'figure'
    for m in pat.finditer(body):
        if m.group(1):
            mode = {'frontmatter': 'front', 'mainmatter': 'main', 'appendix': 'app', 'backmatter': 'back'}[m.group(1)]
            if mode == 'app':
                ch = 0
        elif m.group(0).startswith('\\chapter'):
            if not m.group(2) and mode in ('main', 'app'):
                ch += 1; fig = tab = 0
                chap = chr(64 + ch) if mode == 'app' else str(ch)
        elif m.group(3):
            env = m.group(3)
        else:
            if env == 'figure':
                fig += 1; label = f'{L["fig"]} {chap}.{fig}'
            else:
                tab += 1; label = f'{L["tab"]} {chap}.{tab}'
            out.append(body[pos:m.end()])
            out.append(mk('capnum') + label + ':' + END + ' ')
            pos = m.end()
    out.append(body[pos:])
    return ''.join(out)


# ----------------------------------------------------------------------------------------------
#  5. HTML nachbearbeiten, aufteilen, CHM-Projekt schreiben
# ----------------------------------------------------------------------------------------------
CSS = """
body { font-family: "Segoe UI", Tahoma, Arial, sans-serif; font-size: 10pt; color: #1d252c;
       margin: 0; padding: 14px 22px 30px; background: #fff; line-height: 1.45; max-width: 980px; }
h1 { font-size: 19pt; color: #1F78B9; margin: 4px 0 16px; padding-bottom: 6px; border-bottom: 2px solid #D9601A; }
h1 .num { color: #D9601A; margin-right: .35em; }
h2 { font-size: 13.5pt; color: #1F78B9; margin: 24px 0 8px; }
h3 { font-size: 11.5pt; color: #1F78B9; margin: 18px 0 6px; }
h4 { font-size: 10.5pt; color: #1F78B9; margin: 14px 0 4px; }
h2 .num, h3 .num { color: #5A6570; margin-right: .4em; font-weight: normal; }
p { margin: 6px 0 8px; }
a { color: #1F78B9; } a:hover { color: #D9601A; }
figure { margin: 14px 0 18px; text-align: center; }
figure img { max-width: 100%; height: auto; border: 1px solid #c8ced4; }
figcaption { font-size: 9pt; color: #5A6570; margin-top: 5px; }
.capnum { color: #1F78B9; font-weight: bold; }
table { border-collapse: collapse; margin: 10px 0 14px; font-size: 9.5pt; }
th, td { border-bottom: 1px solid #d5dbe0; padding: 4px 10px 4px 0; vertical-align: top; text-align: left; }
thead th { border-bottom: 2px solid #5A6570; }
td:first-child { font-weight: 600; white-space: nowrap; }
.knopf { display: inline-block; border: 1px solid #a9b1b8; background: #f3f4f5; border-radius: 3px;
         padding: 0 5px; font-size: 9pt; white-space: nowrap; }
kbd { display: inline-block; border: 1px solid #a9b1b8; border-bottom-width: 2px; background: #fafafa;
      border-radius: 3px; padding: 0 4px; font-family: inherit; font-size: 9pt; white-space: nowrap; }
.feld { font-style: italic; }
.reiter { font-weight: bold; color: #1F78B9; }
.datei { font-family: Consolas, "Courier New", monospace; font-size: 9.5pt; }
.menu { white-space: nowrap; }
.mi { display: inline-block; background: #eaf3fa; border: 1px solid #b9d4ea; border-radius: 3px; padding: 0 5px; font-size: 9pt; }
.arr { color: #5A6570; }
.nr { display: inline-block; width: 1.5em; height: 1.5em; line-height: 1.5em; border-radius: .75em;
      background: #1F78B9; color: #fff; font-weight: bold; font-size: 8.5pt; text-align: center; }
.nrm { background: #E06C00; }
.schritte ol { list-style: none; padding-left: 0; counter-reset: st; }
.schritte ol > li { position: relative; padding-left: 2.3em; margin: 7px 0; counter-increment: st; }
.schritte ol > li:before { content: counter(st); position: absolute; left: 0; top: 0; width: 1.6em; height: 1.6em;
      line-height: 1.6em; border-radius: .8em; background: #1F78B9; color: #fff; font-weight: bold;
      font-size: 8.5pt; text-align: center; }
dl dt { font-weight: bold; margin-top: 6px; } dl dd { margin: 0 0 4px 1.6em; }
.ziel { border-left: 4px solid #1F78B9; background: #EAF3FA; padding: 6px 12px; margin: 10px 0; }
.wenn { border-left: 4px solid #D9601A; background: #FFF4D6; padding: 6px 12px; margin: 12px 0; }
.hinweis, .tipp, .achtung, .rechtlich { border-left: 4px solid #1F78B9; background: #EAF3FA; padding: 6px 12px; margin: 12px 0; }
.tipp { border-color: #2E8B57; background: #f0f8f3; }
.achtung { border-color: #C0392B; background: #fcf0ee; }
.rechtlich { border-color: #D9601A; background: #FFF4D6; }
.box-title { font-weight: bold; }
.licensebox { font-family: Consolas, "Courier New", monospace; font-size: 8.5pt; background: #f7f7f7;
              border: 1px solid #ccc; padding: 4px 10px; }
.nav { font-size: 9pt; color: #5A6570; border-top: 1px solid #d5dbe0; margin-top: 28px; padding-top: 6px; }
.nav a { margin-right: 14px; }
.toc ul { list-style: none; padding-left: 1.2em; } .toc > ul { padding-left: 0; }
.toc li { margin: 3px 0; }
.formula { display: block; text-align: center; margin: 10px 0; }
.formula img { border: 0; }
.start { text-align: center; margin-top: 30px; }
.start h1 { border: 0; font-size: 26pt; }
.start .sub { font-size: 13pt; color: #5A6570; }
"""

MARK_HTML = {
    'knopf': '<span class="knopf">', 'feld': '<span class="feld">', 'reiter': '<span class="reiter">',
    'datei': '<span class="datei">', 'nr': '<span class="nr">', 'nrm': '<span class="nr nrm">', 'kbd': '<kbd>', 'menu': '<span class="menu">',
    'mi': '<span class="mi">', 'num': '<span class="num">', 'capnum': '<span class="capnum">',
    'box-title': '<span class="box-title">', 'math': '<span class="formula">',
}


def postprocess(htm):
    # Öffnende Markierungen → Tags; schließende passend zur zuletzt geöffneten
    stack, out, pos = [], [], 0
    for m in re.finditer(PU + r'([^' + PU + r']+)' + PU, htm):
        out.append(htm[pos:m.start()])
        tag = m.group(1)
        if tag == '/':
            t = stack.pop() if stack else 'span'
            out.append('</kbd>' if t == 'kbd' else '</span>')
        elif tag == 'arr':
            out.append('<span class="arr">&#9656;</span>')
        elif tag.startswith('idx'):
            out.append(f'<a name="{tag}" id="{tag}"></a>')
        else:
            stack.append(tag)
            out.append(MARK_HTML[tag])
        pos = m.end()
    out.append(htm[pos:])
    htm = ''.join(out)
    htm = re.sub(r'<p>\s*((?:<a name="idx\d+" id="idx\d+"></a>\s*)+)</p>', r'\1', htm)
    return htm


def page(title, body, L, nav=''):
    return ('<!DOCTYPE html>\n<html lang="%s">\n<head>\n<meta http-equiv="Content-Type" content="text/html; charset=utf-8">\n'
            '<meta http-equiv="X-UA-Compatible" content="IE=edge">\n<title>%s</title>\n'
            '<link rel="stylesheet" href="vitaparms.css">\n</head>\n<body>\n%s\n%s\n</body>\n</html>\n'
            % (L['code'], html.escape(title), body, nav))


def text_of(h):
    return html.unescape(re.sub(r'<[^>]+>', '', h)).strip()


def cp1252(s):
    """HTML Help liest Inhalts-/Stichwortdateien in der Codepage des Projekts."""
    return html.escape(s, quote=True).encode('cp1252', 'xmlcharrefreplace')


def sitemap_obj(name, local=None, see=None):
    p = [f'<param name="Name" value="{name}">']
    if local:
        p.append(f'<param name="Local" value="{local}">')
    if see:
        p.append(f'<param name="See Also" value="{see}">')
    return '<LI><OBJECT type="text/sitemap">' + ''.join(p) + '</OBJECT>\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source', help='Ordner des LaTeX-Handbuchs bzw. der Kurzanleitung (handbuch.tex, manual.tex, '
                                   'erste-schritte.tex oder getting-started.tex)')
    ap.add_argument('-o', '--output', help='Ziel (.chm); Vorgabe: docs/<Name>.chm neben diesem Skript')
    ap.add_argument('--keep', action='store_true', help='HTML-Projektordner behalten (neben der .chm-Datei)')
    a = ap.parse_args()

    src = Path(a.source).resolve()
    main_tex = next((src / n for n in ('handbuch.tex', 'manual.tex', 'erste-schritte.tex', 'getting-started.tex')
                     if (src / n).exists()), None)
    if not main_tex:
        die(f'{src}: weder handbuch.tex, manual.tex, erste-schritte.tex noch getting-started.tex gefunden')
    pre_name = re.search(r'\\input\{(praeambel|preamble)\}', main_tex.read_text(encoding='utf-8')).group(1)
    preamble = strip_comments((src / (pre_name + '.tex')).read_text(encoding='utf-8'))
    babel = re.search(r'\\usepackage\[([^\]]+)\]\{babel\}', preamble).group(1)
    lang = 'de' if 'german' in babel else 'en'
    L = dict(LANG[lang], code=lang)
    if main_tex.name in ('erste-schritte.tex', 'getting-started.tex'):   # Kurzanleitung
        L.update(GUIDE[lang])
    version = re.search(r'\\newcommand\*?\{\\Version\}\{([^}]+)\}', preamble).group(1)
    L['version_str'] = version
    gpath = re.search(r'\\graphicspath\{\{([^}]+)\}\}', preamble).group(1).rstrip('/')
    out = Path(a.output) if a.output else Path(__file__).resolve().parent.parent / 'docs' / (L['name'] + '.chm')

    for tool in ('pandoc', 'pdflatex', 'pdftoppm'):
        if not shutil.which(tool):
            die(f'{tool} nicht gefunden')
    compiler = shutil.which('chmcmd') or shutil.which('hhc') or shutil.which('hhc.exe')
    for p in (r'C:\Program Files (x86)\HTML Help Workshop\hhc.exe', r'C:\Program Files\HTML Help Workshop\hhc.exe'):
        if not compiler and os.path.exists(p):
            compiler = p
    if not compiler:
        die('Kein CHM-Compiler gefunden (chmcmd aus fp-utils oder hhc.exe aus dem HTML Help Workshop)')

    tmp = Path(tempfile.mkdtemp(prefix='vpchm-'))
    proj = tmp / L['name']
    gen = tmp / 'gen'
    proj.mkdir(); gen.mkdir()
    print(f'{L["name"]} {version} ({lang}) → {out}')

    body = drop_newcommands(read_document(src, main_tex))
    body = replace_macro(body, 'IfFileExists', 3, lambda f, yes, no: yes if (src / f).exists() else no)
    body, ntikz = render_tikz(body, src, preamble, gen, babel)
    body = expand_figures(body, preamble, src, 'gen')
    labels, _ = number_things(body, L)
    body = number_captions(body, L)
    body = number_headings(body, labels)
    body, index = simplify(body, L, labels, preamble, src, 'gen')
    body = simplify_rest(body, L, preamble)

    texfile = tmp / 'doc.tex'
    texfile.write_text('\\documentclass{book}\n\\begin{document}\n' + body + '\n\\end{document}\n', encoding='utf-8')
    r = subprocess.run(['pandoc', str(texfile), '-f', 'latex', '-t', 'html5', '--top-level-division=chapter',
                        '--wrap=none', '-M', 'lang=' + lang], capture_output=True, text=True, cwd=src)
    if r.returncode:
        die('pandoc: ' + r.stderr)
    for line in r.stderr.splitlines():
        if 'Could not convert' in line or 'Skipped' in line:
            print('  pandoc: ' + line, file=sys.stderr)
    htm = postprocess(r.stdout)
    if PU in htm:
        die('nicht aufgelöste Markierung im HTML')

    # Bilder kopieren und Pfade auf den Projektordner umstellen
    (proj / 'img').mkdir()
    def fix_img(m):
        p = m.group(1)
        f = (gen / Path(p).name) if p.startswith('gen/') else (src / p)
        if not f.exists():
            die(f'Bild fehlt: {p}')
        shutil.copy(f, proj / 'img' / f.name)
        style = m.group(2) or ''
        return f'<img src="img/{f.name}"{style} alt="">'
    htm = re.sub(r'<img src="([^"]+)"( style="[^"]*")?[^>]*>', fix_img, htm)
    # Bildbreiten (in % der Textbreite) übernehmen, aber nie breiter als das Fenster
    htm = re.sub(r'style="width:([\d.]+)%"', lambda m: f'style="width:{float(m.group(1)):.0f}%"', htm)

    # Sprungziele auf reine ASCII-Namen umstellen: Die Windows-Hilfe deutet einen Doppelpunkt
    # (pandoc: "sec:...", "fig:...") als Protokoll ("http://k04.htm") und verträgt keine Umlaute
    # in Ankern, weil Inhalts- und Stichwortverzeichnis in der ANSI-Codepage gespeichert werden.
    import unicodedata
    id_map, used = {}, set()
    for old in re.findall(r'\bid="([^"]+)"', htm):
        if old in id_map or re.fullmatch(r'idx\d+', old):
            continue
        aid = unicodedata.normalize('NFKD', old.replace('ß', 'ss')).encode('ascii', 'ignore').decode()
        aid = re.sub(r'[^A-Za-z0-9_-]+', '-', aid).strip('-').lower() or 'a'
        if aid[0].isdigit():
            aid = 'a-' + aid
        base, n = aid, 2
        while aid in used:
            aid = f'{base}-{n}'; n += 1
        used.add(aid)
        id_map[old] = aid
    htm = re.sub(r'\bid="([^"]+)"', lambda m: 'id="%s"' % id_map.get(m.group(1), m.group(1)), htm)
    htm = re.sub(r'href="#([^"]+)"', lambda m: 'href="#%s"' % id_map.get(html.unescape(m.group(1)), m.group(1)), htm)

    # In Kapitel aufteilen
    parts = re.split(r'(?=<h1[ >])', htm)
    pre, chapters = parts[0], parts[1:]
    files, toc = [], []
    id_file = {}
    for k, c in enumerate(chapters, 1):
        h1 = re.match(r'<h1[^>]*>(.*?)</h1>', c, re.S)
        title = text_of(h1.group(1))
        fname = f'k{k:02d}.htm'
        files.append((fname, title, c))
        for i in re.findall(r'\bid="([^"]+)"', c):
            id_file[i] = fname
        secs = [(m.group(1), text_of(m.group(2))) for m in re.finditer(r'<h2[^>]*\bid="([^"]+)"[^>]*>(.*?)</h2>', c, re.S)]
        h1id = re.search(r'\bid="([^"]+)"', h1.group(0))
        toc.append((title, fname, secs))

    def fix_links(c):
        return re.sub(r'href="#([^"]+)"', lambda m: f'href="{id_file.get(m.group(1), "")}#{m.group(1)}"', c)

    for k, (fname, title, c) in enumerate(files):
        nav = ['<div class="nav">']
        if k > 0:
            nav.append(f'<a href="{files[k-1][0]}">&#9664; {L["prev"]}</a>')
        nav.append(f'<a href="index.htm">{L["top"]}</a>')
        if k + 1 < len(files):
            nav.append(f'<a href="{files[k+1][0]}">{L["next"]} &#9654;</a>')
        nav.append('</div>')
        (proj / fname).write_text(page(title, fix_links(c), L, ''.join(nav)), encoding='utf-8')

    # Startseite mit Inhaltsverzeichnis
    tl = ['<div class="toc"><ul>']
    for title, fname, secs in toc:
        tl.append(f'<li><a href="{fname}">{html.escape(title)}</a>')
        if secs:
            tl.append('<ul>' + ''.join(f'<li><a href="{fname}#{i}">{html.escape(t)}</a></li>' for i, t in secs) + '</ul>')
        tl.append('</li>')
    tl.append('</ul></div>')
    shutil.copy(Path(__file__).resolve().parent.parent / 'resources' / 'icons' / 'vitaparms.png', proj / 'img' / 'logo.png')
    start = (f'<div class="start"><img src="img/logo.png" width="96" height="96" alt=""><h1>VitaParms</h1>'
             f'<p class="sub">{L["intro"]} · {L["version"]} {html.escape(version)}</p><p>{L["hint"]}</p></div>'
             f'<h2>{L["contents"]}</h2>' + ''.join(tl))
    (proj / 'index.htm').write_text(page(L['title'], start, L), encoding='utf-8')
    (proj / 'vitaparms.css').write_text(CSS, encoding='utf-8')

    # Inhaltsverzeichnis (.hhc)
    hhc = ['<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">\n<HTML><HEAD></HEAD><BODY>\n'
           '<OBJECT type="text/site properties"><param name="Window Styles" value="0x800025"></OBJECT>\n<UL>\n',
           sitemap_obj(html.escape(L['start']), 'index.htm')]
    for title, fname, secs in toc:
        hhc.append(sitemap_obj(html.escape(title), fname))
        if secs:
            hhc.append('<UL>\n' + ''.join(sitemap_obj(html.escape(t), f'{fname}#{i}') for i, t in secs) + '</UL>\n')
    hhc.append('</UL>\n</BODY></HTML>\n')
    (proj / (L['name'] + '.hhc')).write_bytes(''.join(hhc).encode('cp1252', 'xmlcharrefreplace'))

    # Stichwortverzeichnis (.hhk): Begriff → Fundstellen, „!“ = Unterbegriff, „|see{…}“ = Verweis
    entries = {}
    for n, term in enumerate(index):
        see = None
        m = re.match(r'(.*)\|see\{(.*)\}$', term)
        if m:
            term, see = m.group(1), m.group(2)
        term = text_of(re.sub(r'\\[a-zA-Z]+\s*', '', term.split('@')[-1]))
        keys = tuple(t.strip() for t in term.split('!'))
        anchor = f'idx{n}'
        fname = next((f for f, _, c in files if f'name="{anchor}"' in c), None)
        if not fname:
            continue
        c = next(c for f, _, c in files if f == fname)
        apos = c.index(f'name="{anchor}"')
        heads = [(m.start(), text_of(m.group(1))) for m in re.finditer(r'<h[12][^>]*>(.*?)</h[12]>', c, re.S)]
        chap = [t for p_, t in heads if p_ < apos][-1] if any(p_ < apos for p_, _ in heads) else heads[0][1]
        e = entries.setdefault(keys, {'see': None, 'locs': []})
        if see:
            e['see'] = see
        else:
            e['locs'].append((chap, f'{fname}#{anchor}'))
    hhk = ['<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">\n<HTML><HEAD></HEAD><BODY>\n<UL>\n']
    tops = sorted({k[0] for k in entries}, key=lambda s: s.casefold().replace('ä', 'a').replace('ö', 'o').replace('ü', 'u').replace('ß', 'ss'))

    def obj(name, e):
        p = [f'<param name="Name" value="{html.escape(name)}">']
        if e and e['see']:
            p.append(f'<param name="See Also" value="{html.escape(e["see"])}">')
        elif e:
            seen = set()
            for chap, loc in e['locs']:
                if loc.split('#')[0] in seen:
                    continue
                seen.add(loc.split('#')[0])
                p.append(f'<param name="Name" value="{html.escape(chap)}"><param name="Local" value="{loc}">')
        return '<LI><OBJECT type="text/sitemap">' + ''.join(p) + '</OBJECT>\n'
    for t in tops:
        e = entries.get((t,))
        subs = sorted(k for k in entries if k[0] == t and len(k) > 1)
        if not e and subs:      # Oberbegriff ohne eigene Fundstelle: erste Fundstelle der Unterbegriffe
            e = {'see': None, 'locs': entries[subs[0]]['locs'][:1]}
        hhk.append(obj(t, e))
        if subs:
            hhk.append('<UL>\n' + ''.join(obj(' '.join(k[1:]), entries[k]) for k in subs) + '</UL>\n')
    hhk.append('</UL>\n</BODY></HTML>\n')
    (proj / (L['name'] + '.hhk')).write_bytes(''.join(hhk).encode('cp1252', 'xmlcharrefreplace'))

    # Projektdatei (.hhp)
    allfiles = ['index.htm', 'vitaparms.css'] + [f for f, _, _ in files] + sorted('img/' + p.name for p in (proj / 'img').iterdir())
    name = L['name']
    hhp = f"""[OPTIONS]
Compatibility=1.1 or later
Compiled file={name}.chm
Contents file={name}.hhc
Index file={name}.hhk
Default Window=main
Default topic=index.htm
Display compile progress=No
Full-text search=Yes
Binary TOC=No
Binary Index=Yes
Language={L['lcid']}
Title={L['title']}

[WINDOWS]
main="{L['title']}","{name}.hhc","{name}.hhk","index.htm","index.htm",,,,,0x62520,260,0x10384e,[60,40,1180,860],,,,,,,0

[FILES]
""" + '\n'.join(f.replace('/', '\\') if 'hhc' in compiler.lower() and 'chmcmd' not in compiler else f for f in allfiles) + '\n'
    (proj / (name + '.hhp')).write_bytes(hhp.encode('cp1252', 'xmlcharrefreplace'))

    r = subprocess.run([compiler, name + '.hhp'], cwd=proj, capture_output=True, text=True, errors='replace')
    chm = proj / (name + '.chm')
    if not chm.exists():
        die('CHM-Compiler fehlgeschlagen:\n' + r.stdout + r.stderr)
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(chm, out)
    print(f'  {len(files)} Seiten, {len(entries)} Stichwörter, {ntikz} TikZ-Abbildungen, '
          f'{len(list((proj / "img").iterdir()))} Bilder → {out} ({out.stat().st_size // 1024} KB)')
    if a.keep:
        keep = out.with_suffix('')
        shutil.rmtree(keep, ignore_errors=True)
        shutil.copytree(proj, keep)
        print(f'  HTML-Projekt: {keep}')
    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    main()
