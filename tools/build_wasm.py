"""Build the pinned C++20/Wasm runtime and record portable source/output provenance."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("EMSDK", ROOT / ".tools" / "emsdk-main"))
COMPILER = SDK / "upstream" / "emscripten" / "em++.py"
if not COMPILER.is_file():
    raise SystemExit("Emscripten SDK not found. See README.md, or set EMSDK.")

env = os.environ.copy()
env["EM_CONFIG"] = str(SDK / ".emscripten")
version = subprocess.check_output(
    [sys.executable, str(COMPILER), "--version"], env=env, text=True
).splitlines()[0]
if "4.0.23" not in version:
    raise SystemExit(f"Expected Emscripten 4.0.23; found {version}")


def file_hash(relative_path: str, *, binary: bool = False) -> str:
    contents = (ROOT / relative_path).read_bytes()
    if not binary:
        contents = contents.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return hashlib.sha256(contents).hexdigest()


def source_hashes() -> dict[str, str]:
    inputs = ["native/sources.txt", "tools/build_wasm.py"]
    inputs.extend(
        path.relative_to(ROOT).as_posix()
        for path in (ROOT / "native").rglob("*")
        if path.suffix in {".cpp", ".h"}
    )
    return {source: file_hash(source) for source in sorted(inputs)}


sources = [
    line.strip()
    for line in (ROOT / "native" / "sources.txt").read_text(encoding="utf-8").splitlines()
    if line.strip()
]
(ROOT / "wasm").mkdir(exist_ok=True)
# Keep all recorded paths portable. C assert checks stay enabled in native Debug.
arguments = [
    "-std=c++20", "-O3", "-DNDEBUG", "-ffp-contract=off", "-fexceptions",
    "-Wall", "-Wextra", "-Werror", "--bind", "--no-entry", "-I", "native/src",
    *sources,
    "-sMODULARIZE=1", "-sEXPORT_NAME=createPBDModule", "-sENVIRONMENT=web,node",
    "-sALLOW_MEMORY_GROWTH=1", "-sEXPORT_EXCEPTION_HANDLING_HELPERS=1",
    "-sINITIAL_MEMORY=33554432", "-o", "wasm/pbd_core.js",
]
before = source_hashes()
subprocess.run([sys.executable, str(COMPILER), *arguments], cwd=ROOT, env=env, check=True)
if source_hashes() != before:
    raise SystemExit("Build inputs changed during compilation. Rebuild before publishing the runtime.")
manifest = {
    "schemaVersion": 1,
    "sdk": {"name": "emscripten", "version": "4.0.23"},
    "languageStandard": "c++20",
    "textNormalization": "LF",
    "command": ["em++", *arguments],
    "sourceHashes": before,
    "outputHashes": {
        "wasm/pbd_core.js": file_hash("wasm/pbd_core.js"),
        "wasm/pbd_core.wasm": file_hash("wasm/pbd_core.wasm", binary=True),
    },
}
manifest_path = ROOT / "wasm" / "build-manifest.json"
temporary_path = manifest_path.with_suffix(".json.tmp")
temporary_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
temporary_path.replace(manifest_path)
print(version)
print("Built wasm/pbd_core.js + wasm/pbd_core.wasm; recorded wasm/build-manifest.json")
