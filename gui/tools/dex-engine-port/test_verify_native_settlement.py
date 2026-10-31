"""No-node tests for strict Cargo artifact selection in the acceptance runner."""
import json
from pathlib import Path
import tempfile
import unittest
from verify_native_settlement import built_executable, fixture_environment


class ArtifactSelection(unittest.TestCase):
    def test_fixture_environment_pins_local_build_and_docker(self):
        hostile = {key: "unwanted" for key in (
            "MONZERO_TEST_SPEND_KEY", "MONZERO_TEST_QT_QUOTE_DRIVER", "RUSTC", "RUSTDOC", "RUSTC_WRAPPER",
            "RUSTFLAGS", "CARGO_ENCODED_RUSTFLAGS", "CARGO_TARGET_DIR", "CARGO_BUILD_TARGET",
            "DOCKER_HOST", "DOCKER_CONTEXT", "DOCKER_TLS_VERIFY", "LD_PRELOAD", "LD_AUDIT",
            "BASH_ENV", "ENV", "PYTHONPATH", "PYTHONHOME", "DYLD_INSERT_LIBRARIES")}
        hostile["PATH"] = "/usr/bin"
        hostile["HOME"] = "/caller/home"
        core = Path("/owned/core")
        env = fixture_environment(hostile, core)
        self.assertEqual(hostile["RUSTC_WRAPPER"], "unwanted")
        self.assertEqual(env["PATH"], "/usr/bin")
        self.assertEqual(env["HOME"], "/caller/home")
        self.assertEqual(env["DOCKER_HOST"], "unix:///var/run/docker.sock")
        self.assertEqual(env["RUSTC"], str(core / "build/dex-rust-toolchain-1.95.0/sysroot/usr/bin/rustc"))
        self.assertEqual(env["RUSTDOC"], str(core / "build/dex-rust-toolchain-1.95.0/sysroot/usr/bin/rustdoc"))
        for key in hostile:
            if key not in ("PATH", "HOME"):
                self.assertNotEqual(env.get(key), "unwanted", key)

    def test_rejects_missing_ambiguous_and_wrong_source_artifacts(self):
        with tempfile.TemporaryDirectory(prefix="xmz-build-selection-") as root:
            root = Path(root).resolve()
            source = root / "test.rs"
            source.touch()
            executable = root / "test-binary"
            executable.touch()
            event = {"reason": "compiler-artifact", "target": {
                "name": "settlement", "kind": ["test"], "src_path": str(source)},
                "profile": {"test": True}, "executable": str(executable)}
            select = lambda events: built_executable("\n".join(json.dumps(e) for e in events),
                                                     root, "settlement", "test", "test.rs", True)
            self.assertEqual(select([event]), executable)
            self.assertEqual(select([event, event]), executable)
            for events in ([], [{**event, "executable": None}],
                           [{**event, "profile": {"test": False}}]):
                with self.assertRaises(RuntimeError):
                    select(events)
            other = root / "other"
            other.touch()
            with self.assertRaises(RuntimeError):
                select([event, {**event, "executable": str(other)}])
            with self.assertRaises(RuntimeError):
                select([{**event, "target": {**event["target"], "src_path": str(other)}}])


if __name__ == "__main__":
    unittest.main()
