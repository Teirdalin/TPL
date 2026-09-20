"""Inventory an RE_Kenshi project and create a non-destructive TPL migration kit.

This tool automates evidence collection and starter generation.  It deliberately
does not invent native addresses, C++ layouts, hook signatures, or lifecycle
equivalence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import uuid
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SKIP_DIRS = {
    ".git", ".vs", "backups", "build", "dist", "artifacts", "releases",
    "node_modules", "__pycache__", "x64", "x86",
}
TEXT_EXTENSIONS = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl",
    ".asm", ".def", ".json", ".md", ".props", ".ps1", ".py", ".sln",
    ".targets", ".txt", ".vcxproj", ".xml", ".yml", ".yaml",
}
NON_CODE_EXTENSIONS = {".md", ".txt", ".yml", ".yaml"}
MAX_FILES = 20000
MAX_TEXT_BYTES = 64 * 1024 * 1024
MAX_FILE_BYTES = 4 * 1024 * 1024

SIGNALS = (
    ("kenshilib-include", re.compile(r"#\s*include\s*[<\"](?:KenshiLib|kenshilib)[^>\"]*[>\"]", re.I), "native"),
    ("kenshilib-reference", re.compile(r"\bKenshiLib(?:::|\.dll|\.lib)", re.I), "native"),
    ("address-resolution", re.compile(r"\bGetRealAddress\s*\("), "native"),
    ("legacy-hook", re.compile(r"\bAddHook\s*\("), "native"),
    ("legacy-entry", re.compile(r"\bstartPlugin\s*\("), "entry"),
    ("tpl-entry", re.compile(r"\bTPL_Start\s*\("), "entry"),
    ("dynamic-loading", re.compile(r"\b(?:LoadLibrary(?:Ex)?[AW]?|GetProcAddress)\s*\("), "review"),
    ("engine-library", re.compile(r"\b(?:OgreMain|MyGUIEngine|ParticleUniverse)(?:_x64)?(?:\.dll|\.lib)?\b", re.I), "native"),
    ("dllmain", re.compile(r"\bDllMain\s*\("), "review"),
    ("custom-entrypoint", re.compile(r"(?:/ENTRY:|EntryPointSymbol|_DllMainCRTStartup)", re.I), "review"),
)


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def rel(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def iter_project_files(source: Path):
    count = 0
    for path in sorted(source.rglob("*"), key=lambda item: str(item).lower()):
        if any(part.lower() in SKIP_DIRS for part in path.relative_to(source).parts[:-1]):
            continue
        if path.is_symlink() or not path.is_file():
            continue
        count += 1
        if count > MAX_FILES:
            raise ValueError(f"Project exceeds the {MAX_FILES}-file scan limit")
        yield path


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def manifest_entries(data, key: str):
    value = data.get(key)
    if value is None:
        value = data.get(key[0].lower() + key[1:])
    result = []
    for item in value or []:
        if isinstance(item, str):
            result.append(item)
        elif isinstance(item, dict):
            candidate = item.get("dll") or item.get("DLL") or item.get("path")
            if isinstance(candidate, str):
                result.append(candidate)
    return result


def inspect_manifests(source: Path, files):
    manifests = []
    for path in files:
        if path.name.lower() != "re_kenshi.json":
            continue
        row = {"path": rel(path, source), "plugins": [], "preloadPlugins": [], "error": None}
        try:
            data = read_json(path)
            if not isinstance(data, dict):
                raise ValueError("root must be an object")
            row["plugins"] = manifest_entries(data, "Plugins")
            row["preloadPlugins"] = manifest_entries(data, "PreloadPlugins")
        except (OSError, ValueError, json.JSONDecodeError) as error:
            row["error"] = str(error)
        manifests.append(row)
    return manifests


def source_signals(source: Path, files):
    matches = []
    scanned = 0
    truncated = False
    for path in files:
        if path.suffix.lower() not in TEXT_EXTENSIONS:
            continue
        size = path.stat().st_size
        if size > MAX_FILE_BYTES:
            continue
        if scanned + size > MAX_TEXT_BYTES:
            truncated = True
            break
        scanned += size
        try:
            text = path.read_text(encoding="utf-8-sig", errors="replace")
        except OSError:
            continue
        for number, line in enumerate(text.splitlines(), 1):
            for name, pattern, category in SIGNALS:
                if pattern.search(line):
                    matches.append({"kind": name, "category": category,
                                    "classificationRelevant": path.suffix.lower() not in NON_CODE_EXTENSIONS,
                                    "path": rel(path, source), "line": number,
                                    "excerpt": line.strip()[:240]})
                    if len(matches) >= 2000:
                        return matches, scanned, True
    return matches, scanned, truncated


def find_declared_binaries(source: Path, files, manifests):
    by_name = {}
    for path in files:
        if path.suffix.lower() == ".dll":
            by_name.setdefault(path.name.lower(), []).append(path)
    declared = []
    for manifest in manifests:
        manifest_dir = source / Path(manifest["path"]).parent
        for phase, items in (("ordinary", manifest["plugins"]), ("preload", manifest["preloadPlugins"])):
            for item in items:
                normalized = item.replace("\\", "/")
                direct = (manifest_dir / normalized).resolve()
                candidates = [direct] if direct.is_file() else by_name.get(Path(normalized).name.lower(), [])
                declared.append({"manifest": manifest["path"], "phase": phase, "declared": item,
                                 "candidates": [rel(p, source) for p in candidates if source in p.parents]})
    return declared


def inspect_pe(path: Path, source: Path):
    row = {"path": rel(path, source), "sha256": sha256(path), "machine": None,
           "pe32Plus": False, "imports": [], "delayImports": [], "exports": [], "error": None}
    try:
        import pefile  # Bundled with TPL's analysis requirements.
        pe = pefile.PE(str(path), fast_load=False)
        row["machine"] = hex(pe.FILE_HEADER.Machine)
        row["pe32Plus"] = pe.OPTIONAL_HEADER.Magic == 0x20B and pe.FILE_HEADER.Machine == 0x8664
        for library in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            dll = library.dll.decode("ascii", "replace")
            names = []
            for item in library.imports:
                names.append(item.name.decode("ascii", "replace") if item.name else f"ordinal:{item.ordinal}")
            row["imports"].append({"dll": dll, "symbols": names})
        for library in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
            dll = library.dll.decode("ascii", "replace")
            names = []
            for item in library.imports:
                names.append(item.name.decode("ascii", "replace") if item.name else f"ordinal:{item.ordinal}")
            row["delayImports"].append({"dll": dll, "symbols": names})
        directory = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
        if directory:
            row["exports"] = [item.name.decode("ascii", "replace") if item.name else f"ordinal:{item.ordinal}"
                              for item in directory.symbols]
        pe.close()
    except ImportError:
        row["error"] = "pefile is unavailable; install tools/requirements-analysis.txt and rerun"
    except Exception as error:  # PE parser error text belongs in the report.
        row["error"] = str(error)
    return row


def analyze(source: Path):
    files = list(iter_project_files(source))
    manifests = inspect_manifests(source, files)
    signals, scanned, truncated = source_signals(source, files)
    declared = find_declared_binaries(source, files, manifests)
    binary_paths = []
    for item in declared:
        for candidate in item["candidates"]:
            path = source / candidate
            if path not in binary_paths:
                binary_paths.append(path)
    binaries = [inspect_pe(path, source) for path in binary_paths]

    kinds = Counter(item["kind"] for item in signals)
    import_names = {library["dll"].lower() for binary in binaries
                    for library in binary["imports"] + binary["delayImports"]}
    relevant = [item for item in signals if item["classificationRelevant"]]
    relevant_kinds = Counter(item["kind"] for item in relevant)
    has_native = any(item["category"] == "native" for item in relevant) or "kenshilib.dll" in import_names
    has_preload = any(item["preloadPlugins"] for item in manifests)
    has_dynamic = bool(relevant_kinds["dynamic-loading"])
    old_import = bool({"kenshilib.dll", "re_kenshi.dll"} & import_names)
    legacy_export = any("?startPlugin@@YAXXZ" in binary["exports"] for binary in binaries)
    all_x64 = bool(binaries) and all(binary["pe32Plus"] for binary in binaries if not binary["error"])

    blockers = []
    if not manifests:
        blockers.append("No RE_Kenshi.json was found; plugin DLL ownership and load phase need manual identification.")
    if any(item["error"] for item in manifests):
        blockers.append("At least one legacy manifest could not be parsed.")
    if has_preload:
        blockers.append("PreloadPlugins require an earlier lifecycle contract; TPL must not silently run them at the main menu.")
    if has_native:
        blockers.append("Native game/KenshiLib dependencies require reviewed ABI bindings; they cannot be mechanically translated.")
    if has_dynamic:
        blockers.append("Dynamic loading/symbol lookup requires manual dependency review beyond PE imports.")
    if old_import:
        blockers.append("A declared DLL still imports RE_Kenshi/KenshiLib and will not become TPL-native by changing its manifest.")

    if has_native or old_import:
        classification = "native-bridge-required"
    elif has_preload:
        classification = "early-lifecycle-required"
    elif legacy_export and all_x64 and binaries:
        classification = "legacy-fallback-candidate"
    elif relevant_kinds["legacy-entry"] and not has_dynamic:
        classification = "simple-source-port-candidate"
    else:
        classification = "manual-inventory-required"

    return {
        "schema": 1,
        "source": str(source),
        "scan": {"files": len(files), "textBytes": scanned, "truncated": truncated,
                 "excludedDirectories": sorted(SKIP_DIRS)},
        "classification": classification,
        "blockers": blockers,
        "manifests": manifests,
        "declaredBinaries": declared,
        "binaries": binaries,
        "sourceSignals": signals,
        "signalCounts": dict(sorted(kinds.items())),
        "claims": {"runtimeVerified": False, "gameplayVerified": False,
                   "addressesGenerated": False, "sourceModified": False},
    }


def safe_name(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_]", "", value.replace(" ", "_"))
    if not result or not result[0].isalpha():
        result = "Plugin_" + result
    return result[:48]


def infer_name(source: Path, report) -> str:
    for manifest in report["manifests"]:
        entries = manifest["plugins"] + manifest["preloadPlugins"]
        if entries:
            return safe_name(Path(entries[0].replace("\\", "/")).stem)
    return safe_name(source.name)


def write_json(path: Path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def create_starter(destination: Path, name: str, display_name: str):
    starter = destination / "starter"
    template = ROOT / "templates" / "plugin"
    mapping = {
        "plugin.cpp.in": "src/plugin.cpp", "plugin.vcxproj.in": f"{name}.vcxproj",
        "README.md.in": "README.md", "plugin.cfg": "plugin.cfg", "build.ps1": "build.ps1",
        "package.ps1": "package.ps1", "deploy.ps1": "deploy.ps1", "project-tools.ps1": "project-tools.ps1",
    }
    guid = str(uuid.uuid4()).upper()
    for original, target in mapping.items():
        text = (template / original).read_text(encoding="utf-8")
        text = text.replace("__TPL_PLUGIN_NAME__", name).replace("__TPL_PROJECT_GUID__", guid)
        output = starter / target
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8")
    include = starter / "sdk" / "include"
    licenses = starter / "sdk" / "Licenses"
    include.mkdir(parents=True)
    licenses.mkdir(parents=True)
    for header in (ROOT / "include").iterdir():
        if header.is_file() and header.suffix.lower() in {".h", ".hpp"}:
            shutil.copy2(header, include / header.name)
    shutil.copy2(ROOT / "PLUGIN_API_PERMISSION.md", starter / "sdk" / "PLUGIN_API_PERMISSION.md")
    shutil.copy2(ROOT / "Licenses" / "JDL-1.txt", licenses / "JDL-1.txt")
    shutil.copy2(ROOT / "Licenses" / "JDL.png", licenses / "JDL.png")
    write_json(starter / "TPL.json", {"plugins": [{"name": display_name, "dll": f"{name}.dll"}]})
    write_json(starter / "plugin-project.json", {"schema": 1, "name": name, "displayName": display_name,
                                                   "version": "0.1.0", "tplMinimum": "0.1.0"})
    (starter / ".gitignore").write_text("build/\ndist/\nbackups/\n.vs/\n*.user\n", encoding="utf-8")


def migration_markdown(report, name: str) -> str:
    counts = report["signalCounts"]
    lines = [
        f"# {name}: generated TPL migration inventory", "",
        f"Classification: **{report['classification']}**", "",
        "This kit is an inventory and clean TPL starter, not a claim that the old plugin has been converted.",
        "The converter did not modify the source project, generate native addresses, launch Kenshi, or validate gameplay.", "",
        "## Findings", "",
        f"- Scanned {report['scan']['files']} files and {report['scan']['textBytes']} bytes of text.",
        f"- Found {len(report['manifests'])} legacy manifests and inspected {len(report['binaries'])} declared DLL candidates.",
    ]
    if report["scan"]["truncated"]:
        lines.append("- The bounded text scan reached its limit; treat the inventory as incomplete.")
    if counts:
        lines.append("- Source signal counts: " + ", ".join(f"{key}={value}" for key, value in counts.items()) + ".")
    lines.extend(["", "## Blocking review items", ""])
    if report["blockers"]:
        lines.extend(f"- {item}" for item in report["blockers"])
    else:
        lines.append("- No automatic blocker was found. Manual lifecycle and gameplay review is still required.")
    lines.extend(["", "## Generated starter", "",
                  "`starter/` is an independent minimal TPL plugin project. Build it first and confirm its two log messages.",
                  "Then migrate independent logic one feature at a time. Do not paste native Kenshi calls into the starter",
                  "until their ABI, module identity, address correspondence, ownership, and call phase have been reviewed.", "",
                  "## Required path by classification", ""])
    routes = {
        "native-bridge-required": "Use a reviewed bootstrap/native bridge or replace the native calls with supported TPL services. Preserve VC100 x64 ABI code where exact game C++ layouts remain necessary.",
        "early-lifecycle-required": "Keep the preload feature disabled until TPL exposes and validates a genuinely equivalent early lifecycle phase.",
        "legacy-fallback-candidate": "The existing DLL may be eligible for TPL's experimental legacy fallback. Verify the binary report and test it without removing the known-good installation.",
        "simple-source-port-candidate": "Move the entry logic behind C `TPL_Start`, use TPL logging/owned services, and keep exceptions and C++ ownership inside the plugin.",
        "manual-inventory-required": "Identify the actual plugin DLL, entrypoint, dependencies, and load phase before editing code.",
    }
    lines.append(routes[report["classification"]])
    lines.extend(["", "## Acceptance gates", "",
                  "1. Preserve the known-good DLL/PDB, manifests, settings, assets, and hashes.",
                  "2. Resolve every native binding against the actual executable and reject ambiguous matches.",
                  "3. Build hooks as a disabled transaction and test rollback plus combined target spacing.",
                  "4. Inspect final imports/exports; TPL-native DLLs must not retain hidden old-runtime dependencies.",
                  "5. Package every required DLL and manifest, deploy with Kenshi closed, and verify hashes.",
                  "6. Run feature-specific live acceptance, save/load, main-menu return, and coexistence tests.", "",
                  "Read `migration-report.json` for machine-readable evidence and exact source locations.", ""])
    return "\n".join(lines)


def run(argv=None):
    parser = argparse.ArgumentParser(description="Create a non-destructive RE_Kenshi-to-TPL migration inventory and starter")
    parser.add_argument("source", type=Path, help="RE_Kenshi plugin project directory")
    parser.add_argument("--output", type=Path, required=True, help="new migration-kit directory")
    parser.add_argument("--name", help="ASCII TPL project identifier")
    parser.add_argument("--display-name", help="manifest display name")
    args = parser.parse_args(argv)
    source = args.source.resolve()
    output = args.output.resolve()
    if not source.is_dir():
        parser.error(f"source directory does not exist: {source}")
    if output.exists():
        parser.error(f"output already exists (nothing was overwritten): {output}")
    report = analyze(source)
    name = args.name or infer_name(source, report)
    if (not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,47}", name) or
            re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])", name, re.I)):
        parser.error("--name must be 1-48 ASCII letters/digits/underscores and start with a letter")
    display_name = args.display_name or name.replace("_", " ")
    if len(display_name) > 100 or any(ord(char) < 32 for char in display_name):
        parser.error("--display-name must be a single line of at most 100 characters")
    output.mkdir(parents=True)
    try:
        report["project"] = {"name": name, "displayName": display_name}
        write_json(output / "migration-report.json", report)
        (output / "MIGRATION_PLAN.md").write_text(migration_markdown(report, display_name), encoding="utf-8")
        create_starter(output, name, display_name)
    except BaseException:
        shutil.rmtree(output, ignore_errors=True)
        raise
    print(json.dumps({"output": str(output), "classification": report["classification"],
                      "blockers": len(report["blockers"]), "sourceModified": False}))
    return 0


if __name__ == "__main__":
    sys.exit(run())
