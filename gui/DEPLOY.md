# Monzero GUI deployment status

Monzero GUI is prerelease software. The inherited Monero deployment procedure
is not a valid Monzero release process and has been retired.

Current test packages must be built from the exact GUI commit and pinned core
submodule recorded by that commit, with `DEV_MODE=OFF` and `WITH_UPDATER=OFF`.
Record the compiler, dependency environment, artifact sizes, and SHA-256 sums.
Distribute source and binaries together as clearly labelled prerelease
archives. Do not advertise them as reproducible or verified releases.

A public release additionally requires:

- a project-controlled source distribution channel;
- an offline release key with a published, independently verified fingerprint;
- signed source and binary manifests;
- independently reproduced Linux and Windows builds;
- a tested native Windows installer or an explicitly supported ZIP package;
- clean-install, upgrade, seed-restoration, synchronization, and transfer tests;
- an independent security review; and
- a private vulnerability-reporting contact.

Automatic updating must remain disabled until Monzero-owned update manifests,
keys, transport endpoints, rollback handling, and failure behavior have been
reviewed and tested.
