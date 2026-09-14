#!/usr/bin/env python3
"""Regression tests for fail-closed browser-lab release packaging."""
import copy
import hashlib
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from scripts.package_browser_lab_release import (
    package_browser_lab,
    validate_browser_release,
)


artifact_bytes = b"validated browser kernel"
artifact_sha256 = hashlib.sha256(artifact_bytes).hexdigest()
commit = "1" * 40
runtime_lock = {
    "runtime": "ktock/qemu-wasm",
    "distributionCommit": "2" * 40,
    "files": {"qemu.wasm": "3" * 64},
}
evidence = {
    "commit": commit,
    "artifactSha256": artifact_sha256,
    "runtime": runtime_lock["runtime"],
    "runtimeCommit": runtime_lock["distributionCommit"],
    "runtimeFiles": runtime_lock["files"],
    "testedAt": "2026-09-14T00:00:00.000Z",
    "browser": "Chromium 140",
    "serial": "booting\r\nFLINTSTONE_KERNEL_BOOT_OK\r\n",
    "checks": ["exact-marker", "vga-text-memory"],
}
manifest = {
    "schemaVersion": 2,
    "commit": commit,
    "artifact": "flintstone.img",
    "sha256": artifact_sha256,
    "bootSuccessMarker": "FLINTSTONE_KERNEL_BOOT_OK",
    "bootSuccessMarkerImplemented": True,
    "bootableCandidate": True,
    "bootable": True,
    "browserCompatible": True,
    "validationOutcome": "browser-bootable",
    "blockers": [],
    "browserValidation": evidence,
}

assert validate_browser_release(manifest, evidence, runtime_lock) == "flintstone.img"
for field in evidence:
    incomplete = copy.deepcopy(evidence)
    del incomplete[field]
    invalid = {**manifest, "browserValidation": incomplete}
    try:
        validate_browser_release(invalid, incomplete, runtime_lock)
    except ValueError:
        pass
    else:
        raise AssertionError(f"missing browser evidence field was accepted: {field}")

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    dist = root / "dist"
    lab = root / "tools/browser-lab"
    dist.mkdir()
    lab.mkdir(parents=True)
    (dist / "flintstone.img").write_bytes(artifact_bytes)
    (dist / "build-info.json").write_text(json.dumps(manifest), encoding="utf-8")
    (dist / "browser-validation.json").write_text(json.dumps(evidence), encoding="utf-8")
    (lab / "runtime-lock.json").write_text(json.dumps(runtime_lock), encoding="utf-8")
    (lab / "lab.js").write_text(
        'metadataUrl: "../../dist/build-info.json", artifactBaseUrl: "../../dist",',
        encoding="utf-8",
    )

    output = package_browser_lab(root)
    assert (output / "artifacts/flintstone.img").read_bytes() == artifact_bytes
    assert 'metadataUrl: "./artifacts/build-info.json"' in (output / "lab.js").read_text(encoding="utf-8")

    (dist / "flintstone.img").write_bytes(b"changed after validation")
    try:
        package_browser_lab(root)
    except ValueError as error:
        assert "invalid SHA-256" in str(error)
    else:
        raise AssertionError("post-validation artifact mutation was packaged")

print("test_package_browser_lab_release: PASS")
