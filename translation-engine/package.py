"""Build a deterministic, separately downloaded translation component. No install/deploy actions."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile


def imports(path):
    """Read PE import names without executing an untrusted DLL or requiring extra packages."""
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"Not a PE image: {path}")
    sections, optional_size = struct.unpack_from("<H12xH", data, pe + 6)
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise ValueError(f"Not a PE32+ image: {path}")
    import_rva = struct.unpack_from("<I", data, optional + 120)[0]
    table = optional + optional_size

    def offset(rva):
        for i in range(sections):
            size, base, raw_size, raw = struct.unpack_from("<IIII", data, table + i * 40 + 8)
            if base <= rva < base + max(size, raw_size):
                return raw + rva - base
        raise ValueError(f"Invalid RVA in {path}")

    if not import_rva:
        return []
    pos = offset(import_rva)
    names = []
    while any(data[pos:pos + 20]):
        name = offset(struct.unpack_from("<I", data, pos + 12)[0])
        end = data.index(b"\0", name)
        names.append(data[name:end].decode("ascii").lower())
        pos += 20
    return names


SYSTEM_DLLS = {
    "kernel32.dll", "advapi32.dll", "bcrypt.dll", "user32.dll", "ntdll.dll",
    "ole32.dll", "shell32.dll", "shlwapi.dll", "ws2_32.dll", "psapi.dll",
    "msvcrt.dll", "ucrtbase.dll", "version.dll", "secur32.dll", "dbghelp.dll",
    # The product already requires the VC runtime. No redistributables are taken from System32.
    "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "vcruntime140.dll",
    "vcruntime140_1.dll", "vcomp140.dll", "concrt140.dll",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--ct2", type=Path, required=True)
    parser.add_argument("--dependencies", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--source-spm", type=Path, required=True)
    parser.add_argument("--target-spm", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    paths = [args.build / "Release", args.build / "ct2" / "Release", args.dependencies / "bin"]
    available = {p.name.lower(): p for directory in paths for p in directory.glob("*.dll")}
    files = {}
    pending = ["yantai_translate.dll"]
    while pending:
        name = pending.pop()
        if name in files:
            continue
        if re.search(r"(mkl|iomp|cuda|cudnn|cublas)", name, re.I):
            raise ValueError(f"Forbidden non-CPU/MKL dependency: {name}")
        if name not in available:
            if name in SYSTEM_DLLS or name.startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            raise ValueError(f"Unresolved dependency: {name}")
        files[name] = available[name]
        pending.extend(imports(files[name]))
    for name in ["model.bin", "config.json", "shared_vocabulary.json"]:
        files[name] = args.model / name
    files["source.spm"] = args.source_spm
    files["target.spm"] = args.target_spm
    files["NOTICE.md"] = root / "NOTICE.md"
    files["licenses/yantai-GPL-3.0.txt"] = root.parent / "LICENSE"
    files["licenses/CTranslate2.txt"] = args.ct2 / "LICENSE"
    files["licenses/cpu_features.txt"] = args.ct2 / "third_party" / "cpu_features" / "LICENSE"
    files["licenses/spdlog.txt"] = args.ct2 / "third_party" / "spdlog" / "LICENSE"
    for port in ["onednn", "sentencepiece", "protobuf", "abseil", "utf8-range"]:
        files[f"licenses/{port}.txt"] = args.dependencies / "share" / port / "copyright"

    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / "translate-zh-en-v1.zip"
    hashes = {}
    binary_size = sum(path.stat().st_size for name, path in files.items() if name.endswith(".dll"))
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as package:
        for name, path in sorted(files.items()):
            content = path.read_bytes()
            hashes[name] = {"size": len(content), "sha256": hashlib.sha256(content).hexdigest()}
            info = zipfile.ZipInfo(name, (2026, 11, 8, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            package.writestr(info, content, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    manifest = {
        "asset": archive.name,
        "url": "https://github.com/Yu-DaTouX/yantai-ime/releases/download/translate-model-v1/" + archive.name,
        "size": archive.stat().st_size,
        "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "engine_binary_bytes": binary_size,
        "files": hashes,
    }
    (args.output / "component.lock.json").write_bytes(
        (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    (args.output / "SHA256SUMS.txt").write_bytes(
        (manifest["sha256"] + "  " + archive.name + "\n").encode("ascii"))
    print(json.dumps({key: value for key, value in manifest.items() if key != "files"}, indent=2))


if __name__ == "__main__":
    main()
