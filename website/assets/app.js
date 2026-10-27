/* VitaParms – Website: entschlüsselt das von VitaParms hochgeladene Datenpaket im Browser
 * und zeigt es schreibgeschützt an. Kein Framework, keine externen Quellen.
 * Format "VPW1": "VPW1" | IV(16) | AES-256-CTR | HMAC-SHA256(32); Schlüssel per PBKDF2-SHA256.
 * SPDX-License-Identifier: MIT */
'use strict';

(() => {
  // ================================================================ Texte
  const TEXT = {
    de: {
      locale: 'de-DE',
      loginIntro: 'Diese Seite zeigt Gesundheitsdaten, die mit VitaParms erfasst wurden. Sie sind verschlüsselt und werden erst nach Eingabe des Passworts in diesem Browser lesbar.',
      password: 'Passwort', show: 'Anzeigen', hide: 'Verbergen', open: 'Öffnen',
      wrongPassword: 'Das Passwort stimmt nicht.', decrypting: 'Daten werden entschlüsselt …',
      noData: 'Hier sind noch keine Daten. In VitaParms unter Datei → „Website aktualisieren“ hochladen.',
      loadError: 'Die Daten konnten nicht geladen werden: ', oldBrowser: 'Dieser Browser ist zu alt für die Entschlüsselung. Bitte einen aktuellen Browser verwenden.',
      insecure: 'Die Seite muss über https:// aufgerufen werden.',
      loginFoot: 'Ohne das Passwort sind die Daten auf dem Server nicht lesbar.',
      logout: 'Abmelden', persons: 'Personen', person: 'Person', asOf: 'Stand',
      born: 'geboren am', years: 'Jahre', sex: { m: 'männlich', w: 'weiblich', d: 'divers' },
      diagnoses: 'Diagnosen', allergies: 'Allergien', address: 'Adresse', phone: 'Telefon', mobile: 'Mobil',
      notes: 'Bemerkungen', amputations: 'Fehlende Gliedmaßen', emergency: 'Notfallmedikation',
      tabs: { overview: 'Übersicht', chart: 'Verlaufskurven', journal: 'Messwerte', meds: 'Medikamente', appts: 'Termine', reports: 'Berichte' },
      latest: 'Letzte Messwerte', bloodPressure: 'Blutdruck', last30: 'Spur: letzte 30 Tage',
      nextAppts: 'Nächste Termine', currentMeds: 'Regelmedikation', lastObs: 'Letzte Beobachtungen',
      noReadings: 'Für diese Person gibt es noch keine Messwerte.', none: 'Keine.',
      today: 'heute', yesterday: 'gestern', daysAgo: (n) => `vor ${n} Tagen`,
      scales: { day: 'Tag', week: 'Woche', month: 'Monat', year: 'Jahr', custom: 'Zeitraum' },
      prev: 'Zurück', next: 'Weiter', todayBtn: 'Heute', print: 'Drucken', from: 'von', to: 'bis',
      week: (w, y) => `KW ${w}/${y}`, dailyNote: 'Tageswerte: Mittelwert, senkrechter Strich = Minimum bis Maximum',
      noValues: 'Keine Messwerte im Zeitraum', allHidden: 'Alle Kurven ausgeblendet – zum Einblenden auf die Legende klicken',
      toggleHint: 'Klicken: Kurve ein- oder ausblenden', avg: (v, lo, hi, n) => `Ø ${v} (${lo} – ${hi}), ${n} Messungen`,
      range: 'Zeitraum', all: 'Alle', lastDays: (n) => `Letzte ${n} Tage`, lastYear: 'Letzte 12 Monate',
      search: 'Suchen', searchPh: 'Beobachtung, Messzeitpunkt …', csv: 'Als CSV herunterladen',
      when: 'Zeitpunkt', context: 'Messzeitpunkt', observations: 'Beobachtungen', admission: 'Aufnahme',
      count: (n) => `${n} Messung${n === 1 ? '' : 'en'}`, more: (n) => `Weitere ${n} anzeigen`,
      showArchived: 'Auch abgesetzte Medikamente zeigen', archived: 'abgesetzt',
      medCols: ['Präparat', 'Wirkstoff', 'Dosierung', 'Einnahme', 'Grund', 'Hinweise', 'seit'],
      upcoming: 'Anstehend', past: 'Vergangen', allDay: 'ganztägig', showAll: 'Alle anzeigen',
      reportsIntro: 'Berichte, wie VitaParms sie druckt. Sie werden beim Herunterladen in diesem Browser entschlüsselt.',
      download: 'Herunterladen', csvReport: 'Alle Messwerte (CSV, für Excel)', csvType: 'CSV-Datei, wird im Browser erzeugt',
      personal: 'Persönliche Angaben',
      careLevel: 'Pflegegrad', careNone: 'kein Pflegegrad', careReview: 'nächste Überprüfung',
      insuranceNo: 'Vers.-Nr.', tel: 'Tel.', mobileShort: 'mobil',
      advance: 'Vorsorge', disability: 'Schwerbehindertenausweis', gdb: 'GdB', validUntil: 'gültig bis', unlimited: 'unbefristet', reviewSoon: 'bald fällig',
      footer: 'Daten mit AES-256 verschlüsselt, entschlüsselt nur in diesem Browser. Automatische Abmeldung nach 30 Minuten ohne Benutzung.',
      idle: 'Sie wurden nach 30 Minuten ohne Benutzung abgemeldet.',
    },
    en: {
      locale: 'en-GB',
      loginIntro: 'This page shows health data recorded with VitaParms. It is encrypted and only becomes readable in this browser after you enter the password.',
      password: 'Password', show: 'Show', hide: 'Hide', open: 'Open',
      wrongPassword: 'The password is not correct.', decrypting: 'Decrypting data …',
      noData: 'There is no data here yet. Upload it in VitaParms with File → “Update website”.',
      loadError: 'The data could not be loaded: ', oldBrowser: 'This browser is too old for decryption. Please use a current browser.',
      insecure: 'This page must be opened via https://.',
      loginFoot: 'Without the password, the data on the server cannot be read.',
      logout: 'Log out', persons: 'Persons', person: 'Person', asOf: 'As of',
      born: 'born', years: 'years', sex: { m: 'male', w: 'female', d: 'diverse' },
      diagnoses: 'Diagnoses', allergies: 'Allergies', address: 'Address', phone: 'Phone', mobile: 'Mobile',
      notes: 'Notes', amputations: 'Missing limbs', emergency: 'Emergency medication',
      tabs: { overview: 'Overview', chart: 'Trend charts', journal: 'Readings', meds: 'Medication', appts: 'Appointments', reports: 'Reports' },
      latest: 'Latest readings', bloodPressure: 'Blood pressure', last30: 'Trace: last 30 days',
      nextAppts: 'Next appointments', currentMeds: 'Regular medication', lastObs: 'Latest observations',
      noReadings: 'There are no readings for this person yet.', none: 'None.',
      today: 'today', yesterday: 'yesterday', daysAgo: (n) => `${n} days ago`,
      scales: { day: 'Day', week: 'Week', month: 'Month', year: 'Year', custom: 'Period' },
      prev: 'Previous', next: 'Next', todayBtn: 'Today', print: 'Print', from: 'from', to: 'to',
      week: (w, y) => `Week ${w}/${y}`, dailyNote: 'Daily values: mean, vertical line = minimum to maximum',
      noValues: 'No readings in this period', allHidden: 'All curves hidden – click the legend to show them',
      toggleHint: 'Click: show or hide this curve', avg: (v, lo, hi, n) => `Ø ${v} (${lo} – ${hi}), ${n} readings`,
      range: 'Period', all: 'All', lastDays: (n) => `Last ${n} days`, lastYear: 'Last 12 months',
      search: 'Search', searchPh: 'Observation, time of measurement …', csv: 'Download as CSV',
      when: 'Time', context: 'Time of measurement', observations: 'Observations', admission: 'Admission',
      count: (n) => `${n} reading${n === 1 ? '' : 's'}`, more: (n) => `Show ${n} more`,
      showArchived: 'Also show discontinued medicines', archived: 'discontinued',
      medCols: ['Product', 'Active substance', 'Dosage', 'Intake', 'Reason', 'Instructions', 'since'],
      upcoming: 'Upcoming', past: 'Past', allDay: 'all day', showAll: 'Show all',
      reportsIntro: 'Reports as VitaParms prints them. They are decrypted in this browser when you download them.',
      download: 'Download', csvReport: 'All readings (CSV, for Excel)', csvType: 'CSV file, created in the browser',
      personal: 'Personal details',
      careLevel: 'Care level', careNone: 'no care level', careReview: 'next review',
      insuranceNo: 'Insurance no.', tel: 'Tel.', mobileShort: 'mobile',
      advance: 'Advance directives', disability: 'Severe disability card', gdb: 'Degree of disability', validUntil: 'valid until', unlimited: 'unlimited', reviewSoon: 'due soon',
      footer: 'Data encrypted with AES-256, decrypted only in this browser. Automatic logout after 30 minutes without use.',
      idle: 'You were logged out after 30 minutes without use.',
    },
  };
  let L = TEXT.de;

  // ================================================================ Hilfen
  const $ = (sel, el = document) => el.querySelector(sel);
  const SVGNS = 'http://www.w3.org/2000/svg';
  function h(tag, attrs, ...kids) {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (v === null || v === undefined || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'text') el.textContent = v;
      else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
      else el.setAttribute(k, v === true ? '' : v);
    }
    for (const c of kids.flat()) if (c !== null && c !== undefined && c !== false) el.append(c.nodeType ? c : String(c));
    return el;
  }
  function s(tag, attrs) {
    const el = document.createElementNS(SVGNS, tag);
    for (const [k, v] of Object.entries(attrs || {})) if (v !== null && v !== undefined) el.setAttribute(k, v);
    return el;
  }
  const b64 = (str) => Uint8Array.from(atob(str), (c) => c.charCodeAt(0));
  const toB64 = (u8) => btoa(String.fromCharCode(...u8));
  const DAY = 86400000;
  const startOfDay = (d) => new Date(d.getFullYear(), d.getMonth(), d.getDate());
  const addDays = (d, n) => new Date(d.getFullYear(), d.getMonth(), d.getDate() + n);
  const parseT = (str) => {                       // "2026-10-19T07:12" als Ortszeit
    const [d, t = '00:00'] = str.split('T');
    const [y, m, dd] = d.split('-').map(Number);
    const [hh, mi] = t.split(':').map(Number);
    return new Date(y, m - 1, dd, hh, mi).getTime();
  };
  const parseD = (str) => (str ? parseT(str) : null);
  let fmtDate, fmtDateTime, fmtTime, fmtMonth, fmtDow, fmtDayMonth;
  function setupFormats() {
    const loc = L.locale;
    fmtDate = new Intl.DateTimeFormat(loc, { day: '2-digit', month: '2-digit', year: 'numeric' });
    fmtDateTime = new Intl.DateTimeFormat(loc, { weekday: 'short', day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit' });
    fmtTime = new Intl.DateTimeFormat(loc, { hour: '2-digit', minute: '2-digit' });
    fmtMonth = new Intl.DateTimeFormat(loc, { month: 'long', year: 'numeric' });
    fmtDow = new Intl.DateTimeFormat(loc, { weekday: 'short', day: '2-digit', month: '2-digit' });
    fmtDayMonth = new Intl.DateTimeFormat(loc, { day: '2-digit', month: '2-digit' });
  }
  const numFmts = {};
  const fmtNum = (v, d = 0) => (numFmts[d] ||= new Intl.NumberFormat(L.locale, { minimumFractionDigits: d, maximumFractionDigits: d })).format(v);
  const isoWeek = (date) => {
    const d = new Date(Date.UTC(date.getFullYear(), date.getMonth(), date.getDate()));
    const day = d.getUTCDay() || 7;
    d.setUTCDate(d.getUTCDate() + 4 - day);
    const y = d.getUTCFullYear();
    return [Math.ceil(((d - Date.UTC(y, 0, 1)) / DAY + 1) / 7), y];
  };
  function relDay(ms) {
    const days = Math.round((startOfDay(new Date()) - startOfDay(new Date(ms))) / DAY);
    if (days === 0) return `${L.today} ${fmtTime.format(ms)}`;
    if (days === 1) return `${L.yesterday} ${fmtTime.format(ms)}`;
    if (days > 1 && days < 14) return L.daysAgo(days);
    return fmtDate.format(ms);
  }
  function download(name, blob) {
    const url = URL.createObjectURL(blob);
    const a = h('a', { href: url, download: name });
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 30000);
  }

  // ================================================================ Kryptografie
  const enc = new TextEncoder();
  async function deriveKeys(password, salt, iterations) {
    const base = await crypto.subtle.importKey('raw', enc.encode(password), 'PBKDF2', false, ['deriveBits']);
    const bits = new Uint8Array(await crypto.subtle.deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt, iterations }, base, 512));
    return importKeys(bits);
  }
  async function importKeys(bits) {
    const aes = await crypto.subtle.importKey('raw', bits.slice(0, 32), { name: 'AES-CTR' }, false, ['decrypt']);
    const mac = await crypto.subtle.importKey('raw', bits.slice(32, 64), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign', 'verify']);
    return { aes, mac, raw: bits };
  }
  async function passwordMatches(keys, checkB64) {
    return crypto.subtle.verify('HMAC', keys.mac, b64(checkB64), enc.encode('vitaparms-password-check'));
  }
  async function decrypt(buffer, keys) {
    const u8 = new Uint8Array(buffer);
    if (u8.length < 52 || String.fromCharCode(...u8.slice(0, 4)) !== 'VPW1') throw new Error('format');
    const body = u8.slice(0, u8.length - 32);
    if (!(await crypto.subtle.verify('HMAC', keys.mac, u8.slice(u8.length - 32), body))) throw new Error('integrity');
    return new Uint8Array(await crypto.subtle.decrypt({ name: 'AES-CTR', counter: body.slice(4, 20), length: 64 }, keys.aes, body.slice(20)));
  }
  async function inflate(u8) {
    const stream = new Blob([u8]).stream().pipeThrough(new DecompressionStream('deflate'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }
  async function fetchFile(name) {
    const r = await fetch(`data/${state.manifest.bundle}/${name}`, { cache: 'no-store' });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    return decrypt(await r.arrayBuffer(), state.keys);
  }

  // ================================================================ Zustand
  const state = {
    manifest: null, keys: null, data: null, person: null, tab: 'overview',
    vitals: {}, panels: [],
    chart: { scale: 'month', ref: startOfDay(new Date()), from: null, to: null, panels: null, hidden: new Set() },
    journal: { range: 'all', query: '', shown: 200 },
    meds: { archived: false }, appts: { allPast: false },
  };
  const SESSION = 'vitaparms.session';

  // ================================================================ Start und Anmeldung
  async function init() {
    if (location.protocol === 'http:' && !/^(localhost|127\.0\.0\.1)$/.test(location.hostname)) {
      location.replace(location.href.replace(/^http:/, 'https:'));
      return;
    }
    const browserLang = (navigator.language || 'de').toLowerCase().startsWith('de') ? 'de' : 'en';
    let manifest = null, problem = null;
    try {
      const r = await fetch('data/current.json', { cache: 'no-store' });
      if (r.ok) manifest = await r.json();
      else problem = r.status === 404 || r.status === 403 ? 'noData' : `HTTP ${r.status}`;
    } catch (e) { problem = e.message; }
    L = TEXT[(manifest && manifest.lang) || browserLang] || TEXT.de;
    document.documentElement.lang = L === TEXT.de ? 'de' : 'en';
    setupFormats();
    state.manifest = manifest;
    buildLogin();
    if (!window.crypto || !crypto.subtle || typeof DecompressionStream === 'undefined') return loginMessage(L.oldBrowser);
    if (!manifest) return loginMessage(problem === 'noData' ? L.noData : L.loadError + problem);
    // Sitzung im selben Tab wiederherstellen (sessionStorage endet mit dem Tab)
    try {
      const saved = JSON.parse(sessionStorage.getItem(SESSION) || 'null');
      if (saved && saved.bundle === manifest.bundle && saved.salt === manifest.kdf.salt) {
        state.keys = await importKeys(b64(saved.key));
        if (await passwordMatches(state.keys, manifest.check)) return loadData();
      }
    } catch (e) { /* neu anmelden */ }
    $('#login').hidden = false;
    $('#password').focus();
  }

  function buildLogin() {
    $('#loginIntro').textContent = L.loginIntro;
    $('#pwLabel').textContent = L.password;
    $('#pwShow').textContent = L.show;
    $('#loginBtn').textContent = L.open;
    $('#loginFoot').textContent = L.loginFoot;
    $('#pwShow').addEventListener('click', () => {
      const pw = $('#password');
      pw.type = pw.type === 'password' ? 'text' : 'password';
      $('#pwShow').textContent = pw.type === 'password' ? L.show : L.hide;
    });
    $('#loginForm').addEventListener('submit', async (e) => {
      e.preventDefault();
      const pw = $('#password').value;
      if (!pw || !state.manifest) return;
      $('#loginBtn').disabled = true;
      loginMessage(L.decrypting, true);
      try {
        state.keys = await deriveKeys(pw, b64(state.manifest.kdf.salt), state.manifest.kdf.iterations);
        if (!(await passwordMatches(state.keys, state.manifest.check))) {
          state.keys = null;
          loginMessage(L.wrongPassword);
          $('#password').select();
          return;
        }
        sessionStorage.setItem(SESSION, JSON.stringify({ bundle: state.manifest.bundle, salt: state.manifest.kdf.salt, key: toB64(state.keys.raw) }));
        await loadData();
      } catch (err) {
        loginMessage(L.loadError + err.message);
      } finally {
        $('#loginBtn').disabled = false;
      }
    });
    if (sessionStorage.getItem('vitaparms.idle')) {
      sessionStorage.removeItem('vitaparms.idle');
      loginMessage(L.idle, true);
    }
  }

  function loginMessage(text, busy = false) {
    $('#login').hidden = false;
    const m = $('#loginMsg');
    m.textContent = text;
    m.classList.toggle('busy', busy);
  }

  async function loadData() {
    loginMessage(L.decrypting, true);
    const plain = await fetchFile(state.manifest.data);
    state.data = JSON.parse(new TextDecoder().decode(await inflate(plain)));
    for (const v of state.data.vitals) { v.base = v.color; state.vitals[v.key] = v; }
    applyScheme(false);
    const dark = matchMedia('(prefers-color-scheme: dark)');
    dark.addEventListener('change', () => applyScheme(true));
    addEventListener('beforeprint', () => applyScheme(true, true));
    addEventListener('afterprint', () => applyScheme(true));
    state.panels = state.data.panels;
    for (const p of state.data.persons) {
      p.rs = p.readings.map((r) => ({ ...r, ms: parseT(r.t) })).sort((a, b) => a.ms - b.ms);
    }
    $('#login').hidden = true;
    $('#app').hidden = false;
    startIdleTimer();
    buildShell();
    route();
  }

  // Sehr dunkle Kurvenfarben (z. B. Gewicht) sind auf dunklem Grund unsichtbar → aufhellen
  function applyScheme(rerender, printing = false) {
    const dark = !printing && matchMedia('(prefers-color-scheme: dark)').matches;
    for (const v of Object.values(state.vitals)) {
      const n = parseInt(v.base.slice(1), 16);
      const lum = (0.2126 * (n >> 16) + 0.7152 * ((n >> 8) & 255) + 0.0722 * (n & 255)) / 255;
      v.color = dark && lum < 0.25 ? '#C3CDD6' : v.base;
    }
    if (rerender && state.person) renderView();
  }

  function logout(idle = false) {
    sessionStorage.removeItem(SESSION);
    if (idle) sessionStorage.setItem('vitaparms.idle', '1');
    location.hash = '';
    location.reload();
  }

  let idleTimer = null;
  function startIdleTimer() {
    const reset = () => { clearTimeout(idleTimer); idleTimer = setTimeout(() => logout(true), 30 * 60 * 1000); };
    for (const ev of ['pointerdown', 'keydown', 'wheel', 'touchstart']) addEventListener(ev, reset, { passive: true });
    reset();
  }

  // ================================================================ Rahmen und Navigation
  const TABS = ['overview', 'chart', 'journal', 'meds', 'appts', 'reports'];

  function buildShell() {
    $('#logout').textContent = L.logout;
    $('#logout').addEventListener('click', () => logout());
    $('#personSelectLabel').textContent = L.person;
    $('#stamp').textContent = `${L.asOf}: ${fmtDateTime.format(new Date(state.data.generated))}`;
    $('#footer').textContent = [state.data.disclaimer, L.footer].filter(Boolean).map((x) => x.trim().replace(/[.]?$/, '.')).join(' ');
    $('#brandLink').addEventListener('click', (e) => { e.preventDefault(); go({ tab: 'overview' }); });
    const rail = $('#rail');
    rail.append(h('h2', { text: L.persons }));
    const sel = $('#personSelect');
    for (const p of state.data.persons) {
      const born = p.birthDate ? `${L.born} ${fmtDate.format(parseD(p.birthDate))}` : '';
      rail.append(h('button', { type: 'button', 'data-id': p.id, onclick: () => go({ person: p.id }) },
        h('span', { text: p.name }), born && h('small', { text: born })));
      sel.append(h('option', { value: p.id, text: p.name }));
    }
    sel.addEventListener('change', () => go({ person: sel.value }));
    const tabs = $('#tabs');
    for (const t of TABS)
      tabs.append(h('button', { type: 'button', role: 'tab', 'data-tab': t, onclick: () => go({ tab: t }), text: L.tabs[t] }));
    addEventListener('hashchange', route);
    let rt = null;
    addEventListener('resize', () => { clearTimeout(rt); rt = setTimeout(() => { if (state.tab === 'chart') renderView(); }, 150); });
  }

  function go({ person, tab }) {
    const p = person ?? state.person?.id;
    const t = tab ?? state.tab;
    location.hash = `#p=${encodeURIComponent(p)}&t=${t}`;
  }

  function route() {
    const q = new URLSearchParams(location.hash.slice(1));
    const persons = state.data.persons;
    const p = persons.find((x) => x.id === q.get('p')) || persons[0];
    const t = TABS.includes(q.get('t')) ? q.get('t') : 'overview';
    const changedPerson = p !== state.person;
    state.person = p;
    state.tab = t;
    if (changedPerson) { state.chart.panels = null; state.journal.shown = 200; }
    for (const b of $('#rail').querySelectorAll('button')) b.setAttribute('aria-current', b.dataset.id === p?.id ? 'true' : 'false');
    $('#personSelect').value = p?.id ?? '';
    for (const b of $('#tabs').querySelectorAll('button')) b.setAttribute('aria-selected', b.dataset.tab === t ? 'true' : 'false');
    document.title = p ? `${p.name} – VitaParms` : 'VitaParms';
    renderHead();
    renderView();
  }

  // ================================================================ Kopf der Person
  function age(birth) {
    if (!birth) return null;
    const b = new Date(parseD(birth)), n = new Date();
    let a = n.getFullYear() - b.getFullYear();
    if (n.getMonth() < b.getMonth() || (n.getMonth() === b.getMonth() && n.getDate() < b.getDate())) a--;
    return a;
  }

  function renderHead() {
    const head = $('#personHead');
    head.replaceChildren();
    const p = state.person;
    if (!p) return;
    head.append(h('h1', { text: p.name }));
    const meta = [];
    if (p.birthDate) meta.push(`${L.born} ${fmtDate.format(parseD(p.birthDate))}, ${age(p.birthDate)} ${L.years}`);
    if (L.sex[p.sex]) meta.push(L.sex[p.sex]);
    if (meta.length) head.append(h('p', { class: 'meta', text: meta.join(', ') }));
    const dl = h('dl');
    const row = (k, v) => { if (v && String(v).trim()) dl.append(h('dt', { text: k }), h('dd', { text: v })); };
    row(L.diagnoses, p.diagnoses);
    row(L.allergies, p.allergies);
    for (const [k, v] of Object.entries(p.fields || {})) if (k !== 'Allergien' && k !== 'Allergies' && v !== p.allergies) row(k, v);
    row(L.amputations, p.amputations);
    if (p.careLevel) {
      const due = p.careReview && parseD(p.careReview) - Date.now() < 60 * DAY;
      row(L.careLevel, `${p.careLevel}` + (p.careReview ? ` – ${L.careReview} ${fmtDate.format(parseD(p.careReview))}${due ? ` (${L.reviewSoon})` : ''}` : ''));
    }
    if (p.disabilityDegree) {
      const marks = (p.disabilityMarks || []).map((m) => {
        const meaning = (state.data.disabilityMarks || {})[m];
        return meaning ? `${m} (${meaning})` : m;
      });
      row(L.disability, [`${L.gdb} ${p.disabilityDegree}`, ...marks].join(', ')
        + ` – ${p.disabilityValid ? `${L.validUntil} ${fmtDate.format(parseD(p.disabilityValid))}` : L.unlimited}`);
    }
    for (const line of (p.advance || [])) row(L.advance, line);
    for (const c of (p.contacts || [])) {
      const dd = h('dd');
      const parts = [c.name, c.person, c.insuranceNo ? `${L.insuranceNo} ${c.insuranceNo}` : '', c.address].filter(Boolean);
      dd.append(parts.join(', '));
      const link = (href, text) => h('a', { href, text, rel: 'noopener', target: href.startsWith('http') ? '_blank' : '' });
      const extras = [];
      if (c.phone) extras.push(link(`tel:${c.phone.replace(/[^0-9+]/g, '')}`, `${L.tel} ${c.phone}`));
      if (c.mobile) extras.push(link(`tel:${c.mobile.replace(/[^0-9+]/g, '')}`, `${L.mobileShort} ${c.mobile}`));
      if (c.email) extras.push(link(`mailto:${c.email}`, c.email));
      if (c.website) extras.push(link(/^https?:/i.test(c.website) ? c.website : `https://${c.website}`, c.website));
      for (const x of extras) dd.append(' · ', x);
      if (c.note) dd.append(h('div', { class: 'sub', text: c.note }));
      dl.append(h('dt', { text: c.kind }), dd);
    }
    row(L.address, p.address);
    row(L.phone, [p.phone, p.mobile].filter(Boolean).join(', '));
    if (dl.children.length) {
      if (state.tab === 'overview') head.append(dl);
      else head.append(h('details', { class: 'more' }, h('summary', { text: L.personal }), dl));
    }
    const em = p.medications.filter((m) => m.category === 2 && !m.archived);
    if (em.length)
      head.append(h('div', { class: 'emergency' }, h('strong', { text: `${L.emergency}: ` }),
        em.map((m) => `${medName(m)}${m.schedule ? ' – ' + m.schedule : ''}`).join('; ')));
  }

  function medName(m) {
    return [m.name, m.strength && !m.name.includes(m.strength) ? m.strength : '', m.form ? `(${m.form})` : ''].filter(Boolean).join(' ');
  }

  // ================================================================ Ansichten
  function renderView() {
    const view = $('#view');
    view.replaceChildren();
    hideTip();
    const p = state.person;
    if (!p) return;
    ({ overview, chart, journal, meds, appts, reports })[state.tab](view, p);
  }

  // ---------------------------------------------------------------- Übersicht
  function overview(view, p) {
    view.append(h('h2', { text: L.latest }));
    if (!p.rs.length) { view.append(h('p', { class: 'empty', text: L.noReadings })); }
    else {
      const strip = h('div', { class: 'strip' });
      const order = ['systolic', 'pulse', 'glucose', 'weight', 'bmi', 'temperature', 'spo2', 'peakflow', 'inr', 'edema'];
      for (const key of order) {
        const def = state.vitals[key];
        if (!def) continue;
        const last = [...p.rs].reverse().find((r) => r.v[key] !== undefined);
        if (!last) continue;
        const keys = key === 'systolic' ? ['systolic', 'diastolic'] : [key];
        const label = key === 'systolic' ? L.bloodPressure : def.label;
        const value = key === 'systolic' && last.v.diastolic !== undefined
          ? `${fmtNum(last.v.systolic)}/${fmtNum(last.v.diastolic)}` : fmtNum(last.v[key], def.decimals);
        const panel = state.panels.find((x) => x.keys.includes(key));
        const tile = h('div', {},
          h('div', { class: 'lbl' }, legendSwatch(keys.map((k) => state.vitals[k])), label),
          h('div', { class: 'val num' }, value, h('small', { text: def.unit })),
          h('div', { class: 'when', text: relDay(last.ms) + (last.ctx ? `, ${last.ctx}` : '') }),
          sparkline(p, keys));
        tile.tabIndex = 0;
        tile.setAttribute('role', 'link');
        tile.title = L.tabs.chart;
        const open = () => { state.chart.panels = new Set([panel?.id].filter(Boolean)); go({ tab: 'chart' }); };
        tile.addEventListener('click', open);
        tile.addEventListener('keydown', (e) => { if (e.key === 'Enter') open(); });
        tile.classList.add('tile');
        strip.append(tile);
      }
      const ss = stoolStatus(p);
      if (ss && ss.tracked) {
        const tile = h('div', { class: 'tile' + (ss.warn ? ' alert' : '') },
          h('div', { class: 'lbl' }, legendSwatch([state.vitals.stool]), state.vitals.stool.label),
          h('div', { class: 'val', text: ss.text }),
          h('div', { class: 'when', text: fmtDate.format(ss.last) }));
        tile.tabIndex = 0;
        tile.setAttribute('role', 'link');
        const open = () => { state.chart.panels = new Set(['stool']); go({ tab: 'chart' }); };
        tile.addEventListener('click', open);
        tile.addEventListener('keydown', (e) => { if (e.key === 'Enter') open(); });
        strip.append(tile);
      }
      if (ss && ss.warn) view.insertBefore(h('div', { class: 'banner warn', role: 'status', text: ss.warn }), view.firstChild);
      view.append(strip, h('p', { class: 'hint', text: L.last30 }));
    }
    const cols = h('div', { class: 'overview-cols' });
    const now = Date.now();
    const next = p.appointments.filter((a) => !a.done && parseT(a.start) >= startOfDay(new Date()).getTime()).slice(0, 5);
    cols.append(h('div', {}, h('h2', { text: L.nextAppts }), next.length ? apptList(next) : h('p', { class: 'empty', text: L.none })));
    const regular = p.medications.filter((m) => m.category === 0 && !m.archived);
    const ml = h('ul', { class: 'list' });
    for (const m of regular)
      ml.append(h('li', {}, h('span', { class: 'num', text: `${m.dose} ${m.unit || ''}` }), h('span', {}, medName(m), m.schedule ? h('div', { class: 'sub', text: m.schedule }) : null)));
    cols.append(h('div', {}, h('h2', { text: L.currentMeds }), regular.length ? ml : h('p', { class: 'empty', text: L.none })));
    view.append(cols);
    const obs = p.rs.filter((r) => r.obs).slice(-5).reverse();
    if (obs.length) {
      const ol = h('ul', { class: 'list' });
      for (const r of obs) ol.append(h('li', {}, h('span', { text: fmtDate.format(r.ms) }), h('span', { text: r.obs })));
      view.append(h('h2', { text: L.lastObs }), ol);
    }
    void now;
  }

  function sparkline(p, keys) {
    const svg = s('svg', { class: 'spark', viewBox: '0 0 200 38', preserveAspectRatio: 'none', 'aria-hidden': 'true' });
    const to = Date.now(), from = to - 30 * DAY;
    let lo = Infinity, hi = -Infinity;
    const series = keys.map((k) => {
      const pts = daily(p.rs.filter((r) => r.ms >= from && r.v[k] !== undefined).map((r) => ({ ms: r.ms, v: r.v[k] })));
      for (const q of pts) { lo = Math.min(lo, q.v); hi = Math.max(hi, q.v); }
      return { k, pts };
    });
    if (!(hi >= lo)) return svg;
    if (hi === lo) { hi += 1; lo -= 1; }
    const x = (ms) => ((ms - from) / (to - from)) * 196 + 2, y = (v) => 34 - ((v - lo) / (hi - lo)) * 30;
    for (const { k, pts } of series) {
      if (!pts.length) continue;
      const col = state.vitals[k].color;
      svg.append(s('path', { d: pts.map((q, i) => `${i ? 'L' : 'M'}${x(q.ms).toFixed(1)},${y(q.v).toFixed(1)}`).join(''),
        fill: 'none', stroke: col, 'stroke-width': 1.6, 'vector-effect': 'non-scaling-stroke', 'stroke-linejoin': 'round' }));
      const l = pts[pts.length - 1];
      svg.append(s('circle', { cx: x(l.ms), cy: y(l.v), r: 2.6, fill: col }));
    }
    return svg;
  }

  function legendSwatch(defs) {
    const svg = s('svg', { viewBox: `0 0 ${defs.length * 14 + 12} 12`, width: defs.length * 14 + 12, height: 12, 'aria-hidden': 'true' });
    defs.forEach((d, i) => svg.append(marker(d.marker, 6 + i * 14, 6, 4, d.color, null)));
    return svg;
  }

  // ---------------------------------------------------------------- Verlaufskurven
  function daily(pts) {
    const out = [];
    for (const q of pts) {
      const d = startOfDay(new Date(q.ms)).getTime() + DAY / 2;
      const last = out[out.length - 1];
      if (last && last.ms === d) { last.sum += q.v; last.n++; last.lo = Math.min(last.lo, q.v); last.hi = Math.max(last.hi, q.v); }
      else out.push({ ms: d, sum: q.v, n: 1, lo: q.v, hi: q.v });
    }
    for (const o of out) o.v = o.sum / o.n;
    return out;
  }

  function chartRange() {
    const c = state.chart, r = c.ref;
    switch (c.scale) {
      case 'day': return [r, addDays(r, 1)];
      case 'week': { const m = addDays(r, -((r.getDay() + 6) % 7)); return [m, addDays(m, 7)]; }
      case 'month': return [new Date(r.getFullYear(), r.getMonth(), 1), new Date(r.getFullYear(), r.getMonth() + 1, 1)];
      case 'year': return [new Date(r.getFullYear(), 0, 1), new Date(r.getFullYear() + 1, 0, 1)];
      default: {
        const p = state.person;
        const from = c.from || (p.rs.length ? startOfDay(new Date(p.rs[0].ms)) : addDays(r, -90));
        const to = c.to || addDays(startOfDay(new Date()), 1);
        return from < to ? [from, to] : [to, from];
      }
    }
  }

  function periodText(from, to) {
    switch (state.chart.scale) {
      case 'day': return new Intl.DateTimeFormat(L.locale, { weekday: 'long', day: '2-digit', month: '2-digit', year: 'numeric' }).format(from);
      case 'week': { const [w, y] = isoWeek(from); return `${L.week(w, y)}: ${fmtDayMonth.format(from)} – ${fmtDate.format(addDays(to, -1))}`; }
      case 'month': return fmtMonth.format(from);
      case 'year': return String(from.getFullYear());
      default: return `${fmtDate.format(from)} – ${fmtDate.format(addDays(to, -1))}`;
    }
  }

  function step(dir) {
    const c = state.chart, r = c.ref;
    c.ref = c.scale === 'day' ? addDays(r, dir) : c.scale === 'week' ? addDays(r, 7 * dir)
      : c.scale === 'month' ? new Date(r.getFullYear(), r.getMonth() + dir, 1) : new Date(r.getFullYear() + dir, 0, 1);
    renderView();
  }

  const isoDate = (d) => `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;

  function chart(view, p) {
    const c = state.chart;
    if (!c.panels) {
      const withData = state.panels.filter((pd) => pd.keys.some((k) => p.rs.some((r) => r.v[k] !== undefined))).map((pd) => pd.id);
      const pref = ['bp', 'glucose', 'weight'].filter((id) => withData.includes(id));
      c.panels = new Set(pref.length ? pref : withData.slice(0, 3));
    }
    const [from, to] = chartRange();
    const ctl = h('div', { class: 'chart-controls' });
    const seg = h('div', { class: 'seg', role: 'group', 'aria-label': L.range });
    for (const sc of ['day', 'week', 'month', 'year', 'custom'])
      seg.append(h('button', { type: 'button', 'aria-pressed': c.scale === sc ? 'true' : 'false', text: L.scales[sc],
        onclick: () => { c.scale = sc; renderView(); } }));
    ctl.append(seg);
    if (c.scale !== 'custom') {
      ctl.append(h('button', { type: 'button', class: 'ghost', 'aria-label': L.prev, title: L.prev, text: '◀', onclick: () => step(-1) }),
        h('span', { class: 'period', text: periodText(from, to) }),
        h('button', { type: 'button', class: 'ghost', 'aria-label': L.next, title: L.next, text: '▶', onclick: () => step(1) }),
        h('button', { type: 'button', class: 'ghost', text: L.todayBtn, onclick: () => { c.ref = startOfDay(new Date()); renderView(); } }));
    } else {
      const fromIn = h('input', { type: 'date', value: isoDate(from), 'aria-label': L.from });
      const toIn = h('input', { type: 'date', value: isoDate(addDays(to, -1)), 'aria-label': L.to });
      const apply = () => {
        if (fromIn.value) c.from = new Date(parseT(fromIn.value));
        if (toIn.value) c.to = addDays(new Date(parseT(toIn.value)), 1);
        renderView();
      };
      fromIn.addEventListener('change', apply);
      toIn.addEventListener('change', apply);
      ctl.append(h('span', { text: L.from }), fromIn, h('span', { text: L.to }), toIn);
    }
    ctl.append(h('span', { class: 'grow' }), h('button', { type: 'button', class: 'ghost noprint', text: L.print, onclick: () => print() }));
    view.append(ctl);

    const chips = h('div', { class: 'panel-chips' });
    for (const pd of state.panels) {
      const has = pd.keys.some((k) => p.rs.some((r) => r.v[k] !== undefined));
      if (!has) continue;
      chips.append(h('button', { type: 'button', class: 'chip', 'aria-pressed': c.panels.has(pd.id) ? 'true' : 'false',
        onclick: () => { c.panels.has(pd.id) ? c.panels.delete(pd.id) : c.panels.add(pd.id); renderView(); } },
        legendSwatch(pd.keys.map((k) => state.vitals[k])), pd.title));
    }
    view.append(chips);
    if (!p.rs.length) { view.append(h('p', { class: 'empty', text: L.noReadings })); return; }
    for (const pd of state.panels) if (c.panels.has(pd.id)) view.append(panelChart(pd, p, from.getTime(), to.getTime()));
  }

  function niceTicks(lo, hi, target) {
    if (!(hi > lo)) { const pad = Math.max(1, Math.abs(hi) * 0.05); lo -= pad; hi += pad; }
    const rough = (hi - lo) / Math.max(1, target - 1);
    const mag = Math.pow(10, Math.floor(Math.log10(rough)));
    const n = rough / mag;
    const stepv = (n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5 ? 2.5 : n <= 5 ? 5 : 10) * mag;
    const nlo = Math.floor(lo / stepv) * stepv, nhi = Math.ceil(hi / stepv) * stepv;
    const ticks = [];
    for (let v = nlo; v <= nhi + stepv / 2; v += stepv) ticks.push(Math.abs(v) < stepv * 1e-9 ? 0 : v);
    return { ticks, lo: nlo, hi: nhi, step: stepv };
  }

  function timeTicks(from, to, width) {
    const span = to - from;
    const cands = [
      [3600e3, 'time'], [2 * 3600e3, 'time'], [3 * 3600e3, 'time'], [6 * 3600e3, 'time'], [12 * 3600e3, 'dtime'],
      [DAY, 'dow'], [DAY, 'dm'], [2 * DAY, 'dm'], [7 * DAY, 'dm'], [14 * DAY, 'dm'],
      ['m1', 'mon'], ['m3', 'mon'], ['m6', 'mon'], ['m12', 'year'],
    ];
    const labelW = { time: 46, dtime: 90, dow: 78, dm: 50, mon: 62, year: 42 };
    let pick = cands[cands.length - 1];
    for (const cnd of cands) {
      if (span > 2 * DAY && typeof cnd[0] === 'number' && cnd[0] < DAY) continue;
      const approx = typeof cnd[0] === 'number' ? cnd[0] : Number(cnd[0].slice(1)) * 30.4 * DAY;
      if ((span / approx) * labelW[cnd[1]] * 1.3 <= width) { pick = cnd; break; }
    }
    const [stepv, kind] = pick;
    const ticks = [];
    const fmt = { time: fmtTime, dtime: new Intl.DateTimeFormat(L.locale, { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' }),
      dow: fmtDow, dm: fmtDayMonth, mon: new Intl.DateTimeFormat(L.locale, { month: 'short', year: '2-digit' }),
      year: new Intl.DateTimeFormat(L.locale, { year: 'numeric' }) }[kind];
    const f = new Date(from);
    if (typeof stepv === 'string') {
      const months = Number(stepv.slice(1));
      let d = months === 12 ? new Date(f.getFullYear(), 0, 1) : new Date(f.getFullYear(), f.getMonth(), 1);
      for (; d.getTime() <= to; d = new Date(d.getFullYear(), d.getMonth() + months, 1)) if (d.getTime() >= from) ticks.push(d.getTime());
    } else if (stepv >= DAY) {
      let d = startOfDay(f);
      if (stepv >= 7 * DAY) d = addDays(d, -((d.getDay() + 6) % 7));
      for (; d.getTime() <= to; d = addDays(d, stepv / DAY)) if (d.getTime() >= from) ticks.push(d.getTime());
    } else {
      const hours = stepv / 3600e3;
      let d = new Date(f.getFullYear(), f.getMonth(), f.getDate(), f.getHours() - (f.getHours() % hours));
      for (; d.getTime() <= to; d = new Date(d.getTime() + stepv)) if (d.getTime() >= from) ticks.push(d.getTime());
    }
    const daySeps = [];
    if (typeof stepv === 'number' && stepv < DAY && span > DAY)
      for (let d = addDays(startOfDay(f), 1); d.getTime() < to; d = addDays(d, 1)) daySeps.push(d.getTime());
    return { ticks, fmt, daySeps };
  }

  function marker(shape, cx, cy, r, color, ring) {
    let el;
    const common = { fill: color };
    if (ring) Object.assign(common, { stroke: ring, 'stroke-width': 1.5, 'paint-order': 'stroke' });
    switch (shape) {
      case 'square': el = s('rect', { x: cx - r * 0.85, y: cy - r * 0.85, width: r * 1.7, height: r * 1.7, ...common }); break;
      case 'triangle': el = s('polygon', { points: `${cx},${cy - r * 1.15} ${cx - r},${cy + r * 0.75} ${cx + r},${cy + r * 0.75}`, ...common }); break;
      case 'triangleDown': el = s('polygon', { points: `${cx},${cy + r * 1.15} ${cx - r},${cy - r * 0.75} ${cx + r},${cy - r * 0.75}`, ...common }); break;
      case 'diamond': el = s('polygon', { points: `${cx},${cy - r * 1.2} ${cx + r},${cy} ${cx},${cy + r * 1.2} ${cx - r},${cy}`, ...common }); break;
      case 'cross': el = s('path', { d: `M${cx - r},${cy - r}L${cx + r},${cy + r}M${cx - r},${cy + r}L${cx + r},${cy - r}`, stroke: color, 'stroke-width': 2, fill: 'none' }); break;
      default: el = s('circle', { cx, cy, r, ...common });
    }
    return el;
  }

  function surfaceColor() { return getComputedStyle(document.documentElement).getPropertyValue('--surface').trim() || '#fff'; }

  // ---------------------------------------------------------------- Stuhlgang
  const isDark = () => !document.body.classList.contains('printing') && matchMedia('(prefers-color-scheme: dark)').matches;
  const dayKey = (ms) => isoDate(new Date(ms));
  function stoolText(r) {
    const st = state.data.stool;
    if (!st || r.v.stool === undefined) return '';
    return st.names[r.v.stool] + (r.sd ? ` (${r.sd.filter(Boolean).join(', ')})` : '');
  }
  function edemaText(r) {
    if (r.v.edema === undefined) return '';
    const parts = [state.data.stool.edemaNames[r.v.edema]];
    if (r.bi && r.v.edema > 0) parts.push(...[r.bi[2], r.bi[3]].filter(Boolean));
    return parts.join(', ');
  }
  function stoolGroupColor(type) {
    const st = state.data.stool, g = st.groups[st.groupOf[type] || 0];
    return isDark() ? g.dark : g.color;
  }
  // Tage mit Stuhlgang (Map isoDate → Typen) und Zeitspannen ohne Stuhlgang (≥ minDays, höchstens trackedDays)
  function stoolDays(p) {
    const m = new Map();
    for (const r of p.rs) if (r.v.stool !== undefined) { const k = dayKey(r.ms); if (!m.has(k)) m.set(k, []); m.get(k).push(r.v.stool); }
    return m;
  }
  function stoolGaps(days, minDays) {
    const st = state.data.stool, keys = [...days.keys()].sort(), gaps = [];
    const add = (a, b) => { const n = Math.round((b - a) / DAY) + 1; if (n >= minDays && n <= st.trackedDays) gaps.push({ a, b, n }); };
    for (let i = 1; i < keys.length; i++) {
      const prev = new Date(parseT(keys[i - 1])).getTime(), cur = new Date(parseT(keys[i])).getTime();
      if (Math.round((cur - prev) / DAY) > 1) add(addDays(new Date(prev), 1).getTime(), addDays(new Date(cur), -1).getTime());
    }
    if (keys.length) {
      const last = new Date(parseT(keys[keys.length - 1])), today = startOfDay(new Date());
      if (last < today) add(addDays(last, 1).getTime(), today.getTime());
    }
    return gaps;
  }
  function stoolStatus(p) {
    const st = state.data.stool;
    if (!st) return null;
    const keys = [...stoolDays(p).keys()].sort();
    if (!keys.length) return null;
    const last = new Date(parseT(keys[keys.length - 1]));
    const days = Math.round((startOfDay(new Date()) - last) / DAY);
    const text = days <= 0 ? st.today : days === 1 ? st.yesterday : st.ago.replace('%1', days);
    return { last, days, text, tracked: days <= st.trackedDays,
             warn: days >= st.warnDays && days <= st.trackedDays ? st.warn.replace('%1', days).replace('%2', fmtDate.format(last)) : '' };
  }
  function stoolChart(pd, p, from, to) {
    const st = state.data.stool;
    const box = h('div', { class: 'panel' });
    const head = h('div', { class: 'panel-head' }, h('h3', { text: pd.title }));
    const legendItem = (color, text, wash) => {
      const sw = h('span', { class: 'sw' + (wash ? ' wash' : '') });
      sw.style.background = color;
      return h('span', { class: 'legend-item static' }, sw, text);
    };
    for (const gi of [1, 2, 3, 0]) head.append(legendItem(isDark() ? st.groups[gi].dark : st.groups[gi].color, st.groups[gi].label));
    head.append(legendItem('var(--stool-gap)', st.gapLegend, true));
    box.append(head);
    const width = Math.max(320, ($('#view').clientWidth || 800) - 32), height = 200;
    const left = 48, right = 10, top = 10, bottom = 26, pw = width - left - right, ph = height - top - bottom;
    const svg = s('svg', { class: 'plot', viewBox: `0 0 ${width} ${height}`, role: 'img', 'aria-label': `${pd.title}, ${periodText(new Date(from), new Date(to))}` });
    const xOf = (ms) => left + ((ms - from) / (to - from)) * pw;
    const days = stoolDays(p);
    const inRange = [...days.entries()].filter(([k]) => { const t = parseT(k); return t >= from && t < to; });
    const gaps = stoolGaps(days, st.warnDays).filter((g) => g.b >= from && g.a < to);
    const hits = [];
    for (const g of gaps) {
      const x0 = Math.max(left, xOf(g.a)), x1 = Math.min(left + pw, xOf(g.b + DAY));
      if (x1 <= x0) continue;
      svg.append(s('rect', { class: 'stool-gap', x: x0, y: top, width: x1 - x0, height: ph }));
      if (x1 - x0 > 70) { const t = s('text', { class: 'gap-label', x: (x0 + x1) / 2, y: top + 14, 'text-anchor': 'middle' }); t.textContent = st.gapText.replace('%1', g.n); svg.append(t); }
      hits.push({ x: (x0 + x1) / 2, title: st.gapTip.replace('%1', g.n).replace('%2', fmtDate.format(g.a)).replace('%3', fmtDate.format(g.b)), rows: [] });
    }
    svg.append(s('rect', { class: 'frame', x: left, y: top, width: pw, height: ph }));
    const maxN = Math.max(3, ...inRange.map(([, t]) => t.length));
    const unit = (ph - 4) / maxN;
    const axisY = s('g', { class: 'axis' });
    for (let n = 0; n <= maxN; n += maxN <= 6 ? 1 : 2) {
      const y = top + ph - n * unit;
      if (n) svg.append(s('line', { class: 'gridline', x1: left, x2: left + pw, y1: y, y2: y }));
      const l = s('text', { x: left - 6, y: y + 4, 'text-anchor': 'end' }); l.textContent = `${n}×`; axisY.append(l);
    }
    svg.append(axisY);
    const tt = timeTicks(from, to, pw);
    const axisX = s('g', { class: 'axis' });
    for (const t of tt.ticks) {
      const l = s('text', { x: xOf(t), y: top + ph + 17, 'text-anchor': 'middle' }); l.textContent = tt.fmt.format(t); axisX.append(l);
    }
    svg.append(axisX);
    const dayW = xOf(from + DAY) - xOf(from), gapW = dayW > 8 ? Math.min(dayW * 0.22, 6) : 0;
    for (const [k, types] of inRange) {
      const t0 = parseT(k), x = xOf(t0) + gapW / 2, w = Math.max(1, dayW - gapW);
      types.slice().sort((a, b) => a - b).forEach((ty, i) => {
        svg.append(s('rect', { x, y: top + ph - (i + 1) * unit + (i ? 1 : 0), width: w, height: unit - (i ? 1 : 0), fill: stoolGroupColor(ty) }));
      });
      hits.push({ x: x + w / 2, title: fmtDate.format(t0), rows: [{ color: stoolGroupColor(types[0]), text: `${st.dayTip.replace('%1', types.length)}: ${types.map((t) => st.names[t]).join(', ')}` }] });
    }
    if (!inRange.length && !gaps.length) { const m = s('text', { class: 'msg', x: left + pw / 2, y: top + ph / 2, 'text-anchor': 'middle' }); m.textContent = st.none; svg.append(m); }
    const move = (ev) => {
      const rect = svg.getBoundingClientRect();
      const px = ((ev.clientX - rect.left) / rect.width) * width;
      let best = null, bd = Infinity;
      for (const hp of hits) { const d = Math.abs(hp.x - px) - (hp.rows.length ? 1000 : 0) * (Math.abs(hp.x - px) < dayW ? 1 : 0); if (d < bd) { bd = d; best = hp; } }
      if (!best || Math.abs(best.x - px) > Math.max(30, dayW)) { hideTip(); return; }
      showTip(ev.clientX, ev.clientY, best.title, best.rows);
    };
    svg.addEventListener('pointermove', move);
    svg.addEventListener('pointerdown', move);
    svg.addEventListener('pointerleave', hideTip);
    box.append(svg);
    return box;
  }

  // Ein- und Ausfuhr: je Tag zwei Balken und die Bilanz (Raute); Trinkmengen als Linien
  function fluidChart(pd, p, from, to) {
    const F = state.data.stool.fluid, c = state.chart;
    const din = state.vitals.fluidIn, dout = state.vitals.fluidOut;
    const offIn = c.hidden.has('fluidIn'), offOut = c.hidden.has('fluidOut');
    const box = h('div', { class: 'panel' });
    const head = h('div', { class: 'panel-head' }, h('h3', { text: pd.title }));
    const item = (key, d, off) => {
      const sw = h('span', { class: 'sw' });
      sw.style.background = off ? 'transparent' : d.color;
      head.append(h('button', { type: 'button', class: 'legend-item', 'aria-pressed': off ? 'false' : 'true', title: L.toggleHint,
        onclick: () => { off ? c.hidden.delete(key) : c.hidden.add(key); renderView(); } }, sw, `${d.short} [${d.unit}]`));
    };
    item('fluidIn', din, offIn);
    item('fluidOut', dout, offOut);
    box.append(head);
    const width = Math.max(320, ($('#view').clientWidth || 800) - 32), height = 220;
    const left = 56, right = 10, top = 10, bottom = 26, pw = width - left - right, ph = height - top - bottom;
    const svg = s('svg', { class: 'plot', viewBox: `0 0 ${width} ${height}`, role: 'img', 'aria-label': `${pd.title}, ${periodText(new Date(from), new Date(to))}` });
    const xOf = (ms) => left + ((ms - from) / (to - from)) * pw;
    const days = new Map();
    for (const r of p.rs) {
      if (r.ms < from || r.ms >= to || (r.v.fluidIn === undefined && r.v.fluidOut === undefined)) continue;
      const k = dayKey(r.ms);
      if (!days.has(k)) days.set(k, { in: 0, out: 0, nIn: 0, nOut: 0 });
      const d = days.get(k);
      if (r.v.fluidIn !== undefined) { d.in += r.v.fluidIn; d.nIn++; }
      if (r.v.fluidOut !== undefined) { d.out += r.v.fluidOut; d.nOut++; }
    }
    svg.append(s('rect', { class: 'frame', x: left, y: top, width: pw, height: ph }));
    const tt = timeTicks(from, to, pw);
    const axisX = s('g', { class: 'axis' });
    for (const t of tt.ticks) { const l = s('text', { x: xOf(t), y: top + ph + 17, 'text-anchor': 'middle' }); l.textContent = tt.fmt.format(t); axisX.append(l); }
    svg.append(axisX);
    box.append(svg);
    if (!days.size || (offIn && offOut)) {
      const m = s('text', { class: 'msg', x: left + pw / 2, y: top + ph / 2, 'text-anchor': 'middle' });
      m.textContent = offIn && offOut ? L.allHidden : F.none;
      svg.append(m);
      return box;
    }
    let lo = 0, hi = Math.max(500, p.fluidMin || 0, p.fluidMax || 0);
    for (const d of days.values()) {
      if (!offIn) hi = Math.max(hi, d.in);
      if (!offOut) hi = Math.max(hi, d.out);
      if (!offIn && !offOut) lo = Math.min(lo, d.in - d.out);
    }
    const yt = niceTicks(lo, hi * 1.05, Math.max(3, Math.min(6, Math.floor(ph / 34))));
    const yOf = (v) => top + ph - ((v - yt.lo) / (yt.hi - yt.lo)) * ph;
    const axisY = s('g', { class: 'axis' });
    for (const v of yt.ticks) {
      svg.insertBefore(s('line', { class: 'gridline', x1: left, x2: left + pw, y1: yOf(v), y2: yOf(v) }), axisX);
      const l = s('text', { x: left - 6, y: yOf(v) + 4, 'text-anchor': 'end' }); l.textContent = fmtNum(v, 0); axisY.append(l);
    }
    svg.append(axisY);
    for (const [ml, cls, txt] of [[p.fluidMin, 'limit-min', F.min], [p.fluidMax, 'limit-max', F.max]]) {
      if (!ml || offIn) continue;
      svg.append(s('line', { class: cls, x1: left, x2: left + pw, y1: yOf(ml), y2: yOf(ml) }));
      const l = s('text', { class: 'gap-label', x: left + 6, y: yOf(ml) - 4 }); l.textContent = txt.replace('%1', fmtNum(ml, 0)); svg.append(l);
    }
    const dayW = xOf(from + DAY) - xOf(from), gapW = dayW > 8 ? Math.min(dayW * 0.2, 6) : 0;
    const n = (offIn ? 0 : 1) + (offOut ? 0 : 1), barW = Math.max(1, (dayW - gapW) / Math.max(1, n));
    const hits = [];
    for (const [k, d] of days) {
      const t0 = parseT(k);
      let x = xOf(t0) + gapW / 2;
      const xc = x + (dayW - gapW) / 2;
      if (!offIn) { if (d.nIn) svg.append(s('rect', { x, y: yOf(d.in), width: barW, height: yOf(0) - yOf(d.in), fill: din.color })); x += barW; }
      if (!offOut && d.nOut) svg.append(s('rect', { x, y: yOf(d.out), width: barW, height: yOf(0) - yOf(d.out), fill: dout.color }));
      if (!offIn && !offOut && d.nIn && d.nOut) svg.append(marker('diamond', xc, yOf(d.in - d.out), Math.min(4, dayW * 0.35), 'currentColor', surfaceColor()));
      const rows = [];
      if (d.nIn) rows.push({ color: din.color, text: `${din.label}: ${fmtNum(d.in, 0)} ml` });
      if (d.nOut) rows.push({ color: dout.color, text: `${dout.label}: ${fmtNum(d.out, 0)} ml` });
      if (d.nIn && d.nOut) rows.push({ color: 'transparent', text: `${F.balance}: ${d.in - d.out > 0 ? '+' : ''}${fmtNum(d.in - d.out, 0)} ml` });
      hits.push({ x: xc, title: fmtDate.format(t0), rows });
    }
    const move = (ev) => {
      const rect = svg.getBoundingClientRect();
      const px = ((ev.clientX - rect.left) / rect.width) * width;
      let best = null, bd = Infinity;
      for (const hp of hits) { const d = Math.abs(hp.x - px); if (d < bd) { bd = d; best = hp; } }
      if (!best || bd > Math.max(30, dayW)) { hideTip(); return; }
      showTip(ev.clientX, ev.clientY, best.title, best.rows);
    };
    svg.addEventListener('pointermove', move);
    svg.addEventListener('pointerdown', move);
    svg.addEventListener('pointerleave', hideTip);
    return box;
  }

  function panelChart(pd, p, from, to) {
    if (pd.id === 'stool') return stoolChart(pd, p, from, to);
    if (pd.id === 'fluid') return fluidChart(pd, p, from, to);
    const c = state.chart;
    const agg = to - from > 62 * DAY;
    const box = h('div', { class: 'panel' });
    const head = h('div', { class: 'panel-head' }, h('h3', { text: pd.title }));
    for (const k of pd.keys) {
      const d = state.vitals[k];
      const off = c.hidden.has(k);
      const sw = s('svg', { viewBox: '0 0 26 12', 'aria-hidden': 'true' });
      sw.append(s('line', { x1: 1, y1: 6, x2: 25, y2: 6, stroke: off ? 'currentColor' : d.color, 'stroke-width': 2, 'stroke-dasharray': off ? '2 3' : null }));
      if (!off) sw.append(marker(d.marker, 13, 6, 4, d.color, surfaceColor()));
      head.append(h('button', { type: 'button', class: 'legend-item', 'aria-pressed': off ? 'false' : 'true', title: L.toggleHint,
        onclick: () => { off ? c.hidden.delete(k) : c.hidden.add(k); renderView(); } }, sw, d.unit ? `${d.short} [${d.unit}]` : d.short));
    }
    if (agg) head.append(h('span', { class: 'panel-note', text: L.dailyNote }));
    box.append(head);

    const width = Math.max(320, ($('#view').clientWidth || 800) - 32);
    const height = 230;
    const left = 48, right = 10, top = 10, bottom = 26;
    const pw = width - left - right, ph = height - top - bottom;
    const svg = s('svg', { class: 'plot', viewBox: `0 0 ${width} ${height}`, role: 'img', 'aria-label': `${pd.title}, ${periodText(new Date(from), new Date(to))}` });
    svg.append(s('rect', { class: 'frame', x: left, y: top, width: pw, height: ph }));
    const xOf = (ms) => left + ((ms - from) / (to - from)) * pw;

    const series = [];
    let lo = Infinity, hi = -Infinity;
    for (const k of pd.keys) {
      if (c.hidden.has(k)) continue;
      const raw = p.rs.filter((r) => r.ms >= from && r.ms < to && r.v[k] !== undefined).map((r) => ({ ms: r.ms, v: r.v[k], lo: r.v[k], hi: r.v[k], n: 1, r }));
      const pts = agg ? daily(raw) : raw;
      for (const q of pts) { lo = Math.min(lo, q.lo); hi = Math.max(hi, q.hi); }
      series.push({ k, def: state.vitals[k], pts });
    }
    const tt = timeTicks(from, to, pw);
    for (const t of tt.daySeps) svg.append(s('line', { class: 'daysep', x1: xOf(t), x2: xOf(t), y1: top, y2: top + ph }));
    const axisX = s('g', { class: 'axis' });
    for (const t of tt.ticks) {
      const x = xOf(t);
      svg.append(s('line', { class: 'gridline', x1: x, x2: x, y1: top, y2: top + ph }));
      const label = s('text', { x, y: top + ph + 17, 'text-anchor': 'middle' });
      label.textContent = tt.fmt.format(t);
      axisX.append(label);
    }
    svg.append(axisX);
    box.append(svg);
    if (!series.length || !(hi >= lo)) {
      const m = s('text', { class: 'msg', x: left + pw / 2, y: top + ph / 2, 'text-anchor': 'middle' });
      m.textContent = series.length ? L.noValues : L.allHidden;
      svg.append(m);
      return box;
    }
    const band = pd.id === 'inr' && p.inr ? p.inr : null;     // INR-Zielbereich
    if (band) { lo = Math.min(lo, band[0]); hi = Math.max(hi, band[1]); }
    if (pd.id === 'edema') { lo = 0; hi = 4; }
    const pad = pd.id === 'edema' ? 0 : (hi - lo) * 0.08;
    const yt = niceTicks(lo - pad, hi + pad, Math.max(3, Math.min(6, Math.floor(ph / 34))));
    const yOf = (v) => top + ph - ((v - yt.lo) / (yt.hi - yt.lo)) * ph;
    if (band) {
      svg.insertBefore(s('rect', { class: 'inr-band', x: left, y: yOf(band[1]), width: pw, height: yOf(band[0]) - yOf(band[1]) }), axisX);
      const bt = s('text', { class: 'gap-label', x: left + pw - 6, y: yOf(band[1]) + 13, 'text-anchor': 'end' });
      bt.textContent = state.data.stool.fluid.target.replace('%1', fmtNum(band[0], 1)).replace('%2', fmtNum(band[1], 1));
      svg.insertBefore(bt, axisX);
    }
    const dec = yt.step < 0.1 ? 2 : yt.step < 1 ? 1 : 0;
    const axisY = s('g', { class: 'axis' });
    for (const v of yt.ticks) {
      const y = yOf(v);
      svg.insertBefore(s('line', { class: 'gridline', x1: left, x2: left + pw, y1: y, y2: y }), axisX);
      const label = s('text', { x: left - 6, y: y + 4, 'text-anchor': 'end' });
      label.textContent = fmtNum(v, dec);
      axisY.append(label);
    }
    svg.append(axisY);
    const ring = surfaceColor();
    const plotG = s('g', {});
    const hits = [];
    for (const { def, pts } of series) {
      if (agg)
        for (const q of pts) if (q.n > 1 && q.hi > q.lo)
          plotG.append(s('line', { x1: xOf(q.ms), x2: xOf(q.ms), y1: yOf(q.lo), y2: yOf(q.hi), stroke: def.color, 'stroke-opacity': 0.45, 'stroke-width': 1.5 }));
      let d = '';
      pts.forEach((q, i) => {
        const gap = i && agg && q.ms - pts[i - 1].ms > 7 * DAY;
        d += `${i === 0 || gap ? 'M' : 'L'}${xOf(q.ms).toFixed(1)},${yOf(q.v).toFixed(1)}`;
      });
      if (pts.length > 1) plotG.append(s('path', { d, fill: 'none', stroke: def.color, 'stroke-width': 2, 'stroke-linejoin': 'round', 'stroke-linecap': 'round' }));
      const dense = pts.length > 150;
      for (const q of pts) {
        const x = xOf(q.ms), y = yOf(q.v);
        plotG.append(marker(def.marker, x, y, dense ? 2.2 : 4, def.color, dense ? null : ring));
        hits.push({ x, y, q, def });
      }
    }
    svg.append(plotG);

    // Hover: Fadenkreuz und Tooltip zum nächstgelegenen Punkt
    const cross = s('line', { class: 'cross', y1: top, y2: top + ph, visibility: 'hidden' });
    svg.append(cross);
    const move = (ev) => {
      const rect = svg.getBoundingClientRect();
      const px = ((ev.clientX - rect.left) / rect.width) * width, py = ((ev.clientY - rect.top) / rect.height) * height;
      let best = null, bd = Infinity;
      for (const hp of hits) {
        const dd = (hp.x - px) ** 2 + ((hp.y - py) ** 2) * 0.25;
        if (dd < bd) { bd = dd; best = hp; }
      }
      if (!best || Math.abs(best.x - px) > 40) { hideTip(); cross.setAttribute('visibility', 'hidden'); return; }
      cross.setAttribute('x1', best.x); cross.setAttribute('x2', best.x); cross.setAttribute('visibility', 'visible');
      const rows = [];
      const sameTime = hits.filter((hp) => hp.q.ms === best.q.ms);
      for (const hp of (sameTime.length ? sameTime : [best])) {
        const dcm = hp.def.decimals;
        const val = hp.q.n > 1 ? L.avg(fmtNum(hp.q.v, dcm), fmtNum(hp.q.lo, dcm), fmtNum(hp.q.hi, dcm), hp.q.n) : fmtNum(hp.q.v, dcm);
        rows.push({ color: hp.def.color, text: `${hp.def.label}: ${val} ${hp.def.unit}` });
      }
      const r = best.q.r;
      const title = agg ? fmtDate.format(best.q.ms) : fmtDateTime.format(best.q.ms) + (r && r.ctx ? `, ${r.ctx}` : '');
      showTip(ev.clientX, ev.clientY, title, rows, r && r.obs);
    };
    svg.addEventListener('pointermove', move);
    svg.addEventListener('pointerdown', move);
    svg.addEventListener('pointerleave', () => { hideTip(); cross.setAttribute('visibility', 'hidden'); });
    return box;
  }

  function showTip(x, y, title, rows, note) {
    const tip = $('#tooltip');
    tip.replaceChildren(h('div', { class: 't', text: title }));
    for (const r of rows) {
      const sw = h('span', { class: 'sw' });
      sw.style.background = r.color;
      tip.append(h('div', { class: 'row' }, sw, r.text));
    }
    if (note) tip.append(h('div', { class: 't', text: note }));
    tip.hidden = false;
    const w = tip.offsetWidth, hh = tip.offsetHeight;
    tip.style.left = `${Math.min(innerWidth - w - 8, x + 14)}px`;
    tip.style.top = `${y + 18 + hh > innerHeight ? y - hh - 12 : y + 18}px`;
  }
  function hideTip() { const t = $('#tooltip'); if (t) t.hidden = true; }

  // ---------------------------------------------------------------- Messwerte (Tabelle)
  function journalRows(p) {
    const j = state.journal;
    let rows = p.rs;
    const now = Date.now();
    if (j.range !== 'all') rows = rows.filter((r) => r.ms >= now - Number(j.range) * DAY);
    if (j.query) {
      const q = j.query.toLowerCase();
      rows = rows.filter((r) => (r.obs || '').toLowerCase().includes(q) || (r.ctx || '').toLowerCase().includes(q)
        || Object.values(r.x || {}).some((v) => String(v).toLowerCase().includes(q)));
    }
    return rows.slice().reverse();
  }

  function journalColumns(p) {
    const keys = state.data.vitals.map((v) => v.key).filter((k) => p.rs.some((r) => r.v[k] !== undefined));
    const extra = [...new Set(p.rs.flatMap((r) => Object.keys(r.x || {})))];
    return { keys, extra };
  }

  function journal(view, p) {
    const j = state.journal;
    const bar = h('div', { class: 'toolbar' });
    const range = h('select', { 'aria-label': L.range });
    for (const [v, t] of [['all', L.all], ['30', L.lastDays(30)], ['90', L.lastDays(90)], ['365', L.lastYear]])
      range.append(h('option', { value: v, text: t, selected: j.range === v }));
    range.addEventListener('change', () => { j.range = range.value; j.shown = 200; renderView(); });
    const search = h('input', { type: 'search', placeholder: L.searchPh, 'aria-label': L.search, value: j.query });
    let st = null;
    search.addEventListener('input', () => { clearTimeout(st); st = setTimeout(() => { j.query = search.value.trim(); j.shown = 200; renderView(); $('input[type=search]')?.focus(); }, 250); });
    const rows = journalRows(p);
    bar.append(range, search, h('span', { class: 'grow hint', text: L.count(rows.length) }),
      h('button', { type: 'button', class: 'ghost', text: L.csv, onclick: () => downloadCsv(p, rows) }),
      h('button', { type: 'button', class: 'ghost', text: L.print, onclick: () => print() }));
    view.append(bar);
    if (!rows.length) { view.append(h('p', { class: 'empty', text: p.rs.length ? L.none : L.noReadings })); return; }
    const { keys, extra } = journalColumns(p);
    const thead = h('tr', {}, h('th', { text: L.when }), h('th', { text: L.context }),
      keys.map((k) => h('th', { class: k === 'stool' ? null : 'n', title: state.vitals[k].label }, state.vitals[k].short, h('small', { text: state.vitals[k].unit }))),
      extra.map((x) => h('th', { text: x })), h('th', { text: L.observations }));
    const tbody = h('tbody');
    for (const r of rows.slice(0, j.shown)) {
      tbody.append(h('tr', { class: r.adm ? 'adm' : null },
        h('td', { class: 'when num', text: fmtDateTime.format(r.ms) }),
        h('td', { text: r.adm ? L.admission : r.ctx || '' }),
        keys.map((k) => h('td', { class: k === 'stool' ? null : 'n', text: k === 'stool' ? stoolText(r) : k === 'edema' ? edemaText(r) : r.v[k] !== undefined ? fmtNum(r.v[k], state.vitals[k].decimals) : '' })),
        extra.map((x) => h('td', { text: r.x && r.x[x] !== undefined ? String(r.x[x]) : '' })),
        h('td', { class: 'obs', text: r.obs || '' })));
    }
    view.append(h('div', { class: 'tablewrap' }, h('table', {}, h('thead', {}, thead), tbody)));
    if (rows.length > j.shown)
      view.append(h('p', {}, h('button', { type: 'button', class: 'linkbtn noprint', text: L.more(Math.min(200, rows.length - j.shown)),
        onclick: () => { j.shown += 200; renderView(); } })));
  }

  function downloadCsv(p, rows) {
    const de = L === TEXT.de;
    const sep = de ? ';' : ',';
    const { keys, extra } = journalColumns(p);
    const q = (v) => { const t = String(v ?? ''); return /[";\n,]/.test(t) ? `"${t.replace(/"/g, '""')}"` : t; };
    const num = (v, d) => (v === undefined ? '' : de ? v.toFixed(d).replace('.', ',') : v.toFixed(d));
    const lines = [[L.when, L.context, ...keys.map((k) => `${state.vitals[k].label} [${state.vitals[k].unit}]`), ...extra, L.observations].map(q).join(sep)];
    for (const r of rows.slice().reverse()) {
      const d = new Date(r.ms);
      const stamp = `${isoDate(d)} ${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
      lines.push([stamp, r.adm ? L.admission : r.ctx || '', ...keys.map((k) => (k === 'stool' ? stoolText(r) : k === 'edema' ? edemaText(r) : num(r.v[k], state.vitals[k].decimals))),
        ...extra.map((x) => (r.x ? r.x[x] : '')), r.obs || ''].map(q).join(sep));
    }
    download(`${p.name.replace(/[\\/:*?"<>|]+/g, '_')} – ${L.tabs.journal}.csv`, new Blob(['﻿' + lines.join('\r\n')], { type: 'text/csv;charset=utf-8' }));
  }

  // ---------------------------------------------------------------- Medikamente
  function meds(view, p) {
    const cb = h('input', { type: 'checkbox', id: 'archToggle', checked: state.meds.archived });
    cb.addEventListener('change', () => { state.meds.archived = cb.checked; renderView(); });
    view.append(h('div', { class: 'toolbar' }, cb, h('label', { for: 'archToggle', text: L.showArchived }),
      h('span', { class: 'grow' }), h('button', { type: 'button', class: 'ghost', text: L.print, onclick: () => print() })));
    const cats = state.data.medCategories;
    let any = false;
    for (let ci = 0; ci < cats.length; ci++) {
      const list = p.medications.filter((m) => m.category === ci && (state.meds.archived || !m.archived));
      if (!list.length) continue;
      any = true;
      const tbody = h('tbody');
      for (const m of list)
        tbody.append(h('tr', { class: m.archived ? 'archived' : null },
          h('td', {}, h('strong', { text: m.name }), m.strength && !m.name.includes(m.strength) ? ` ${m.strength}` : '', m.form ? h('div', { class: 'hint', text: m.form }) : null,
            m.archived ? h('div', { class: 'hint', text: `${L.archived}${m.end ? ' ' + fmtDate.format(parseD(m.end)) : ''}` }) : null),
          h('td', { text: m.substance || '' }), h('td', { class: 'num', text: `${m.dose} ${m.unit || ''}` }),
          h('td', { text: m.schedule || '' }), h('td', { text: m.reason || '' }), h('td', { text: m.notes || '' }),
          h('td', { class: 'num', text: m.start ? fmtDate.format(parseD(m.start)) : '' })));
      view.append(h('section', { class: `medcat cat-${ci}` }, h('h2', {}, cats[ci], h('small', { text: String(list.length) })),
        h('div', { class: 'tablewrap' }, h('table', {}, h('thead', {}, h('tr', {}, L.medCols.map((t) => h('th', { text: t })))), tbody))));
    }
    if (!any) view.append(h('p', { class: 'empty', text: L.none }));
  }

  // ---------------------------------------------------------------- Termine
  function apptList(list) {
    const ul = h('ul', { class: 'list' });
    for (const a of list) {
      const ms = parseT(a.start);
      ul.append(h('li', { class: `prio-${a.priority}${a.done ? ' done' : ''}` },
        h('span', {}, fmtDate.format(ms), h('div', { class: 'sub', text: a.allDay ? L.allDay : fmtTime.format(ms) })),
        h('span', {}, h('span', { class: 'what', text: a.title }), a.location ? h('div', { class: 'sub', text: a.location }) : null,
          a.notes ? h('div', { class: 'sub', text: a.notes }) : null)));
    }
    return ul;
  }

  function appts(view, p) {
    const today = startOfDay(new Date()).getTime();
    const up = p.appointments.filter((a) => parseT(a.start) >= today && !a.done);
    const past = p.appointments.filter((a) => parseT(a.start) < today || a.done).reverse();
    view.append(h('h2', { text: L.upcoming }), up.length ? apptList(up) : h('p', { class: 'empty', text: L.none }));
    view.append(h('h2', { text: L.past }), past.length ? apptList(state.appts.allPast ? past : past.slice(0, 15)) : h('p', { class: 'empty', text: L.none }));
    if (past.length > 15 && !state.appts.allPast)
      view.append(h('p', {}, h('button', { type: 'button', class: 'linkbtn', text: L.showAll, onclick: () => { state.appts.allPast = true; renderView(); } })));
  }

  // ---------------------------------------------------------------- Berichte
  function reports(view, p) {
    view.append(h('p', { class: 'hint', text: L.reportsIntro }));
    const ul = h('ul', { class: 'reports' });
    const kb = (n) => (n > 1024 * 1024 ? `${fmtNum(n / 1048576, 1)} MB` : `${fmtNum(Math.max(1, n / 1024))} KB`);
    for (const r of p.reports || []) {
      const btn = h('button', { type: 'button', class: 'primary', text: L.download });
      btn.addEventListener('click', async () => {
        btn.disabled = true;
        try { download(r.name, new Blob([await fetchFile(r.file)], { type: r.mime })); }
        catch (e) { alert(L.loadError + e.message); }
        finally { btn.disabled = false; }
      });
      ul.append(h('li', {}, h('div', { class: 'grow' }, h('div', { text: r.title }), h('div', { class: 'type', text: `${r.name}, ${kb(r.size)}` })), btn));
    }
    const csv = h('button', { type: 'button', class: 'ghost', text: L.download, onclick: () => downloadCsv(p, p.rs.slice().reverse()) });
    ul.append(h('li', {}, h('div', { class: 'grow' }, h('div', { text: L.csvReport }), h('div', { class: 'type', text: L.csvType })), csv));
    view.append(ul);
  }

  init().catch((e) => loginMessage(L.loadError + e.message));
})();
