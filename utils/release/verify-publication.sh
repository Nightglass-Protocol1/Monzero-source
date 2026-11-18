#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: verify-publication.sh <release-metadata.json> <artifact-directory>

Validates artifact names, sizes, hashes, source commits, and package contents.
Set RELEASE_PRODUCTION=1 to additionally require production-ready metadata,
strict package verification, and a detached OpenPGP signature made by the
exact fingerprint in MONZERO_RELEASE_FINGERPRINT.
EOF
  exit 2
}

[[ $# -eq 2 ]] || usage
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
metadata=$(realpath "$1")
artifact_dir=$(realpath "$2")
[[ -f $metadata ]] || { echo "Release metadata not found: $metadata" >&2; exit 2; }
[[ -d $artifact_dir ]] || { echo "Artifact directory not found: $artifact_dir" >&2; exit 2; }

production=${RELEASE_PRODUCTION:-0}
[[ $production == 0 || $production == 1 ]] || {
  echo "RELEASE_PRODUCTION must be 0 or 1" >&2
  exit 2
}

validation_output=$(mktemp "${TMPDIR:-/tmp}/monzero-publication.XXXXXX")
trap 'rm -f -- "$validation_output"' EXIT
if ! python3 - "$metadata" > "$validation_output" <<'PY'
import json, pathlib, sys

path = pathlib.Path(sys.argv[1])
try:
    data = json.loads(path.read_text(encoding="utf-8"))
except (OSError, UnicodeError, json.JSONDecodeError) as exc:
    raise SystemExit(f"Invalid release metadata: {exc}")

required = {
    "schema_version", "project", "release", "channel", "production_ready",
    "source_commit", "published_at", "security_audit", "signing",
    "independent_reproducibility", "artifacts",
}
missing = sorted(required - data.keys())
if missing:
    raise SystemExit("Missing release metadata fields: " + ", ".join(missing))
if data["schema_version"] != 1 or data["project"] != "Monzero":
    raise SystemExit("Unsupported release metadata schema or project")
commit = data["source_commit"]
if not isinstance(commit, str) or len(commit) != 40 or any(c not in "0123456789abcdef" for c in commit):
    raise SystemExit("source_commit must be a 40-character lowercase hexadecimal commit")
if not isinstance(data["production_ready"], bool):
    raise SystemExit("production_ready must be boolean")
artifacts = data["artifacts"]
if not isinstance(artifacts, list) or not artifacts:
    raise SystemExit("artifacts must be a non-empty array")
if len(artifacts) not in (2, 3):
    raise SystemExit("A Monzero publication must contain Linux, Windows, and optionally Windows GUI artifacts")

source = data.get("source")
if source is not None:
    if not isinstance(source, dict):
        raise SystemExit("source must be an object")
    source_filename = source.get("filename")
    source_size = source.get("size_bytes")
    source_digest = source.get("sha256")
    if not isinstance(source_filename, str) or pathlib.PurePosixPath(source_filename).name != source_filename:
        raise SystemExit("Unsafe source artifact filename")
    if not isinstance(source_size, int) or isinstance(source_size, bool) or source_size <= 0:
        raise SystemExit("Invalid source artifact size")
    if not isinstance(source_digest, str) or len(source_digest) != 64 or any(c not in "0123456789abcdef" for c in source_digest):
        raise SystemExit("Invalid source artifact SHA-256")

gui_source = data.get("gui_source")
if gui_source is not None:
    if not isinstance(gui_source, dict):
        raise SystemExit("gui_source must be an object")
    gui_filename = gui_source.get("filename")
    gui_size = gui_source.get("size_bytes")
    gui_digest = gui_source.get("sha256")
    gui_commit = gui_source.get("gui_source_commit")
    gui_core_commit = gui_source.get("core_source_commit")
    if not isinstance(gui_filename, str) or pathlib.PurePosixPath(gui_filename).name != gui_filename:
        raise SystemExit("Unsafe GUI source artifact filename")
    if not isinstance(gui_size, int) or isinstance(gui_size, bool) or gui_size <= 0:
        raise SystemExit("Invalid GUI source artifact size")
    if not isinstance(gui_digest, str) or len(gui_digest) != 64 or any(c not in "0123456789abcdef" for c in gui_digest):
        raise SystemExit("Invalid GUI source artifact SHA-256")
    if not isinstance(gui_commit, str) or len(gui_commit) != 40 or any(c not in "0123456789abcdef" for c in gui_commit):
        raise SystemExit("Invalid GUI source commit")
    if gui_core_commit != commit:
        raise SystemExit("GUI source core commit does not match release source commit")
    if data.get("gui_source_commit") != gui_commit:
        raise SystemExit("GUI source commit does not match top-level release metadata")

print("true" if data["production_ready"] else "false")
print(data["channel"])
print(commit)
print(data["security_audit"])
print(data["signing"])
print(data["independent_reproducibility"])
if source is not None:
    print("\t".join(("source", "source", source_filename, str(source_size), source_digest)))
if gui_source is not None:
    print("\t".join(("gui_source", "gui-source", gui_filename, str(gui_size), gui_digest)))
seen_filenames = set()
seen_platforms = set()
for artifact in artifacts:
    if not isinstance(artifact, dict):
        raise SystemExit("Each artifact must be an object")
    filename = artifact.get("filename")
    size = artifact.get("size_bytes")
    digest = artifact.get("sha256")
    platform = artifact.get("platform")
    if not isinstance(filename, str) or pathlib.PurePosixPath(filename).name != filename:
        raise SystemExit(f"Unsafe artifact filename: {filename!r}")
    if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
        raise SystemExit(f"Invalid artifact size for {filename}")
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise SystemExit(f"Invalid SHA-256 for {filename}")
    if not isinstance(platform, str) or not platform:
        raise SystemExit(f"Invalid platform for {filename}")
    if filename in seen_filenames:
        raise SystemExit(f"Duplicate artifact filename: {filename}")
    if platform in seen_platforms:
        raise SystemExit(f"Duplicate artifact platform: {platform}")
    seen_filenames.add(filename)
    seen_platforms.add(platform)
    print("artifact\t" + platform + "\t" + filename + "\t" + str(size) + "\t" + digest)
required_platforms = {"linux-x86_64", "windows-x64"}
if not required_platforms.issubset(seen_platforms) or not seen_platforms.issubset(required_platforms | {"windows-gui-x64"}):
    raise SystemExit("Artifacts must contain Linux and Windows CLI, with an optional Windows GUI")
PY
then
  exit 1
fi
mapfile -t release_fields < "$validation_output"

[[ ${#release_fields[@]} -ge 7 ]] || { echo "Release metadata produced incomplete validation output" >&2; exit 1; }
production_ready=${release_fields[0]}
channel=${release_fields[1]}
source_commit=${release_fields[2]}
security_audit=${release_fields[3]}
signing=${release_fields[4]}
reproducibility=${release_fields[5]}

if [[ $production == 1 ]]; then
  [[ $production_ready == true && $channel == stable ]] || {
    echo "Production verification requires production_ready=true and channel=stable" >&2; exit 1;
  }
  [[ $security_audit == completed ]] || {
    echo "Production verification requires security_audit=completed" >&2; exit 1;
  }
  [[ $signing == openpgp ]] || {
    echo "Production verification requires signing=openpgp" >&2; exit 1;
  }
  [[ $reproducibility == verified ]] || {
    echo "Production verification requires independent_reproducibility=verified" >&2; exit 1;
  }
  fingerprint=${MONZERO_RELEASE_FINGERPRINT:-}
  [[ $fingerprint =~ ^[0-9A-F]{40}$ ]] || {
    echo "MONZERO_RELEASE_FINGERPRINT must be an exact 40-character uppercase fingerprint" >&2; exit 1;
  }
  signature="$metadata.asc"
  [[ -f $signature ]] || { echo "Detached release metadata signature is missing: $signature" >&2; exit 1; }
  command -v gpg >/dev/null || { echo "Production verification requires gpg" >&2; exit 1; }
  signer=$(gpg --status-fd 1 --verify "$signature" "$metadata" 2>/dev/null |
    awk -f "$script_dir/validsig-primary-fingerprint.awk")
  [[ $signer == "$fingerprint" ]] || {
    echo "Release metadata signature does not match MONZERO_RELEASE_FINGERPRINT" >&2; exit 1;
  }

  reproducer_fingerprint=${MONZERO_REPRODUCER_FINGERPRINT:-}
  [[ $reproducer_fingerprint =~ ^[0-9A-F]{40}$ ]] || {
    echo "MONZERO_REPRODUCER_FINGERPRINT must be an exact 40-character uppercase fingerprint" >&2; exit 1;
  }
  [[ $reproducer_fingerprint != "$fingerprint" ]] || {
    echo "The independent reproducer and release signer must use distinct keys" >&2; exit 1;
  }
  reproduction_attestation=${metadata%.json}.reproduction.json
  reproduction_signature=$reproduction_attestation.asc
  [[ -f $reproduction_attestation ]] || {
    echo "Independent reproduction attestation is missing: $reproduction_attestation" >&2; exit 1;
  }
  [[ -f $reproduction_signature ]] || {
    echo "Independent reproduction signature is missing: $reproduction_signature" >&2; exit 1;
  }
  reproducer_signer=$(gpg --status-fd 1 --verify \
    "$reproduction_signature" "$reproduction_attestation" 2>/dev/null |
    awk -f "$script_dir/validsig-primary-fingerprint.awk")
  [[ $reproducer_signer == "$reproducer_fingerprint" ]] || {
    echo "Reproduction attestation signature does not match MONZERO_REPRODUCER_FINGERPRINT" >&2; exit 1;
  }
  python3 - "$metadata" "$reproduction_attestation" <<'PY'
import json, pathlib, sys

metadata = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
attestation = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))
if attestation.get("schema_version") != 1 or attestation.get("project") != "Monzero":
    raise SystemExit("Unsupported reproduction attestation schema or project")
if attestation.get("result") != "byte-for-byte-match":
    raise SystemExit("Reproduction attestation does not record a byte-for-byte match")
if attestation.get("source_commit") != metadata.get("source_commit"):
    raise SystemExit("Reproduction attestation source commit does not match release metadata")
for field in ("reproducer", "build_environment", "compared_at"):
    if not isinstance(attestation.get(field), str) or not attestation[field].strip():
        raise SystemExit(f"Reproduction attestation field {field} is missing")
expected = {
    (artifact["platform"], artifact["filename"], artifact["sha256"])
    for artifact in metadata["artifacts"]
}
actual_artifacts = attestation.get("artifacts")
if not isinstance(actual_artifacts, list):
    raise SystemExit("Reproduction attestation artifacts must be an array")
actual = {
    (artifact.get("platform"), artifact.get("filename"), artifact.get("sha256"))
    for artifact in actual_artifacts if isinstance(artifact, dict)
}
if actual != expected or len(actual_artifacts) != len(expected):
    raise SystemExit("Reproduction attestation does not bind the exact release artifacts")
PY
else
  [[ $production_ready == false && $channel == prerelease ]] || {
    echo "Non-production verification requires an explicitly marked prerelease" >&2; exit 1;
  }
fi

artifact_count=0
source_count=0
gui_source_count=0
for row in "${release_fields[@]:6}"; do
  IFS=$'\t' read -r marker platform filename expected_size expected_hash <<< "$row"
  artifact="$artifact_dir/$filename"
  [[ -f $artifact ]] || { echo "Published artifact is missing: $artifact" >&2; exit 1; }
  actual_size=$(stat -c %s "$artifact")
  [[ $actual_size == "$expected_size" ]] || {
    echo "Size mismatch for $filename: expected $expected_size, got $actual_size" >&2; exit 1;
  }
  actual_hash=$(sha256sum "$artifact" | awk '{print $1}')
  [[ $actual_hash == "$expected_hash" ]] || {
    echo "SHA-256 mismatch for $filename" >&2; exit 1;
  }

  if [[ $marker == source ]]; then
    ((source_count += 1))
    [[ $source_count -eq 1 ]] || { echo "Multiple source artifacts are forbidden" >&2; exit 1; }
    "$script_dir/verify-source-package.sh" "$artifact"
    package_commit=$(tar -xOf "$artifact" --wildcards '*/SOURCE-MANIFEST.txt' |
      sed -n 's/^source_commit=//p')
    [[ $package_commit == "$source_commit" ]] || {
      echo "Source commit mismatch in $filename" >&2; exit 1;
    }
    continue
  fi
  if [[ $marker == gui_source ]]; then
    ((gui_source_count += 1))
    [[ $gui_source_count -eq 1 ]] || { echo "Multiple GUI source artifacts are forbidden" >&2; exit 1; }
    "$script_dir/verify-gui-source-package.sh" "$artifact"
    package_gui_commit=$(tar -xOf "$artifact" --wildcards '*/SOURCE-MANIFEST.txt' |
      sed -n 's/^gui_source_commit=//p')
    package_core_commit=$(tar -xOf "$artifact" --wildcards '*/SOURCE-MANIFEST.txt' |
      sed -n 's/^core_source_commit=//p')
    metadata_gui_commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["gui_source_commit"])' "$metadata")
    [[ $package_gui_commit == "$metadata_gui_commit" && $package_core_commit == "$source_commit" ]] || {
      echo "GUI/core source commit mismatch in $filename" >&2; exit 1;
    }
    continue
  fi
  [[ $marker == artifact ]] || { echo "Malformed artifact validation record" >&2; exit 1; }

  case "$platform:$filename" in
    linux-x86_64:*.tar.gz)
      package_commit=$(tar -xOf "$artifact" --wildcards '*/BUILD-MANIFEST.txt' |
        sed -n 's/^source_commit=//p')
      if [[ $production == 1 ]]; then
        RELEASE_STRICT=1 "$script_dir/verify-package.sh" "$artifact"
      else
        "$script_dir/verify-package.sh" "$artifact"
      fi
      ;;
    windows-x64:*.zip)
      package_commit=$(unzip -p "$artifact" '*/BUILD-MANIFEST.txt' |
        sed -n 's/^source_commit=//p')
      if [[ $production == 1 ]]; then
        RELEASE_STRICT=1 "$script_dir/verify-windows-package.sh" "$artifact"
      else
        "$script_dir/verify-windows-package.sh" "$artifact"
      fi
      ;;
    windows-gui-x64:*.zip)
      package_commit=$(unzip -p "$artifact" '*/GUI_BUILD_MANIFEST.txt' |
        sed -n 's/^core_source_commit=//p')
      if [[ $production == 1 ]]; then
        RELEASE_STRICT=1 "$script_dir/verify-windows-gui-package.sh" "$artifact"
      else
        "$script_dir/verify-windows-gui-package.sh" "$artifact"
      fi
      ;;
    *) echo "No package verifier for $platform artifact $filename" >&2; exit 1 ;;
  esac
  [[ $package_commit == "$source_commit" ]] || {
    echo "Source commit mismatch in $filename" >&2; exit 1;
  }
  ((artifact_count += 1))
done

[[ $artifact_count -eq 2 || $artifact_count -eq 3 ]] || {
  echo "A Monzero publication must contain Linux and Windows CLI, with an optional Windows GUI" >&2; exit 1;
}
if [[ $production == 1 && $source_count -ne 1 ]]; then
  echo "Production verification requires one verified source artifact" >&2
  exit 1
fi
if [[ $production == 1 ]] && grep -q $'"platform"[[:space:]]*:[[:space:]]*"windows-gui-x64"' "$metadata" && [[ $gui_source_count -ne 1 ]]; then
  echo "Production verification with a GUI artifact requires one verified GUI source artifact" >&2
  exit 1
fi
echo "Publication verification passed ($artifact_count binary artifacts, $source_count core source, $gui_source_count GUI source, commit $source_commit)"
