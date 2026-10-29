<?php
// VitaParms – Zugriffsprotokoll der Website
//
// Hält fest, wer (Name bei der Anmeldung), wann (Datum und Uhrzeit des Servers) und von wo
// (IP-Adresse) auf die Website zugegriffen hat. Das Protokoll liegt nur auf dem Server
// (data/.access-log.php, von außen nicht abrufbar) und wird über das Web-Interface angezeigt
// und heruntergeladen.
//
//   POST log.php        Rumpf {event, user, detail?}, Kopf "X-VitaParms-Access" (außer bei
//                       „login-failed“) → {ok}
//   GET  log.php        Kopf "X-VitaParms-Access" → {ok, entries: [...]} (neueste zuerst)
// Zeitangaben mit Zeitzone (ISO 8601); der Browser zeigt sie in Ortszeit an und erzeugt daraus
// auch die CSV-Datei zum Herunterladen.
//
// Berechtigung: Der Zugriffswert wird im Browser aus dem Master-Passwort abgeleitet
// (HMAC-SHA256 mit dem MAC-Schlüssel über "vitaparms-access-log"). VitaParms legt beim Hochladen
// dessen SHA-256 als Prüfwert ab (data/.access.php). Ohne das Passwort lässt sich das Protokoll
// also weder lesen noch mit bestätigten Einträgen füllen. Fehlgeschlagene Anmeldungen werden
// ohne Zugriffswert angenommen (je IP höchstens 20 pro Stunde).
// SPDX-License-Identifier: MIT

require __DIR__ . '/lib.php';

const LOG_FILE     = DATA_DIR . '/.access-log.php';
const LOG_OLD      = DATA_DIR . '/.access-log.1.php';
const ACCESS_FILE  = DATA_DIR . '/.access.php';
const LOG_GUARD    = "<?php http_response_code(404); exit; ?>\n";
const LOG_MAX      = 1024 * 1024;     // danach wird das Protokoll umbenannt (eine ältere Datei bleibt)
const FAILED_LIMIT = 20;              // fehlgeschlagene Anmeldungen je IP und Stunde
const EVENTS       = ['login', 'login-failed', 'logout', 'idle-logout', 'report', 'csv', 'log-view', 'log-download'];

function accessHash(): ?string
{
    if (!is_file(ACCESS_FILE)) {
        return null;
    }
    return preg_match('/^#([0-9a-f]{64})$/m', (string) file_get_contents(ACCESS_FILE), $m) ? $m[1] : null;
}

// true: gültiger Zugriffswert; false: falscher Wert; null: Website noch ohne Prüfwert
function accessValid(): ?bool
{
    $hash = accessHash();
    if ($hash === null) {
        return null;
    }
    $token = strtolower(trim((string) ($_SERVER['HTTP_X_VITAPARMS_ACCESS'] ?? '')));
    if (!preg_match('/^[0-9a-f]{64}$/', $token)) {
        return false;
    }
    return hash_equals($hash, hash('sha256', (string) hex2bin($token)));
}

function requireAccess(): void
{
    $ok = accessValid();
    if ($ok === null) {
        fail(409, 'Das Zugriffsprotokoll ist erst verfügbar, wenn VitaParms die Website einmal aktualisiert hat.',
                  'The access log is only available after VitaParms has updated the website once.');
    }
    if (!$ok) {
        usleep(random_int(300000, 900000));
        fail(403, 'Keine Berechtigung für das Zugriffsprotokoll.', 'Not authorised to view the access log.');
    }
}

function clientIp(): array
{
    $ip = (string) ($_SERVER['REMOTE_ADDR'] ?? '');
    $fwd = trim(explode(',', (string) ($_SERVER['HTTP_X_FORWARDED_FOR'] ?? ''))[0]);
    // Hinter einem Proxy des Providers steht die Adresse des Besuchers in X-Forwarded-For.
    // Die Angabe kann gefälscht sein – sie wird deshalb nur zusätzlich vermerkt.
    if ($fwd !== '' && filter_var($fwd, FILTER_VALIDATE_IP) && $fwd !== $ip) {
        return [$ip, $fwd];
    }
    return [$ip, ''];
}

function clean(string $s, int $max): string
{
    $s = preg_replace('/[\x00-\x1F\x7F]+/u', ' ', $s) ?? '';
    $s = trim($s);
    return function_exists('mb_substr') ? mb_substr($s, 0, $max) : substr($s, 0, $max);
}

