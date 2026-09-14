#!/usr/bin/env python3
"""Create a self-contained static browser-lab release from validated inputs."""
import hashlib
import json
import re
import shutil
from datetime import datetime
from pathlib import Path

SHA256 = re.compile(r"^[0-9a-f]{64}$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")


def _nonempty_string(value):
    return isinstance(value, str) and bool(value)


def _parse_timestamp(value):
    if not _nonempty_string(value):
        return False
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return False
    return True


def validate_browser_release(manifest, evidence_file, runtime_lock):
    """Return the safe artifact basename after enforcing schema-2 evidence."""
    artifact = manifest.get("artifact")
    digest = manifest.get("sha256")
    evidence = manifest.get("browserValidation")
    marker = manifest.get("bootSuccessMarker")
    runtime_files = runtime_lock.get("files")
    if not (
        manifest.get("schemaVersion") == 2
        and manifest.get("bootable") is True
        and manifest.get("browserCompatible") is True
        and manifest.get("bootableCandidate") is True
        and manifest.get("bootSuccessMarkerImplemented") is True
        and manifest.get("validationOutcome") == "browser-bootable"
        and manifest.get("blockers") == []
        and _nonempty_string(artifact)
        and Path(artifact).name == artifact
        and artifact not in {".", ".."}
        and isinstance(digest, str) and SHA256.fullmatch(digest)
        and _nonempty_string(marker)
        and isinstance(evidence, dict)
        and evidence == evidence_file
        and evidence.get("commit") == manifest.get("commit")
        and evidence.get("artifactSha256") == digest
        and evidence.get("runtime") == runtime_lock.get("runtime")
        and evidence.get("runtimeCommit") == runtime_lock.get("distributionCommit")
        and isinstance(evidence.get("runtimeCommit"), str)
        and COMMIT.fullmatch(evidence["runtimeCommit"])
        and isinstance(runtime_files, dict) and bool(runtime_files)
        and evidence.get("runtimeFiles") == runtime_files
        and all(Path(name).name == name and name not in {".", ".."} and
                isinstance(value, str) and SHA256.fullmatch(value)
                for name, value in runtime_files.items())
        and _parse_timestamp(evidence.get("testedAt"))
        and _nonempty_string(evidence.get("browser"))
        and isinstance(evidence.get("serial"), str)
        and marker in evidence["serial"].splitlines()
        and isinstance(evidence.get("checks"), list)
        and "exact-marker" in evidence["checks"]
        and "vga-text-memory" in evidence["checks"]
    ):
        raise ValueError("Refusing to package an unvalidated browser artifact")
    return artifact


def _sha256(path):
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def package_browser_lab(root):
    dist = root / "dist"
    manifest = json.loads((dist / "build-info.json").read_text(encoding="utf-8"))
    evidence = json.loads((dist / "browser-validation.json").read_text(encoding="utf-8"))
    runtime_lock = json.loads((root / "tools/browser-lab/runtime-lock.json").read_text(encoding="utf-8"))
    artifact = validate_browser_release(manifest, evidence, runtime_lock)
    if _sha256(dist / artifact) != manifest["sha256"]:
        raise ValueError("Refusing to package an artifact with an invalid SHA-256")

    output = dist / "browser-lab"
    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(root / "tools/browser-lab", output,
                    ignore=shutil.ignore_patterns("node_modules", "package*.json"))
    (output / "artifacts").mkdir()
    for name in (artifact, "build-info.json", "browser-validation.json"):
        shutil.copy2(dist / name, output / "artifacts" / name)

    # The release has stable asset roots while development uses ../../dist.
    lab = output / "lab.js"
    source = lab.read_text(encoding="utf-8")
    source = source.replace('metadataUrl: "../../dist/build-info.json", artifactBaseUrl: "../../dist",',
                            'metadataUrl: "./artifacts/build-info.json", artifactBaseUrl: "./artifacts",')
    lab.write_text(source, encoding="utf-8")
    return output


if __name__ == "__main__":
    print(package_browser_lab(Path(__file__).resolve().parents[1]))
