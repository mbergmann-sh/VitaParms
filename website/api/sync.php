<?php
// VitaParms – Datenbank-Abgleich zwischen Geräten (Desktop, Tablet)
//
// Der Server verwahrt nur die verschlüsselte Datenbank (SQLite3 Multiple Ciphers) und die
// verschlüsselte Schlüsseldatei der Anmeldekonten. Zusammengeführt wird auf den Geräten;
// der Server zählt lediglich eine Revision hoch und lehnt veraltete Uploads ab.
//
//   GET  sync.php?state=1      → {rev, size, keysHash, updated}
//   GET  sync.php?get=db       → Datenbank (application/octet-stream)
//   GET  sync.php?get=keys     → Schlüsseldatei
//   POST sync.php  base=<rev>, Dateien "db" und "keys"
//        → 200 {rev}  bzw. 409, wenn inzwischen ein anderes Gerät hochgeladen hat
// Alle Aufrufe brauchen den Kopf "X-VitaParms-Key" (Upload-Schlüssel der Website).
// SPDX-License-Identifier: MIT

require __DIR__ . '/lib.php';

const STATE_FILE = SYNC_DIR . '/state.json';
const DB_FILE    = SYNC_DIR . '/db.vpdb';
const KEYS_FILE  = SYNC_DIR . '/keys.vpk';

function syncState(): array
{
    if (!is_file(STATE_FILE)) {
        return ['rev' => 0, 'size' => 0, 'keysHash' => '', 'updated' => null];
    }
    $s = json_decode((string) file_get_contents(STATE_FILE), true);
    return is_array($s) ? $s + ['rev' => 0, 'size' => 0, 'keysHash' => '', 'updated' => null]
                        : ['rev' => 0, 'size' => 0, 'keysHash' => '', 'updated' => null];
}

function ensureSyncDir(): void
{
    if (!is_dir(SYNC_DIR) && !@mkdir(SYNC_DIR, 0755, true)) {
        fail(500, 'Der Ordner sync/ fehlt und lässt sich nicht anlegen.', 'The folder sync/ is missing and cannot be created.');
    }
    if (!is_writable(SYNC_DIR)) {
        fail(500, 'Der Ordner sync/ ist nicht beschreibbar (Rechte 755 oder 775 setzen).',
                  'The folder sync/ is not writable (set permissions to 755 or 775).');
    }
    if (!is_file(SYNC_DIR . '/.htaccess')) {
        file_put_contents(SYNC_DIR . '/.htaccess', "Require all denied\n");
    }
}

function sendFile(string $path, string $name): void
{
    if (!is_file($path)) {
        fail(404, 'Auf dem Server liegt noch keine Datenbank.', 'There is no database on the server yet.');
    }
    header('Content-Type: application/octet-stream');
    header('Content-Length: ' . filesize($path));
    header('Content-Disposition: attachment; filename="' . $name . '"');
    readfile($path);
    exit;
}

// ------------------------------------------------------------------ Abrufen
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    if (isset($_GET['state']) && storedHash() === null) {
        // Noch nicht eingerichtet: der erste Upload übernimmt den Schlüssel
        reply(200, ['ok' => true, 'claimed' => false] + syncState());
    }
    authorize(false);
    // Lesesperre: nicht mitten in einen Upload hinein lesen
    $rlock = is_file(LOCK_FILE) ? fopen(LOCK_FILE, 'r') : false;
    if ($rlock !== false) {
        flock($rlock, LOCK_SH);
    }
    if (isset($_GET['state'])) {
        reply(200, ['ok' => true, 'claimed' => true] + syncState() + ['maxUpload' => ini_get('upload_max_filesize'),
                                                   'maxPost' => ini_get('post_max_size')]);
    }
    $what = (string) ($_GET['get'] ?? '');
    if ($what === 'db') {
        sendFile(DB_FILE, 'vitaparms.vpdb');
    }
    if ($what === 'keys') {
        sendFile(KEYS_FILE, 'vitaparms.vpk');
    }
    fail(400, 'Unbekannte Anfrage.', 'Unknown request.');
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    fail(405, 'Nur GET und POST sind erlaubt.', 'Only GET and POST are allowed.');
}

// ------------------------------------------------------------------ Hochladen
if (empty($_POST) && empty($_FILES) && (int) ($_SERVER['CONTENT_LENGTH'] ?? 0) > 0) {
    fail(413, 'Die Datenbank ist größer als post_max_size (' . ini_get('post_max_size') . ') des Servers.',
              'The database exceeds the server\'s post_max_size (' . ini_get('post_max_size') . ').');
}
$lock = lockSite();
authorize(true);
ensureSyncDir();

$state = syncState();
$base = (int) ($_POST['base'] ?? -1);
if ($base !== (int) $state['rev']) {
    reply(409, ['ok' => false, 'rev' => (int) $state['rev'],
                'error' => t('Ein anderes Gerät hat inzwischen abgeglichen.', 'Another device has synchronised in the meantime.')]);
}
foreach (['db', 'keys'] as $f) {
    if (!isset($_FILES[$f]) || $_FILES[$f]['error'] !== UPLOAD_ERR_OK) {
        fail(400, "Datei '$f' fehlt. " . (isset($_FILES[$f]) ? uploadError($_FILES[$f]['error']) : ''),
                  "File '$f' is missing. " . (isset($_FILES[$f]) ? uploadError($_FILES[$f]['error']) : ''));
    }
}
// Unverschlüsselte Datenbanken werden abgelehnt
$head = (string) file_get_contents($_FILES['db']['tmp_name'], false, null, 0, 16);
if (strncmp($head, 'SQLite format 3', 15) === 0) {
    fail(400, 'Die Datenbank ist nicht verschlüsselt.', 'The database is not encrypted.');
}
$keysHash = hash_file('sha256', $_FILES['keys']['tmp_name']);
if (!move_uploaded_file($_FILES['db']['tmp_name'], DB_FILE . '.tmp')
    || !move_uploaded_file($_FILES['keys']['tmp_name'], KEYS_FILE . '.tmp')) {
    fail(500, 'Die Dateien lassen sich nicht speichern.', 'The files cannot be saved.');
}
if (!rename(DB_FILE . '.tmp', DB_FILE) || !rename(KEYS_FILE . '.tmp', KEYS_FILE)) {
    fail(500, 'Die Dateien lassen sich nicht aktivieren.', 'The files cannot be activated.');
}
$new = ['rev' => (int) $state['rev'] + 1, 'size' => filesize(DB_FILE), 'keysHash' => $keysHash,
        'updated' => gmdate('c')];
file_put_contents(STATE_FILE . '.tmp', json_encode($new, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));
rename(STATE_FILE . '.tmp', STATE_FILE);
flock($lock, LOCK_UN);
reply(200, ['ok' => true] + $new + ['message' => t('Datenbank abgeglichen.', 'Database synchronised.')]);
