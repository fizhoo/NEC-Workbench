#!/usr/bin/env python3
"""Writes the Qt and bundled-library section of THIRD_PARTY_LICENSES.txt for a deployed
package, from what is actually in it, to standard output.

  qt-notices.py --platform linux --deployed AppDir/usr --qt-version 6.8.3 \
      --qt-source qtbase-everywhere-src-6.8.3.tar.xz --qt-source qtsvg-...tar.xz \
      --qt-lib-dir "$QT_ROOT_DIR/lib" [--icu-license LICENSE]
  qt-notices.py --platform windows --deployed package --qt-version 6.8.3 --qt-source ... \
      --append package/THIRD_PARTY_LICENSES.txt

It lists the Qt libraries, plugins and translations found in the package, the third-party
code inside those Qt modules (from the qt_attribution.json files of the exact Qt source),
and on Linux every other library linuxdeploy bundled, with its Debian/Ubuntu copyright
file. License texts come from the Qt source's LICENSES folders. It fails on anything it
cannot account for, so the notices cannot silently go stale.
"""

import argparse
import json
import re
import subprocess
import sys
import tarfile
from pathlib import Path, PurePosixPath

RULE = "=" * 88

# Qt library name (Qt6<name>) -> the Qt source module it comes from and its doc module.
QT_MODULES = {
    "Core": ("qtbase", "qtcore"), "Gui": ("qtbase", "qtgui"), "Widgets": ("qtbase", "qtwidgets"),
    "Network": ("qtbase", "qtnetwork"), "DBus": ("qtbase", "qtdbus"), "XcbQpa": ("qtbase", "qtgui"),
    "OpenGL": ("qtbase", "qtopengl"), "OpenGLWidgets": ("qtbase", "qtopenglwidgets"),
    "PrintSupport": ("qtbase", "qtprintsupport"), "Concurrent": ("qtbase", "qtconcurrent"),
    "Xml": ("qtbase", "qtxml"), "Svg": ("qtsvg", "qtsvg"), "SvgWidgets": ("qtsvg", "qtsvgwidgets"),
}

# Attributions for code that is not in a desktop build for this platform.
NOT_FOR = {
    "any": ("android", "webassembly", "macos", "cocoa", "build system", "arm neon",
            "development tools only"),
    "linux": ("windows", "direct 3d", "d3d12", "wintab"),
    "windows": ("xcb", "unix", "x server"),
}


def fail(message):
    print(f"qt-notices.py: {message}", file=sys.stderr)
    sys.exit(1)


class QtSources:
    """The attribution files and license texts of the exact Qt source tarballs."""

    def __init__(self, tarballs):
        self.attributions = []  # (module, attribution directory, entry)
        self.files = {}  # path inside the module -> text, for license files
        for tarball in tarballs:
            module = Path(tarball).name.split("-everywhere-src-")[0]
            with tarfile.open(tarball) as archive:
                members = {}
                for member in archive:
                    if not member.isfile():
                        continue
                    relative = PurePosixPath(*PurePosixPath(member.name).parts[1:])
                    members[relative] = member
                    if relative.name == "qt_attribution.json":
                        text = archive.extractfile(member).read().decode("utf-8")
                        data = json.loads(text, strict=False)
                        for entry in data if isinstance(data, list) else [data]:
                            self.attributions.append((module, relative.parent, entry))
                wanted = {(module, path) for module, folder, entry in self.attributions
                          for path in license_paths(folder, entry)}
                for relative, member in members.items():
                    if relative.parts[0] == "LICENSES" or (module, relative) in wanted:
                        self.files[(module, relative)] = archive.extractfile(member).read().decode(
                            "utf-8", errors="replace")

    def license_text(self, module, license_id):
        for key in ((module, PurePosixPath("LICENSES", f"{license_id}.txt")),
                    ("qtbase", PurePosixPath("LICENSES", f"{license_id}.txt"))):
            if key in self.files:
                return self.files[key]
        return None


def license_paths(folder, entry):
    files = entry.get("LicenseFile") or entry.get("LicenseFiles") or []
    return [folder / name for name in ([files] if isinstance(files, str) else files)]


def license_ids(expression):
    return [token for token in re.split(r"\s+(?:AND|OR|WITH)\s+|[()]", expression or "") if token.strip()]


def copyright_lines(entry):
    value = entry.get("Copyright") or ""
    return value if isinstance(value, list) else value.splitlines()


