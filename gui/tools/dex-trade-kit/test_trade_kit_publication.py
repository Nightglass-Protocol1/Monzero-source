"""Offline checks that configure publishes APPDIR complete or not at all, and never replaces anything."""
import errno
import hashlib
import itertools
import json
import os
from pathlib import Path
import stat
import sys
import unittest
from unittest import mock

import trade_kit as kit
from test_trade_kit import ConfigureFixture

ROLES = ("ltc", "xmz")
COMMON_FILES = {"journal-helper", "journal.json", "payment-rpc.json", "payment.json", "ca.pem", "peer.json", "app.json"}
ROLE_FILES = {"ltc": COMMON_FILES | {"reservation-rpc.json", "reservation.json", "redeem-rpc.json", "redeem.json"},
              "xmz": COMMON_FILES | {"funding-rpc.json", "funding.json"}}
ROLE_DIRECTORIES = {"ltc": {"meta", "secrets", "reservations"}, "xmz": {"meta", "secrets"}}


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for item in value.values():
            yield from strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)


def snapshot(path):
    """What a pre-existing target looks like, without following it."""
    info = os.lstat(path)
    if stat.S_ISLNK(info.st_mode):
        return ("link", info.st_ino, os.readlink(path))
    if stat.S_ISDIR(info.st_mode):
        return ("dir", info.st_ino, sorted(os.listdir(path)))
    return ("file", info.st_ino, Path(path).read_bytes())


def rename_wrapper(before=None, after=None, instead=None):
    """Replace kit.noreplace_rename with one that runs hooks around (or instead of) the real rename."""
    real = kit.noreplace_rename

    def factory():
        rename = real()

        def wrapped(old_dir, old, new_dir, new):
            if before:
                before(old_dir, old, new_dir, new)
            if instead:
                return instead(old_dir, old, new_dir, new)
            rename(old_dir, old, new_dir, new)
            if after:
                after(old_dir, old, new_dir, new)
        return wrapped
    return mock.patch.object(kit, "noreplace_rename", factory)


