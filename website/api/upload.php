<?php
// VitaParms – Upload-Schnittstelle der Website
//
// Nimmt das von VitaParms verschlüsselte Datenpaket an und tauscht es atomar aus.
// Der Server sieht nur Chiffrat; einen Schlüssel zum Entschlüsseln gibt es hier nicht.
//
//   GET  upload.php?status=1   → Zustand (PHP-Version, eingerichtet?, Schlüssel gültig?)
//   POST upload.php            → neues Paket (Kopf "X-VitaParms-Key": Upload-Schlüssel)
//
// Beim allerersten Hochladen merkt sich die Website den Upload-Schlüssel (als Hash in
// data/.uploadkey.php). Neuen Schlüssel übernehmen: diese Datei per FTP löschen.
// SPDX-License-Identifier: MIT

require __DIR__ . '/lib.php';

// ------------------------------------------------------------------ Zustand
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    $hash = storedHash();
    $key = givenKey();
    reply(200, [
        'ok'       => true,
        'php'      => PHP_VERSION,
        'claimed'  => $hash !== null,
        'keyValid' => $hash !== null && $key !== '' && password_verify($key, $hash),
        'writable' => is_dir(DATA_DIR) ? is_writable(DATA_DIR) : is_writable(dirname(DATA_DIR)),
        'maxUpload' => ini_get('upload_max_filesize'),
        'maxPost'  => ini_get('post_max_size'),
    ]);
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    fail(405, 'Nur GET und POST sind erlaubt.', 'Only GET and POST are allowed.');
}

// ------------------------------------------------------------------ Berechtigung
if (empty($_POST) && empty($_FILES) && (int) ($_SERVER['CONTENT_LENGTH'] ?? 0) > 0) {
    fail(413, 'Das Paket ist größer als post_max_size (' . ini_get('post_max_size') . ') des Servers.',
              'The package exceeds the server\'s post_max_size (' . ini_get('post_max_size') . ').');
}
$lock = lockSite();
authorize(true);

// ------------------------------------------------------------------ Paket prüfen
$bundle = (string) ($_POST['bundle'] ?? '');
if (!preg_match('/^\d{14}-[0-9a-f]{8}$/', $bundle)) {
    fail(400, 'Ungültige Paketkennung.', 'Invalid bundle id.');
}
if (!isset($_FILES['manifest']) || $_FILES['manifest']['error'] !== UPLOAD_ERR_OK) {
    fail(400, 'Das Manifest fehlt. ' . (isset($_FILES['manifest']) ? uploadError($_FILES['manifest']['error']) : ''),
              'The manifest is missing. ' . (isset($_FILES['manifest']) ? uploadError($_FILES['manifest']['error']) : ''));
}
$manifest = json_decode((string) file_get_contents($_FILES['manifest']['tmp_name']), true);
if (!is_array($manifest) || ($manifest['format'] ?? '') !== 'VPW1' || ($manifest['bundle'] ?? '') !== $bundle
    || !isset($manifest['kdf']['salt'], $manifest['kdf']['iterations'], $manifest['check'])) {
    fail(400, 'Das Manifest ist ungültig.', 'The manifest is invalid.');
}
$files = $_FILES['files'] ?? null;
if (!is_array($files) || !is_array($files['name'])) {
    fail(400, 'Es wurden keine Dateien übertragen.', 'No files were transferred.');
}
$count = count($files['name']);
if ($count < 1 || $count > MAX_FILES) {
    fail(400, 'Unerwartete Anzahl von Dateien.', 'Unexpected number of files.');
}
$total = 0;
$names = [];
for ($i = 0; $i < $count; $i++) {
    if ($files['error'][$i] !== UPLOAD_ERR_OK) {
        fail(400, uploadError($files['error'][$i]), uploadError($files['error'][$i]));
    }
    $name = (string) $files['name'][$i];
    if (!preg_match('/^(data|r\d{1,5})\.vpw$/', $name) || isset($names[$name])) {
        fail(400, 'Ungültiger Dateiname: ' . $name, 'Invalid file name: ' . $name);
    }
    $head = (string) file_get_contents($files['tmp_name'][$i], false, null, 0, 4);
    if ($head !== 'VPW1') {
        fail(400, 'Datei ist nicht verschlüsselt: ' . $name, 'File is not encrypted: ' . $name);
    }
    $total += (int) $files['size'][$i];
    $names[$name] = $i;
}
if (!isset($names['data.vpw'])) {
    fail(400, 'data.vpw fehlt.', 'data.vpw is missing.');
}
if ($total > MAX_TOTAL) {
    fail(413, 'Das Paket ist zu groß.', 'The package is too large.');
}

// ------------------------------------------------------------------ Speichern und umschalten
$target = DATA_DIR . '/' . $bundle;
removeTree($target);
if (!@mkdir($target, 0755)) {
    fail(500, 'Der Paketordner lässt sich nicht anlegen.', 'The bundle folder cannot be created.');
}
foreach ($names as $name => $i) {
    if (!move_uploaded_file($files['tmp_name'][$i], $target . '/' . $name)) {
        removeTree($target);
        fail(500, 'Eine Datei lässt sich nicht speichern: ' . $name, 'A file cannot be saved: ' . $name);
    }
}
$manifestOut = [
    'format'     => 'VPW1',
    'bundle'     => $bundle,
    'kdf'        => ['name' => 'PBKDF2-SHA256', 'salt' => (string) $manifest['kdf']['salt'],
                     'iterations' => (int) $manifest['kdf']['iterations']],
    'check'      => (string) $manifest['check'],
    'data'       => 'data.vpw',
    'lang'       => ($manifest['lang'] ?? 'de') === 'en' ? 'en' : 'de',
    'uploaded'   => gmdate('c'),
];
// Prüfwert für das Zugriffsprotokoll (SHA-256 des aus dem Passwort abgeleiteten Zugriffswerts) –
// nicht im öffentlichen Manifest, sondern geschützt abgelegt (siehe log.php)
$access = strtolower((string) ($manifest['access'] ?? ''));
if (preg_match('/^[0-9a-f]{64}$/', $access)) {
    file_put_contents(DATA_DIR . '/.access.php', "<?php http_response_code(404); exit; ?>\n#" . $access . "\n", LOCK_EX);
    @chmod(DATA_DIR . '/.access.php', 0600);
}
$tmp = DATA_DIR . '/current.json.tmp';
file_put_contents($tmp, json_encode($manifestOut, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));
if (!rename($tmp, DATA_DIR . '/current.json')) {
    fail(500, 'Das neue Paket lässt sich nicht aktivieren.', 'The new bundle cannot be activated.');
}

// Ältere Pakete entfernen – das vorige bleibt, falls gerade jemand lädt
$previous = null;
$dirs = glob(DATA_DIR . '/[0-9]*-*', GLOB_ONLYDIR) ?: [];
rsort($dirs);
foreach ($dirs as $d) {
    if (basename($d) === $bundle) {
        continue;
    }
    if ($previous === null) {
        $previous = $d;
        continue;
    }
    removeTree($d);
}
flock($lock, LOCK_UN);
reply(200, ['ok' => true, 'bundle' => $bundle, 'files' => $count, 'bytes' => $total,
            'message' => t('Paket gespeichert.', 'Bundle saved.')]);