def deployed_qt(platform, deployed):
    """The Qt libraries, plugins and translations in the package, and the other files."""
    libraries, plugins, translations, others = [], [], [], []
    if platform == "linux":
        for path in sorted((deployed / "lib").glob("*.so*")):
            match = re.fullmatch(r"libQt6(\w+)\.so(\.\d+)*", path.name)
            (libraries if match else others).append(match.group(1) if match else path.name)
        plugin_root = deployed / "plugins"
        plugins = sorted(str(p.relative_to(plugin_root)) for p in plugin_root.rglob("*.so"))
        translations = sorted(p.name for p in (deployed / "translations").glob("*.qm"))
    else:
        for path in sorted(deployed.glob("*.dll")):
            match = re.fullmatch(r"Qt6(\w+)\.dll", path.name, re.IGNORECASE)
            (libraries if match else others).append(match.group(1) if match else path.name)
        for folder in sorted(p for p in deployed.iterdir() if p.is_dir()):
            if folder.name in ("solvers", "examples"):
                continue
            if folder.name == "translations":
                translations = sorted(p.name for p in folder.glob("*.qm"))
                continue
            plugins += sorted(str(p.relative_to(deployed)).replace("\\", "/") for p in folder.rglob("*.dll"))
    unknown = [name for name in libraries if name not in QT_MODULES]
    if unknown:
        fail(f"Qt libraries with no known source module: {', '.join(unknown)}; add them to QT_MODULES")
    return libraries, plugins, translations, others


def relevant(platform, doc_modules, entry, folder):
    if entry.get("QDocModule") not in doc_modules:
        return False
    if any(part in ("tests", "util", "cmake") for part in folder.parts):
        return False
    usage = (entry.get("QtUsage") or "").lower()
    return not any(word in usage for word in NOT_FOR["any"] + NOT_FOR[platform])