@unittest.skipUnless(sys.platform.startswith("linux"), "publication is Linux-only and fails closed elsewhere")
class PublicationTests(ConfigureFixture, unittest.TestCase):
    def assert_complete(self, out, role):
        self.assertEqual(stat.S_IMODE(os.lstat(out).st_mode), 0o700)
        self.assertEqual({p.name for p in out.iterdir()}, ROLE_FILES[role] | ROLE_DIRECTORIES[role])
        for name in ROLE_DIRECTORIES[role]:
            info = os.lstat(out / name)
            self.assertTrue(stat.S_ISDIR(info.st_mode), name)
            self.assertEqual(stat.S_IMODE(info.st_mode), 0o700, name)
            self.assertEqual(os.listdir(out / name), [], name)
        for name in ROLE_FILES[role]:
            info = os.lstat(out / name)
            self.assertTrue(stat.S_ISREG(info.st_mode), name)
            self.assertEqual(info.st_nlink, 1, name)
            self.assertEqual(stat.S_IMODE(info.st_mode), 0o700 if name == "journal-helper" else 0o600, name)

    def assert_nothing_published(self, out):
        self.assertFalse(os.path.lexists(out))
        self.assertEqual(self.staging_leftovers(), [])

    def test_short_writes_are_completed_and_zero_write_fails(self):
        received = bytearray()
        def short_write(fd, data):
            received.extend(data[:2])
            return min(2, len(data))
        with mock.patch.object(kit.os, "write", side_effect=short_write):
            kit.write_all(123, b"retained bytes")
        self.assertEqual(received, b"retained bytes")
        with mock.patch.object(kit.os, "write", return_value=0):
            with self.assertRaises(OSError):
                kit.write_all(123, b"never loop")

    def test_complete_app_with_final_paths_for_both_roles(self):
        for role in ROLES:
            with self.subTest(role=role):
                out = self.root / ("app-" + role)
                self.configure(role=role, out=out)
                self.assert_complete(out, role)
                self.assertEqual(self.staging_leftovers(), [])
                configs = {name: json.loads((out / name).read_text()) for name in ROLE_FILES[role] if name.endswith(".json")}
                for name, data in itertools.chain(configs.items(), [("ca.pem", (out / "ca.pem").read_text())]):
                    self.assertNotIn(".staging-", json.dumps(data), name)
                    for value in strings(data):
                        if value.startswith(str(self.root)) and not value.startswith(str(self.root / role) + "/") \
                                and value != str(self.quote):
                            self.assertTrue(value.startswith(str(out) + "/"), (name, value))
                            self.assertTrue(os.path.lexists(value), (name, value))
                app = configs["app.json"]
                for key, value in app.items():
                    if key.endswith("ConfigFile"):
                        self.assertEqual(Path(value).parent, out, key)
                        self.assertTrue(Path(value).is_file(), key)
                journal = configs["journal.json"]
                self.assertEqual(journal["helper"], str(out / "journal-helper"))
                self.assertEqual(journal["metadataDirectory"], str(out / "meta"))
                self.assertEqual(journal["snapshotDirectory"], str(out / "secrets"))
                self.assertEqual(journal["startQuoteFile"], str(self.quote.resolve()))
                self.assertEqual((out / "journal-helper").read_bytes(), self.helper.read_bytes())
                self.assertEqual(journal["helperDigest"], hashlib.sha256(self.helper.read_bytes()).hexdigest())
                peer = configs["peer.json"]
                self.assertEqual(peer["caFile"], str(out / "ca.pem"))
                # The identity stays where it is: referenced in place, never copied into APPDIR.
                for key, name in (("identityFile", "identity.bin"), ("passphraseFile", "identity.passphrase"),
                                  ("privateKeyFile", "tls.key"), ("certificateFile", "tls.crt")):
                    self.assertEqual(peer[key], str(self.root / role / name))
                if role == "ltc":
                    rpc = configs["reservation-rpc.json"]
                    self.assertEqual(rpc["reservationDirectory"], str(out / "reservations"))

    def test_relative_out_publishes_absolute_final_paths(self):
        previous = os.getcwd()
        self.addCleanup(os.chdir, previous)
        os.chdir(self.root)
        self.configure(out="relative-app")
        out = self.root.resolve() / "relative-app"
        self.assert_complete(out, "ltc")
        app = json.loads((out / "app.json").read_text())
        self.assertEqual(app["journalConfigFile"], str(out / "journal.json"))

    def test_modes_stay_private_under_an_open_umask(self):
        previous = os.umask(0)
        try:
            for role in ROLES:
                with self.subTest(role=role):
                    out = self.root / ("open-umask-" + role)
                    self.configure(role=role, out=out)
                    self.assert_complete(out, role)
        finally:
            os.umask(previous)

    def test_midwrite_failure_at_every_file_publishes_nothing(self):
        real = kit.write_all
        for role in ROLES:
            for target in itertools.count():
                calls = []

                def failing(fd, data):
                    calls.append(len(data))
                    if len(calls) - 1 == target:
                        os.write(fd, bytes(data[:len(data) // 2]))
                        raise OSError(errno.ENOSPC, "injected: no space left")
                    return real(fd, data)
                out = self.root / f"app-{role}-{target}"
                with self.subTest(role=role, failing_file=target), mock.patch.object(kit, "write_all", failing):
                    if target >= len(ROLE_FILES[role]):
                        self.configure(role=role, out=out)  # past the last file: nothing injected
                        self.assert_complete(out, role)
                        break
                    with self.assertRaises(SystemExit) as raised:
                        self.configure(role=role, out=out)
                    self.assertIn("nothing was published", str(raised.exception))
                    self.assert_nothing_published(out)
            self.assertEqual(target, len(ROLE_FILES[role]))

    def test_interrupt_midwrite_publishes_nothing(self):
        real, calls = kit.write_all, []

        def interrupted(fd, data):
            calls.append(1)
            if len(calls) == 3:
                raise KeyboardInterrupt
            return real(fd, data)
        for role in ROLES:
            calls.clear()
            with self.subTest(role=role), mock.patch.object(kit, "write_all", interrupted):
                out = self.root / ("app-" + role)
                with self.assertRaises(KeyboardInterrupt):
                    self.configure(role=role, out=out)
                self.assert_nothing_published(out)

    def test_fsync_failure_before_publication_publishes_nothing_and_after_is_explained(self):
        real = kit.fsync
        for role in ROLES:
            for target in itertools.count():
                calls = []

                def failing(fd):
                    calls.append(fd)
                    if len(calls) - 1 == target:
                        raise OSError(errno.EIO, "injected: I/O error")
                    return real(fd)
                out = self.root / f"app-{role}-{target}"
                with self.subTest(role=role, failing_sync=target), mock.patch.object(kit, "fsync", failing):
                    with self.assertRaises(SystemExit) as raised:
                        self.configure(role=role, out=out)
                    message = str(raised.exception)
                    if not os.path.lexists(out):
                        self.assertIn("nothing was published", message)
                        self.assert_nothing_published(out)
                        continue
                    # Only the final parent sync is after publication: report it, keep the complete app.
                    self.assertIn("was published complete", message)
                    self.assertIn("left in place", message)
                    self.assert_complete(out, role)
                    self.assertEqual(self.staging_leftovers(), [])
                    break
            # one sync per subdirectory and file, the staging directory, then the parent
            self.assertEqual(target, len(ROLE_DIRECTORIES[role]) + len(ROLE_FILES[role]) + 1)

    def test_everything_is_synced_before_publication_and_parent_after(self):
        real = kit.fsync
        for role in ROLES:
            events = []

            def recording(fd):
                events.append(os.readlink(f"/proc/self/fd/{fd}"))
                return real(fd)
            out = self.root / ("app-" + role)
            with self.subTest(role=role), mock.patch.object(kit, "fsync", recording), \
                    rename_wrapper(before=lambda *_: events.append("rename")):
                self.configure(role=role, out=out)
            split = events.index("rename")
            before, after = events[:split], events[split + 1:]
            staging = Path(before[-1])
            self.assertEqual(staging.parent, self.root)
            self.assertTrue(staging.name.startswith(".app-" + role + ".staging-"))
            self.assertEqual(set(before[:-1]), {str(staging / name) for name in ROLE_FILES[role] | ROLE_DIRECTORIES[role]})
            self.assertEqual(len(before), len(set(before)))
            self.assertEqual(after, [str(self.root)])
            self.assert_complete(out, role)

    def make_target(self, kind, path):
        if kind == "file":
            path.write_bytes(b"someone else's file")
        elif kind == "empty directory":
            path.mkdir()
        elif kind == "directory":
            path.mkdir()
            (path / "app.json").write_text("someone else's app")
        elif kind == "symlink to directory":
            (self.root / "elsewhere").mkdir(exist_ok=True)
            path.symlink_to(self.root / "elsewhere")
        elif kind == "symlink to file":
            (self.root / "elsewhere-file").write_text("x")
            path.symlink_to(self.root / "elsewhere-file")
        elif kind == "dangling symlink":
            path.symlink_to(self.root / "would-be-created")
        else:
            raise AssertionError(kind)

    KINDS = ("file", "empty directory", "directory", "symlink to directory", "symlink to file", "dangling symlink")

    def test_existing_target_is_never_replaced(self):
        for role in ROLES:
            for kind in self.KINDS:
                with self.subTest(role=role, kind=kind):
                    out = self.root / f"existing-{role}-{kind.replace(' ', '-')}"
                    self.make_target(kind, out)
                    before = snapshot(out)
                    with self.assertRaises(SystemExit) as raised:
                        self.configure(role=role, out=out)
                    self.assertIn("already exists", str(raised.exception))
                    self.assertEqual(snapshot(out), before)
                    self.assertFalse(os.path.lexists(self.root / "would-be-created"))
                    self.assertEqual(os.listdir(self.root / "elsewhere") if (self.root / "elsewhere").exists() else [], [])
                    self.assertEqual(self.staging_leftovers(), [])

    def test_target_created_during_publication_is_never_replaced(self):
        for role in ROLES:
            for kind in self.KINDS:
                with self.subTest(role=role, kind=kind):
                    out = self.root / f"racing-{role}-{kind.replace(' ', '-')}"
                    made = []

                    def race(*_):
                        self.make_target(kind, out)
                        made.append(snapshot(out))
                    with rename_wrapper(before=race), self.assertRaises(SystemExit) as raised:
                        self.configure(role=role, out=out)
                    self.assertIn("appeared while configuring and was not replaced", str(raised.exception))
                    self.assertEqual(snapshot(out), made[0])
                    self.assertFalse(os.path.lexists(self.root / "would-be-created"))
                    self.assertEqual(self.staging_leftovers(), [])

    def test_interrupt_right_after_publication_keeps_complete_app(self):
        for role in ROLES:
            def interrupt(*_):
                raise KeyboardInterrupt
            out = self.root / ("app-" + role)
            with self.subTest(role=role), rename_wrapper(after=interrupt), self.assertRaises(KeyboardInterrupt):
                self.configure(role=role, out=out)
            self.assert_complete(out, role)
            self.assertEqual(self.staging_leftovers(), [])

    def test_ambiguous_publication_is_left_for_review(self):
        def vanish(old_dir, old, new_dir, new):
            os.rename(old, "moved-away", src_dir_fd=old_dir, dst_dir_fd=new_dir)
            raise OSError(errno.EIO, "injected: lost track of the rename")
        out = self.root / "app"
        with rename_wrapper(instead=vanish), self.assertRaises(SystemExit) as raised:
            self.configure(out=out)
        self.assertIn("could not tell whether", str(raised.exception))
        self.assertFalse(os.path.lexists(out))
        self.assert_complete(self.root / "moved-away", "ltc")  # nothing it held was removed

    def test_filesystem_without_noreplace_fails_closed(self):
        for code in (errno.EINVAL, errno.ENOSYS, errno.EOPNOTSUPP):
            def unsupported(*_):
                raise OSError(code, os.strerror(code))
            out = self.root / "app"
            with self.subTest(errno=errno.errorcode[code]), rename_wrapper(instead=unsupported), \
                    self.assertRaises(SystemExit) as raised:
                self.configure(out=out)
            self.assertIn("does not support no-replace rename", str(raised.exception))
            self.assert_nothing_published(out)

    def test_unsupported_platforms_fail_closed_before_writing(self):
        for name in ("darwin", "win32", "cygwin", "freebsd14"):
            with self.subTest(platform=name):
                listing = sorted(os.listdir(self.root))
                with mock.patch.object(kit.sys, "platform", name), self.assertRaises(SystemExit) as raised:
                    self.configure()
                self.assertIn("cannot publish safely", str(raised.exception))
                self.assertEqual(sorted(os.listdir(self.root)), listing)

    def test_invalid_inputs_create_nothing(self):
        def set_local(role, change):
            def apply():
                local = json.loads(json.dumps(self.local))
                change(local)
                (self.root / "local.json").write_text(json.dumps(local))
            return apply

        cases = {
            "xmz lacks source wallet": ("xmz", set_local("xmz", lambda l: l["xmz"].pop("sourceAddress")), {}),
            "ltc lacks observer wallet": ("ltc", set_local("ltc", lambda l: l["xmz"].pop("observerLoginFile")), {}),
            "lacks peer port": ("ltc", set_local("ltc", lambda l: l["peer"].pop("port")), {}),
            "litecoin not an object": ("xmz", set_local("xmz", lambda l: l.update(litecoin=[])), {}),
            "local not an object": ("ltc", lambda: (self.root / "local.json").write_text("[]"), {}),
            "local not json": ("ltc", lambda: (self.root / "local.json").write_text("{"), {}),
            "policy not object": ("xmz", lambda: (self.root / "policy.json").write_text("[]"), {}),
            "port boolean": ("ltc", set_local("ltc", lambda l: l["peer"].update(port=True)), {}),
            "port out of range": ("ltc", set_local("ltc", lambda l: l["litecoin"].update(rpcPort=65536)), {}),
            "port string": ("xmz", set_local("xmz", lambda l: l["xmz"].update(sourceWalletPort="16184")), {}),
            "policy not json": ("xmz", lambda: (self.root / "policy.json").write_text("not json"), {}),
            "helper missing": ("ltc", lambda: self.helper.unlink(), {}),
            "helper empty": ("xmz", lambda: self.helper.write_bytes(b""), {}),
            "identity missing": ("ltc", lambda: (self.root / "ltc" / "identity.bin").unlink(), {}),
            "tls key missing": ("xmz", lambda: (self.root / "xmz" / "tls.key").unlink(), {}),
            "peer card is own role": ("ltc", lambda: None, {"peer_card": "ltc/card.json"}),
            "out parent missing": ("ltc", lambda: None, {"out": "missing/app"}),
            "out is dot-dot": ("ltc", lambda: None, {"out": "ltc/.."}),
        }
        for name, (role, prepare, overrides) in cases.items():
            with self.subTest(case=name):
                self.setUp()  # a fresh root for each case
                prepare()
                overrides = {k: self.root / v for k, v in overrides.items()}
                listing = sorted(os.listdir(self.root))
                with self.assertRaises(SystemExit):
                    self.configure(role=role, **overrides)
                self.assertEqual(sorted(os.listdir(self.root)), listing)
                self.assertFalse(os.path.lexists(self.root / "app"))


if __name__ == "__main__":
    unittest.main()
