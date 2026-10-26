<?php
// VitaParms – gemeinsame Hilfsfunktionen der Schnittstellen (upload.php, sync.php, files.php)
// SPDX-License-Identifier: MIT

declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

const DATA_DIR  = __DIR__ . '/../data';
const KEY_FILE  = DATA_DIR . '/.uploadkey.php';
const LOCK_FILE = DATA_DIR . '/.lock';
const SYNC_DIR  = __DIR__ . '/../sync';
const MAX_FILES = 2000;
const MAX_TOTAL = 200 * 1024 * 1024;   // 200 MB je Paket

function de(): bool
{
    return stripos($_SERVER['HTTP_ACCEPT_LANGUAGE'] ?? '', 'de') === 0;
}

function t(string $de, string $en): string
{
    return de() ? $de : $en;
}

function reply(int $code, array $body): void
{
    http_response_code($code);
    echo json_encode($body, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

function fail(int $code, string $de, string $en): void
{
    reply($code, ['ok' => false, 'error' => t($de, $en)]);
}

function storedHash(): ?string
{
    if (!is_file(KEY_FILE)) {
        return null;
    }
    $content = (string) file_get_contents(KEY_FILE);
    return preg_match('/^#(\S+)$/m', $content, $m) ? $m[1] : null;
}

function givenKey(): string
{
    return trim((string) ($_SERVER['HTTP_X_VITAPARMS_KEY'] ?? ''));
}

function ensureDataDir(): void
{
    if (!is_dir(DATA_DIR) && !@mkdir(DATA_DIR, 0755, true)) {
        fail(500, 'Der Ordner data/ fehlt und lässt sich nicht anlegen.', 'The folder data/ is missing and cannot be created.');
    }
    if (!is_writable(DATA_DIR)) {
        fail(500, 'Der Ordner data/ ist nicht beschreibbar (Rechte 755 oder 775 setzen).',
                  'The folder data/ is not writable (set permissions to 755 or 775).');
    }
}

function removeTree(string $dir): void
{
    if (!is_dir($dir)) {
        return;
    }
    foreach (scandir($dir) ?: [] as $f) {
        if ($f === '.' || $f === '..') {
            continue;
        }
        $p = $dir . '/' . $f;
        is_dir($p) ? removeTree($p) : @unlink($p);
    }
    @rmdir($dir);
}

function uploadError(int $code): string
{
    switch ($code) {
        case UPLOAD_ERR_INI_SIZE:
        case UPLOAD_ERR_FORM_SIZE:
            return t('Eine Datei ist zu groß für den Server (upload_max_filesize = ', 'A file is too large for the server (upload_max_filesize = ')
                . ini_get('upload_max_filesize') . ').';
        case UPLOAD_ERR_PARTIAL:
            return t('Eine Datei kam nur teilweise an.', 'A file arrived only partially.');
        case UPLOAD_ERR_NO_TMP_DIR:
        case UPLOAD_ERR_CANT_WRITE:
            return t('Der Server kann die hochgeladene Datei nicht zwischenspeichern.', 'The server cannot store the uploaded file.');
        default:
            return t('Fehler beim Hochladen (Code ', 'Upload error (code ') . $code . ').';
    }
}


// Upload-Schlüssel prüfen; beim allerersten Zugriff mit $claim = true übernehmen.
// Muss unter der Sperre (LOCK_FILE) aufgerufen werden, wenn $claim gesetzt ist.
function authorize(bool $claim): void
{
    $key = givenKey();
    if (strlen($key) < 16) {
        fail(401, 'Upload-Schlüssel fehlt.', 'Upload key missing.');
    }
    $hash = storedHash();
    if ($hash === null) {
        if (!$claim) {
            fail(403, 'Die Website ist noch nicht eingerichtet.', 'The website has not been set up yet.');
        }
        ensureDataDir();
        $content = "<?php http_response_code(404); exit; ?>\n#" . password_hash($key, PASSWORD_DEFAULT) . "\n";
        if (file_put_contents(KEY_FILE, $content, LOCK_EX) === false) {
            fail(500, 'Der Upload-Schlüssel lässt sich nicht speichern.', 'The upload key cannot be saved.');
        }
        @chmod(KEY_FILE, 0600);
    } elseif (!password_verify($key, $hash)) {
        usleep(random_int(300000, 900000));
        fail(403, 'Der Upload-Schlüssel passt nicht zu dieser Website.', 'The upload key does not match this website.');
    }
}

function lockSite()
{
    ensureDataDir();
    $lock = fopen(LOCK_FILE, 'c');
    if ($lock === false || !flock($lock, LOCK_EX)) {
        fail(503, 'Die Website ist gerade beschäftigt – bitte gleich noch einmal versuchen.', 'The website is busy – please try again.');
    }
    return $lock;
}
