# Windows packaging status

The inherited Inno Setup installer has been removed. It encoded upstream
Monero application paths, registry keys, URI handlers, data-directory names,
executable names, and upgrade behavior. Distributing it would risk cross-fork
file association and data-directory collisions.

Current Monzero prereleases use a ZIP archive containing the independently
named `monzero-wallet-gui.exe`, `monzerod.exe`, and Monzero wallet controls.

A future native installer must be reviewed and tested for:

- `Monzero GUI Wallet` application and uninstall identifiers;
- `monzero-wallet-gui.exe`, `monzerod.exe`, and Monzero CLI/RPC names;
- `.monzero` data and log paths without access to `.bitmonero`;
- Monzero-only registry and Start Menu locations;
- `monzero:` and `monzeroseed:` URI handlers;
- upgrades, downgrade refusal, uninstall, and preserved wallet/database data;
- clean Windows installation without an existing Monero or Monzero install;
- coexistence with Monero without changing its files, settings, or handlers;
- signed manifests and, separately, Windows Authenticode signing.

Do not introduce a replacement installer until those checks are implemented
and recorded in the release checklist.
