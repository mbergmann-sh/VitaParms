<?php
// VitaParms – Inhalte der Krankenakte beim Datenbank-Abgleich (getrennt von der Datenbank)
//
// Die Geräte laden Dokumentinhalte einzeln hoch und holen nur, was ihnen fehlt. Jeder Inhalt ist
// auf dem Gerät verschlüsselt (AES-256, Schlüssel aus dem Datenbankschlüssel); der Name ist eine
// zufällig wirkende Kennung (64 Hex-Zeichen), die nichts über das Dokument verrät.
//
//   GET  files.php?list=1          → {ok, files: [Kennungen]}
//   GET  files.php?get=<Kennung>   → Inhalt (application/octet-stream)
//   POST files.php?put=<Kennung>   Rumpf = verschlüsselter Inhalt → {ok}
//   POST files.php?prune=1         Rumpf = {"keep": [Kennungen]} → löscht nicht mehr gebrauchte
//                                  Inhalte, die älter als einen Tag sind → {ok, removed}
// Alle Aufrufe brauchen den Kopf "X-VitaParms-Key" (Upload-Schlüssel der Website).
// SPDX-License-Identifier: MIT

require __DIR__ . '/lib.php';

const FILES_DIR = SYNC_DIR . '/files';

function validId(string $id): bool
{
    return preg_match('/^[0-9a-f]{64}$/', $id) === 1;
}

function ensureFilesDir(): void
{
    if (!is_dir(SYNC_DIR) && !@mkdir(SYNC_DIR, 0755, true)) {
        fail(500, 'Der Ordner sync/ fehlt und lässt sich nicht anlegen.', 'The folder sync/ is missing and cannot be created.');
    }
    if (!is_file(SYNC_DIR . '/.htaccess')) {
        file_put_contents(SYNC_DIR . '/.htaccess', "Require all denied\n");
    }
    if (!is_dir(FILES_DIR) && !@mkdir(FILES_DIR, 0755, true)) {
        fail(500, 'Der Ordner sync/files/ lässt sich nicht anlegen.', 'The folder sync/files/ cannot be created.');
    }
    if (!is_writable(FILES_DIR)) {
        fail(500, 'Der Ordner sync/files/ ist nicht beschreibbar (Rechte 755 oder 775 setzen).',
                  'The folder sync/files/ is not writable (set permissions to 755 or 775).');
    }
}

if (storedHash() === null) {
    fail(403, 'Die Website ist noch nicht eingerichtet.', 'The website has not been set up yet.');
}
authorize(false);

if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    if (isset($_GET['list'])) {
        $ids = [];
        if (is_dir(FILES_DIR)) {
            foreach (scandir(FILES_DIR) ?: [] as $f) {
                if (preg_match('/^([0-9a-f]{64})\.bin$/', $f, $m)) {
                    $ids[] = $m[1];
                }
            }
        }
        reply(200, ['ok' => true, 'files' => $ids]);
    }
    $id = (string) ($_GET['get'] ?? '');
    if (!validId($id)) {
        fail(400, 'Ungültige Kennung.', 'Invalid identifier.');
    }
    $path = FILES_DIR . '/' . $id . '.bin';
    if (!is_file($path)) {
        fail(404, 'Dieser Inhalt liegt nicht auf dem Server.', 'This content is not on the server.');
    }
    header('Content-Type: application/octet-stream');
    header('Content-Length: ' . filesize($path));
    readfile($path);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    fail(405, 'Nur GET und POST sind erlaubt.', 'Only GET and POST are allowed.');
}
ensureFilesDir();

if (isset($_GET['put'])) {
    $id = (string) $_GET['put'];
    if (!validId($id)) {
        fail(400, 'Ungültige Kennung.', 'Invalid identifier.');
    }
    $in = fopen('php://input', 'rb');
    $tmp = FILES_DIR . '/' . $id . '.part';
    $out = fopen($tmp, 'wb');
    if ($in === false || $out === false) {
        fail(500, 'Der Inhalt lässt sich nicht speichern.', 'The content cannot be saved.');
    }
    $size = stream_copy_to_stream($in, $out);
    fclose($out);
    $head = (string) file_get_contents($tmp, false, null, 0, 4);
    if ($size === false || $size < 52 || $head !== 'VPF1') {
        @unlink($tmp);
        fail(400, 'Der Inhalt ist nicht verschlüsselt oder unvollständig (post_max_size = ' . ini_get('post_max_size') . ').',
                  'The content is not encrypted or incomplete (post_max_size = ' . ini_get('post_max_size') . ').');
    }
    if (!rename($tmp, FILES_DIR . '/' . $id . '.bin')) {
        @unlink($tmp);
        fail(500, 'Der Inhalt lässt sich nicht speichern.', 'The content cannot be saved.');
    }
    reply(200, ['ok' => true]);
}

if (isset($_GET['prune'])) {
    $body = json_decode((string) file_get_contents('php://input'), true);
    $keep = is_array($body) && is_array($body['keep'] ?? null) ? array_flip(array_filter($body['keep'], 'is_string')) : null;
    if ($keep === null) {
        fail(400, 'Liste fehlt.', 'List missing.');
    }
    $removed = 0;
    $limit = time() - 86400;
    foreach (scandir(FILES_DIR) ?: [] as $f) {
        $p = FILES_DIR . '/' . $f;
        if (preg_match('/^([0-9a-f]{64})\.(bin|part)$/', $f, $m) && !isset($keep[$m[1]]) && filemtime($p) < $limit) {
            if (@unlink($p)) {
                ++$removed;
            }
        }
    }
    reply(200, ['ok' => true, 'removed' => $removed]);
}

fail(400, 'Unbekannte Anfrage.', 'Unknown request.');
