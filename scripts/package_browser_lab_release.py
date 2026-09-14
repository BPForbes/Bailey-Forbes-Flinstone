#!/usr/bin/env python3
"""Create a self-contained static browser-lab release from validated inputs."""
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
dist = root / "dist"
manifest = json.loads((dist / "build-info.json").read_text(encoding="utf-8"))
if not (manifest.get("bootable") and manifest.get("browserCompatible") and
        manifest.get("validationOutcome") == "browser-bootable" and
        manifest.get("browserValidation")):
    raise SystemExit("Refusing to package an unvalidated browser artifact")

output = dist / "browser-lab"
if output.exists():
    shutil.rmtree(output)
shutil.copytree(root / "tools/browser-lab", output,
                ignore=shutil.ignore_patterns("node_modules", "package*.json"))
(output / "artifacts").mkdir()
for name in (manifest["artifact"], "build-info.json", "browser-validation.json"):
    shutil.copy2(dist / name, output / "artifacts" / name)

# The release has stable asset roots while development uses ../../dist.
lab = output / "lab.js"
source = lab.read_text(encoding="utf-8")
source = source.replace('metadataUrl: "../../dist/build-info.json", artifactBaseUrl: "../../dist",',
                        'metadataUrl: "./artifacts/build-info.json", artifactBaseUrl: "./artifacts",')
lab.write_text(source, encoding="utf-8")
print(output)
