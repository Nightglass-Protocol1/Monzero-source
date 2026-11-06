<?php
declare(strict_types=1);
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }

// Private registration drafts. Public profile restrictions still apply.
function registrationDb(): PDO {
    $db = boardDb();
    $db->exec("CREATE TABLE IF NOT EXISTS registration_drafts (
        member_id INTEGER PRIMARY KEY, signature TEXT NOT NULL DEFAULT '', avatar BLOB,
        allow_pms INTEGER NOT NULL, notify_pm INTEGER NOT NULL, notify_mentions_email INTEGER NOT NULL,
        agreed_at INTEGER NOT NULL, applied INTEGER NOT NULL DEFAULT 0
    )");
    return $db;
}
function registrationOptions(array $post): array {
    $string = static fn(string $key): string => is_string($post[$key] ?? null) ? $post[$key] : '';
    $signature = boardClean($string('signature'));
    if ($string('agree') !== 'on') throw new InvalidArgumentException('Please accept the forum rules and website terms, and read the privacy policy.');
    if ($signature !== '' && ($problem = boardTextProblem($signature, 1, BOARD_SIGNATURE_MAX, 'signature'))) throw new InvalidArgumentException($problem);
    if (substr_count($signature, "\n") >= BOARD_SIGNATURE_LINES) throw new InvalidArgumentException('Use at most four lines in your signature.');
    if (boardLinkCount($signature) > BOARD_SIGNATURE_LINKS) throw new InvalidArgumentException('Use at most two links in your signature.');
    return ['signature' => $signature, 'allow_pms' => $string('allow_pms') === '0' ? 0 : 1,
        'notify_pm' => $string('notify_pm') === '1' ? 1 : 0,
        'notify_mentions_email' => $string('notify_mentions_email') === '1' ? 1 : 0];
}
function registrationAvatar(?array $upload): ?string {
    if (!$upload || ($upload['error'] ?? null) === UPLOAD_ERR_NO_FILE) return null;
    if (($upload['error'] ?? null) !== UPLOAD_ERR_OK || !is_string($upload['tmp_name'] ?? null) || !is_uploaded_file($upload['tmp_name'])) throw new InvalidArgumentException('Choose a valid avatar image no larger than 2 MB.');
    return registrationAvatarBytes($upload['tmp_name']);
}
function registrationAvatarBytes(string $path): string {
    if (!function_exists('imagecreatefromstring')) throw new InvalidArgumentException('Avatar uploads are unavailable. You can add one later in profile settings.');
    if (filesize($path) > BOARD_AVATAR_MAX_BYTES) throw new InvalidArgumentException('Avatar images can be 2 MB at most.');
    $info = @getimagesize($path);
    if (!$info || !in_array($info[2], [IMAGETYPE_JPEG, IMAGETYPE_PNG, IMAGETYPE_GIF, IMAGETYPE_WEBP], true)) throw new InvalidArgumentException('Choose a JPEG, PNG, WebP or GIF avatar.');
    [$w, $h] = $info;
    if (min($w, $h) < 16 || max($w, $h) > BOARD_AVATAR_MAX_SIDE) throw new InvalidArgumentException('Avatar dimensions must be between 16 and ' . BOARD_AVATAR_MAX_SIDE . ' pixels.');
    $source = @imagecreatefromstring((string)file_get_contents($path));
    if (!$source) throw new InvalidArgumentException('That avatar could not be read.');
    $out = imagecreatetruecolor(BOARD_AVATAR_SIZE, BOARD_AVATAR_SIZE);
    imagealphablending($out, false); imagesavealpha($out, true);
    imagefill($out, 0, 0, imagecolorallocatealpha($out, 0, 0, 0, 127));
    $side = min($w, $h);
    imagecopyresampled($out, $source, 0, 0, intdiv($w-$side, 2), intdiv($h-$side, 2), BOARD_AVATAR_SIZE, BOARD_AVATAR_SIZE, $side, $side);
    ob_start(); $ok = imagepng($out, null, 9); $bytes = ob_get_clean();
    if (!$ok || !is_string($bytes)) throw new RuntimeException('Avatar could not be prepared.');
    return $bytes;
}
function registrationSave(int $id, array $options, ?string $avatar): void {
    $stmt = registrationDb()->prepare('INSERT INTO registration_drafts (member_id, signature, avatar, allow_pms, notify_pm, notify_mentions_email, agreed_at) VALUES (?, ?, ?, ?, ?, ?, ?)');
    $stmt->bindValue(1, $id, PDO::PARAM_INT); $stmt->bindValue(2, $options['signature']);
    $stmt->bindValue(3, $avatar, $avatar === null ? PDO::PARAM_NULL : PDO::PARAM_LOB);
    $stmt->bindValue(4, $options['allow_pms'], PDO::PARAM_INT); $stmt->bindValue(5, $options['notify_pm'], PDO::PARAM_INT);
    $stmt->bindValue(6, $options['notify_mentions_email'], PDO::PARAM_INT); $stmt->bindValue(7, time(), PDO::PARAM_INT); $stmt->execute();
}
function registrationApply(int $id): void {
    $db = registrationDb();
    $db->beginTransaction();
    try {
        $stmt = $db->prepare('SELECT * FROM registration_drafts WHERE member_id = ?'); $stmt->execute([$id]); $draft = $stmt->fetch();
        $profile = boardProfile($id);
        if ($draft && $profile && !$draft['applied']) {
            $db->prepare('UPDATE profiles SET allow_pms = ?, notify_pm = ?, notify_mentions_email = ? WHERE member_id = ?')->execute([$draft['allow_pms'], $draft['notify_pm'], $draft['notify_mentions_email'], $id]);
            $db->prepare('UPDATE registration_drafts SET applied = 1 WHERE member_id = ?')->execute([$id]);
        }
        if ($draft && $profile && $profile['introduced_at'] && $draft['avatar'] !== null) {
            if (!$profile['avatar']) {
                $name = bin2hex(random_bytes(16)) . '.png'; $path = boardAvatarDir() . '/' . $name;
                if (file_put_contents($path, $draft['avatar'], LOCK_EX) === false) throw new RuntimeException('Avatar could not be saved.');
                chmod($path, 0600);
                $db->prepare('UPDATE profiles SET avatar = ? WHERE member_id = ?')->execute([$name, $id]);
            }
            $db->prepare('UPDATE registration_drafts SET avatar = NULL WHERE member_id = ?')->execute([$id]);
        }
        $db->commit();
    } catch (Throwable $e) { if ($db->inTransaction()) $db->rollBack(); throw $e; }
}
function registrationSignatureDraft(int $id): string {
    $stmt = registrationDb()->prepare('SELECT signature FROM registration_drafts WHERE member_id = ?'); $stmt->execute([$id]);
    return (string)($stmt->fetchColumn() ?: '');
}