def debian_package(name):
    """The Debian/Ubuntu package that installed a bundled library, its version, source
    package and copyright file."""
    for folder in ("/usr/lib/x86_64-linux-gnu", "/lib/x86_64-linux-gnu", "/usr/lib", "/lib"):
        result = subprocess.run(["dpkg", "-S", f"{folder}/{name}"], capture_output=True, text=True)
        if result.returncode == 0:
            package = result.stdout.split(":", 1)[0].strip()
            query = subprocess.run(
                ["dpkg-query", "-W", "-f=${Version}\t${source:Package}\t${source:Version}", package],
                capture_output=True, text=True, check=True).stdout.split("\t")
            copyright_file = Path("/usr/share/doc", package, "copyright")
            if not copyright_file.is_file():
                fail(f"{name}: no copyright file for package {package}")
            return package, query[0], query[1], query[2], copyright_file.read_text(errors="replace")
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", choices=("linux", "windows"), required=True)
    parser.add_argument("--deployed", type=Path, required=True)
    parser.add_argument("--qt-version", required=True)
    parser.add_argument("--qt-source", action="append", required=True)
    parser.add_argument("--qt-lib-dir", type=Path)
    parser.add_argument("--icu-license", type=Path)
    parser.add_argument("--append", type=Path, help="append to this file (UTF-8) instead of printing")
    args = parser.parse_args()

    version = args.qt_version
    series = ".".join(version.split(".")[:2])
    sources = QtSources(args.qt_source)
    libraries, plugins, translations, others = deployed_qt(args.platform, args.deployed)
    modules_needed = {QT_MODULES[name][0] for name in libraries}
    modules_given = {module for module, _, _ in sources.attributions}
    if not modules_needed <= modules_given:
        fail(f"Qt source modules missing: {', '.join(sorted(modules_needed - modules_given))}")
    doc_modules = {QT_MODULES[name][1] for name in libraries}

    out = []
    write = out.append
    write(f"\n\n{RULE}\nQt {version}\n{RULE}\n")
    write(f"""What:    The Qt {version} toolkit (The Qt Company Ltd. and other contributors), which the
         NEC Workbench user interface is built with.
License: GNU Lesser General Public License version 3 (LGPL-3.0-only), whose full text
         follows below; it adds permissions to the GNU General Public License version 3,
         whose full text is above. NEC Workbench links the Qt libraries dynamically: they
         are the separate files listed here, and you may replace them with your own build
         of Qt {version} (or a compatible version) to run NEC Workbench with it.
Source:  The corresponding source of this exact Qt release:
         https://download.qt.io/archive/qt/{series}/{version}/single/qt-everywhere-src-{version}.tar.xz
         (or, per module, https://download.qt.io/archive/qt/{series}/{version}/submodules/).

Qt libraries in this package (Qt source modules: {', '.join(sorted(modules_needed))}):
""")
    for name in libraries:
        write(f"  Qt6{name}\n")
    write("\nQt plugins in this package:\n")
    for name in plugins:
        write(f"  {name}\n")
    if translations:
        write(f"\nQt translations in this package (from the qttranslations module): "
              f"{len(translations)} files, {translations[0]} … {translations[-1]}\n")

    write(f"\nThird-party code in these Qt {version} modules, as Qt's qt_attribution.json files list it:\n")
    texts = {}
    for module, folder, entry in sorted(sources.attributions, key=lambda a: (a[2].get("Name") or "").lower()):
        if not relevant(args.platform, doc_modules, entry, folder):
            continue
        write(f"\n  {entry.get('Name')}{' ' + entry['Version'] if entry.get('Version') else ''}"
              f" ({module}/{folder})\n")
        write(f"    License: {entry.get('LicenseId') or entry.get('License')}\n")
        if entry.get("QtUsage"):
            write(f"    Use:     {' '.join(entry['QtUsage'].split())}\n")
        for line in copyright_lines(entry):
            if line.strip():
                write(f"    {line.strip()}\n")
        for license_id in license_ids(entry.get("LicenseId")):
            text = sources.license_text(module, license_id)
            if text is None:
                files = [sources.files.get((module, path)) for path in license_paths(folder, entry)]
                text = "\n\n".join(f for f in files if f)
            if not text:
                fail(f"no license text for {license_id} ({entry.get('Name')})")
            texts.setdefault(license_id, text)

    write("\nOther libraries in this package:\n")
    other_texts = []
    if args.platform == "windows":
        if others:
            fail(f"bundled DLLs with no notice: {', '.join(others)}")
        write("  None: the Windows package carries only the Qt files above beside nec-workbench.exe.\n")
    else:
        for name in others:
            if name.startswith("libicu"):
                if args.icu_license is None or args.qt_lib_dir is None or not (args.qt_lib_dir / name).exists():
                    fail(f"{name}: an ICU library not from the Qt installation, or no --icu-license")
                write(f"  {name}: ICU (International Components for Unicode), shipped with the Qt {version}\n"
                      f"    binaries; Unicode License V3 / ICU license, full text below.\n")
                other_texts.append(("ICU", args.icu_license.read_text(errors="replace")))
                continue
            found = debian_package(name)
            if found is None:
                fail(f"{name}: bundled library from no known package")
            package, package_version, source, source_version, copyright_text = found
            write(f"  {name}: from Ubuntu package {package} {package_version}\n"
                  f"    Source: https://launchpad.net/ubuntu/+source/{source}/{source_version}\n")
            other_texts.append((f"{package} (copyright file)", copyright_text))
        if not others:
            write("  None.\n")

    write(f"\n\n{RULE}\nLicense texts for Qt and the libraries above\n{RULE}\n")
    lgpl = sources.license_text("qtbase", "LGPL-3.0-only")
    if lgpl is None:
        fail("no LGPL-3.0-only text in the Qt source")
    texts["LGPL-3.0-only"] = lgpl
    for license_id in ["LGPL-3.0-only"] + sorted(k for k in texts if k != "LGPL-3.0-only"):
        if license_id == "GPL-3.0-only":
            write("\n\n--- GPL-3.0-only ---\n\nThe GNU General Public License version 3: the full text "
                  "is above, after the nec2c notice.\n")
            continue
        write(f"\n\n--- {license_id} ---\n\n{texts[license_id].strip()}\n")
    seen = set()
    for title, text in other_texts:
        if title in seen:
            continue
        seen.add(title)
        write(f"\n\n--- {title} ---\n\n{text.strip()}\n")
    text = "".join(out)
    if args.append is None:
        sys.stdout.write(text)
        return
    with open(args.append, "a", encoding="utf-8", newline="\n") as notices:
        notices.write(text)
    print(f"Qt {version}: {len(libraries)} libraries, {len(plugins)} plugins, {len(translations)} "
          f"translations, {len(others)} other libraries, {len(texts)} license texts -> {args.append}")


if __name__ == "__main__":
    main()
