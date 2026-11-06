# Backups

`utils/website/backup-to-usb.sh` makes an encrypted backup of everything that exists only on the
servers and copies it to the owner's USB drives. A user timer runs it every night at 03:30
(`~/.config/systemd/user/monzero-backup.timer`, copies in `utils/website/systemd/`); it also runs
at the next start if the PC was off.

## What is in a backup

Each run writes `<time>-webspace.tar.gpg`, `<time>-eu-node.tar.gpg` and `<time>.sha256`.

- **webspace**: `~/monzero-private/` (admin, faucet, voting and Telegram databases as consistent
  snapshots, every config and secret, node census, content history, deploy manifest),
  admin-edited content (`community/announcements.json`, `roadmap/updates.json`, the four catalogs),
  `network/data` and `news/data`, and the miner-stats file with its history and config.
- **eu-node**: pool database (what the pool owes miners), pool and faucet wallet files, the faucet
  payout journal, network history database, and `/etc` settings and secrets of every service.
  The country database and public exports are not included; they are downloaded or regenerated.

Wallet seeds remain the real wallet backup; the wallet files here only save resyncing.

## Where

| Drive | Folder |
|---|---|
| monzero-transfer SSD (UUID 68aef467-…) | `Monzero-App/Monzero-backups/` |
| Seagate 1 TB (UUID 0BA2-2A03) | `Monzero-backups/` |

The newest 14 backups are kept on each drive. A drive that is not plugged in is skipped (the run
fails only if no drive is connected). `bash utils/website/backup-to-usb.sh --list` shows them.
After copying, the script checks the checksums, decrypts the copy and runs SQLite's integrity check
on every database.

## The key

Archives are encrypted to the gpg key **Monzero website backups**, fingerprint
`29505BF7DC5B2A73136690E1AEB50246E151A3BC`, kept in this PC's keyring without a passphrase so the
nightly run needs nobody. Without that key the backups cannot be opened, so keep one copy of it
somewhere other than this PC and the drives, for example a password manager:

```sh
gpg --export-secret-keys --armor 29505BF7DC5B2A73136690E1AEB50246E151A3BC > monzero-backup-key.asc
# store the file safely, then delete it from the disk
```

## Restore

```sh
B=/run/media/tripo/0BA2-2A03/Monzero-backups; T=20261020T140216Z      # pick a backup
cd "$B" && sha256sum -c "$T.sha256"
mkdir -p ~/restore && gpg -d "$B/$T-webspace.tar.gpg" | tar -C ~/restore -xf -
gpg -d "$B/$T-eu-node.tar.gpg" | tar -C ~/restore/eu -xf -              # mkdir ~/restore/eu first
```

`~/restore/home/` mirrors the webspace home (`monzero-private/…`), `~/restore/docroot/` the files
under the document root, `~/restore/eu/` the EU node's `/etc` and `/var/lib`. Stop the matching
service before copying a database back (for the pool: `systemctl stop xmzpool`), copy it into place
with the original owner and mode, then start the service again.