function readEntries(string $file): array
{
    if (!is_file($file)) {
        return [];
    }
    $out = [];
    foreach (file($file, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) ?: [] as $line) {
        if ($line === '' || $line[0] !== '{') {
            continue;
        }
        $e = json_decode($line, true);
        if (is_array($e)) {
            $out[] = $e;
        }
    }
    return $out;
}

function allEntries(): array
{
    return array_merge(readEntries(LOG_OLD), readEntries(LOG_FILE));     // älteste zuerst
}

function writeEntry(array $entry): void
{
    ensureDataDir();
    $fh = fopen(LOG_FILE, 'c+');
    if ($fh === false || !flock($fh, LOCK_EX)) {
        fail(500, 'Das Zugriffsprotokoll lässt sich nicht schreiben.', 'The access log cannot be written.');
    }
    clearstatcache(true, LOG_FILE);
    $size = filesize(LOG_FILE) ?: 0;
    if ($size > LOG_MAX) {                           // umbenennen, neu beginnen
        @copy(LOG_FILE, LOG_OLD);
        @chmod(LOG_OLD, 0600);
        ftruncate($fh, 0);
        $size = 0;
    }
    if ($size === 0) {
        fwrite($fh, LOG_GUARD);
        @chmod(LOG_FILE, 0600);
    }
    fseek($fh, 0, SEEK_END);
    fwrite($fh, json_encode($entry, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES) . "\n");
    fflush($fh);
    flock($fh, LOCK_UN);
    fclose($fh);
}

function tooManyFailures(string $ip): bool
{
    $since = time() - 3600;
    $n = 0;
    foreach (readEntries(LOG_FILE) as $e) {
        if (($e['event'] ?? '') === 'login-failed' && ($e['ip'] ?? '') === $ip && strtotime((string) ($e['time'] ?? '')) >= $since) {
            ++$n;
        }
    }
    return $n >= FAILED_LIMIT;
}

// ------------------------------------------------------------------ Eintrag schreiben
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    if (!is_file(DATA_DIR . '/current.json')) {
        fail(409, 'Die Website ist noch nicht eingerichtet.', 'The website has not been set up yet.');
    }
    $body = json_decode((string) file_get_contents('php://input', false, null, 0, 4096), true);
    $event = is_array($body) ? (string) ($body['event'] ?? '') : '';
    if (!in_array($event, EVENTS, true)) {
        fail(400, 'Unbekanntes Ereignis.', 'Unknown event.');
    }
    [$ip, $fwd] = clientIp();
    $verified = accessValid();
    if ($event === 'login-failed') {
        if (tooManyFailures($ip)) {
            reply(200, ['ok' => true, 'skipped' => true]);
        }
    } elseif ($verified === false) {
        usleep(random_int(300000, 900000));
        fail(403, 'Keine Berechtigung.', 'Not authorised.');
    }
    $entry = [
        'time'  => date('c'),
        'user'  => clean((string) ($body['user'] ?? ''), 60),
        'ip'    => $ip,
        'event' => $event,
    ];
    if ($fwd !== '') {
        $entry['fwd'] = $fwd;
    }
    $remote = clean((string) ($_SERVER['REMOTE_USER'] ?? $_SERVER['PHP_AUTH_USER'] ?? ''), 60);
    if ($remote !== '') {
        $entry['auth'] = $remote;                    // Anmeldename des Webservers (HTTP-Authentifizierung)
    }
    $detail = clean((string) ($body['detail'] ?? ''), 120);
    if ($detail !== '') {
        $entry['detail'] = $detail;
    }
    if ($verified === null && $event !== 'login-failed') {
        $entry['unverified'] = true;                 // Website noch ohne Prüfwert
    }
    writeEntry($entry);
    reply(200, ['ok' => true]);
}

if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    fail(405, 'Nur GET und POST sind erlaubt.', 'Only GET and POST are allowed.');
}

// ------------------------------------------------------------------ Anzeigen bzw. herunterladen
requireAccess();
$entries = array_reverse(allEntries());              // neueste zuerst

reply(200, ['ok' => true, 'entries' => $entries, 'tz' => date_default_timezone_get(), 'now' => date('c')]);
