#!/usr/bin/env python3
"""Generate a CycloneDX SBOM for the MicroPython repository.

Catalogues two flavours of third-party dependency:

1. Git submodules registered in .gitmodules — pinned commit SHA comes from
   the parent repo's gitlink entries, version is resolved via the layered
   strategy below.
2. Vendored libraries copied into ``lib/`` (e.g., littlefs, oofatfs, uzlib)
   that are not git submodules. Versions are extracted from in-tree
   files when the upstream embeds one, otherwise the component is emitted
   with ``version:source=vendored-unknown`` so the gap is explicit.

For submodules the version resolution is layered:

    1. in-tree extractor — read the version directly from a known file in the
       checked-out submodule (header macros, CMake variables, autoconf, etc.).
       Most authoritative because it is what actually gets compiled.
    2. ``git ls-remote`` tag match — query the upstream and look for a tag
       pointing at the pinned commit.
    3. short commit SHA — final fallback when nothing else identifies the
       pinned commit (forks, branch-tracking submodules, frozen sources).

The ``version:source`` property on each component records which strategy was
used. If an in-tree extractor's expected file or field is missing, the
script reports the failure and exits non-zero — that condition means
upstream has moved the version source and the registry must be updated;
silently falling back would mask the regression. Submodules that simply
aren't checked out trigger a warning and fall through to ls-remote/SHA,
since that is a user-environment issue rather than a script bug.

Usage:
    python tools/sbom.py                    # full resolution (network)
    python tools/sbom.py --offline          # extractors + SHA only
    python tools/sbom.py -o sbom.json

Known coverage gaps (deliberately not yet implemented):

* Recursive sub-submodules. pico-sdk, btstack, and mbedtls each pull in
  their own submodules; we only catalogue the top level.
* Per-port vendored code under ports/*/ — e.g., ports/cc3200/simplelink,
  ports/stm32/usbhost, ports/esp32/managed_components.
* Build-time fetched SDKs that aren't checked in — e.g., ESP-IDF for the
  esp32 port, the Alif toolkit downloads, the emsdk/ tree (gitignored).
* MicroPython-internal trees in lib/ are deliberately excluded
  (lib/mbedtls_errors is MP's own code-generation tooling, not a
  third-party dependency).
"""

import argparse
import datetime
import json
import re
import subprocess
import sys
import uuid
from pathlib import Path
from urllib.parse import urlparse


def run(args, cwd=None):
    return subprocess.run(
        args,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ).stdout


def parse_gitmodules(repo):
    """Return list of submodule dicts {name, path, url, branch?}."""
    text = run(
        ["git", "config", "--file", ".gitmodules", "--list"],
        cwd=str(repo),
    )
    sections = {}
    for line in text.splitlines():
        m = re.match(r"^submodule\.(.+)\.([^=]+)=(.*)$", line)
        if not m:
            continue
        name, key, value = m.group(1), m.group(2), m.group(3)
        sections.setdefault(name, {"name": name})[key] = value
    return [s for s in sections.values() if "path" in s and "url" in s]


def submodule_sha(repo, path):
    """Pinned commit SHA for a submodule, read from the parent repo gitlink."""
    text = run(["git", "ls-tree", "HEAD", path], cwd=str(repo))
    parts = text.split()
    if len(parts) < 3 or parts[1] != "commit":
        return None
    return parts[2]


def ls_remote_tags(url, timeout=60):
    """Return {commit_sha: tag_name} for tags published on the remote.

    For annotated tags ``git ls-remote`` returns both ``refs/tags/<t>`` (the
    tag object SHA) and ``refs/tags/<t>^{}`` (the commit it points at). We
    prefer the dereferenced form so the SHA matches the gitlink commit.
    """
    try:
        text = subprocess.run(
            ["git", "ls-remote", "--tags", url],
            check=True,
            text=True,
            timeout=timeout,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
        print(f"warning: ls-remote failed for {url}: {e}", file=sys.stderr)
        return {}
    tags = {}
    deref = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        sha, ref = line.split("\t", 1)
        if not ref.startswith("refs/tags/"):
            continue
        tag = ref[len("refs/tags/") :]
        if tag.endswith("^{}"):
            deref[sha] = tag[:-3]
        else:
            tags.setdefault(sha, tag)
    merged = dict(tags)
    merged.update(deref)  # dereferenced wins for annotated tags
    return merged


def url_to_purl(url, sha):
    """Build a Package URL (purl) for a git submodule."""
    parsed = urlparse(url)
    host = parsed.netloc.lower()
    path = parsed.path.lstrip("/")
    if path.endswith(".git"):
        path = path[:-4]
    if host == "github.com":
        return f"pkg:github/{path}@{sha}"
    name = path.rsplit("/", 1)[-1] or host
    return f"pkg:generic/{name}@{sha}?vcs_url={url}"


# --- in-tree version extractors --------------------------------------------
#
# Each extractor takes the repo root (Path) and returns a version string, or
# raises ExtractorError if its expected source file/field is missing or
# unparseable. A failing extractor signals that the upstream library has
# changed where it stores its version — the registry needs to be updated, and
# the script must NOT silently mask the failure with a tag- or SHA-fallback.
#
# Submodules that aren't checked out are detected separately (see
# resolve_version) and are handled with a warning + fall-through, since that
# is a user-environment issue, not an extractor bug.


class ExtractorError(Exception):
    """An in-tree extractor's expected file or field is missing/unparseable."""


def _read(repo, relpath):
    """Read a file from the repo. Raises ExtractorError if missing/unreadable."""
    try:
        return (repo / relpath).read_text(errors="replace")
    except FileNotFoundError:
        raise ExtractorError(f"file not found: {relpath}")
    except OSError as e:
        raise ExtractorError(f"cannot read {relpath}: {e}")


def _defines(text, names, source):
    """Pull ``#define NAME VALUE`` pairs; raise on any missing."""
    out = {}
    missing = []
    for name in names:
        m = re.search(
            rf"^\s*#\s*define\s+{re.escape(name)}\s+\(?\s*\"?([^\s\")]+)",
            text,
            re.MULTILINE,
        )
        if m:
            out[name] = m.group(1).rstrip("U")
        else:
            missing.append(name)
    if missing:
        raise ExtractorError(f"{source}: missing #define(s): {', '.join(missing)}")
    return out


def _cmake_set(text, names, source):
    """Pull ``set(NAME VALUE)`` pairs from a CMake file; raise on any missing."""
    out = {}
    missing = []
    for name in names:
        m = re.search(rf"set\s*\(\s*{re.escape(name)}\s+\"?([^\s\")]+)", text, re.IGNORECASE)
        if m:
            out[name] = m.group(1)
        else:
            missing.append(name)
    if missing:
        raise ExtractorError(f"{source}: missing CMake set(): {', '.join(missing)}")
    return out


def _make_assigns(text, names, source):
    """Pull ``KEY = VALUE`` pairs from a Make-style fragment; raise on any missing."""
    out = {}
    missing = []
    for name in names:
        m = re.search(rf"^\s*{re.escape(name)}\s*=\s*(\S+)", text, re.MULTILINE)
        if m:
            out[name] = m.group(1)
        else:
            missing.append(name)
    if missing:
        raise ExtractorError(f"{source}: missing key(s): {', '.join(missing)}")
    return out


def _search(pattern, text, source, flags=0):
    """Run a regex; raise ExtractorError if it does not match."""
    m = re.search(pattern, text, flags)
    if not m:
        raise ExtractorError(f"{source}: pattern did not match: {pattern}")
    return m


def _ext_lwip(repo):
    rel = "lib/lwip/src/include/lwip/init.h"
    v = _defines(
        _read(repo, rel),
        ["LWIP_VERSION_MAJOR", "LWIP_VERSION_MINOR", "LWIP_VERSION_REVISION"],
        rel,
    )
    return f"{v['LWIP_VERSION_MAJOR']}.{v['LWIP_VERSION_MINOR']}.{v['LWIP_VERSION_REVISION']}"


def _ext_mbedtls(repo):
    rel = "lib/mbedtls/include/mbedtls/build_info.h"
    return _search(
        r'#\s*define\s+MBEDTLS_VERSION_STRING\s+"([^"]+)"', _read(repo, rel), rel
    ).group(1)


def _ext_tinyusb(repo):
    rel = "lib/tinyusb/src/tusb_option.h"
    v = _defines(
        _read(repo, rel),
        ["TUSB_VERSION_MAJOR", "TUSB_VERSION_MINOR", "TUSB_VERSION_REVISION"],
        rel,
    )
    return f"{v['TUSB_VERSION_MAJOR']}.{v['TUSB_VERSION_MINOR']}.{v['TUSB_VERSION_REVISION']}"


def _ext_pico_sdk(repo):
    rel = "lib/pico-sdk/pico_sdk_version.cmake"
    text = _read(repo, rel)
    v = _cmake_set(
        text,
        ["PICO_SDK_VERSION_MAJOR", "PICO_SDK_VERSION_MINOR", "PICO_SDK_VERSION_REVISION"],
        rel,
    )
    ver = f"{v['PICO_SDK_VERSION_MAJOR']}.{v['PICO_SDK_VERSION_MINOR']}.{v['PICO_SDK_VERSION_REVISION']}"
    # Pre-release id is optional; only honour an uncommented set().
    live = re.search(
        r"^\s*set\s*\(\s*PICO_SDK_VERSION_PRE_RELEASE_ID\s+\"?([^\s\")]+)", text, re.MULTILINE
    )
    if live:
        ver += f"-{live.group(1)}"
    return ver


def _ext_libffi(repo):
    rel = "lib/libffi/configure.ac"
    return (
        _search(r"AC_INIT\s*\(\s*\[?[^,\]]+\]?\s*,\s*\[?([^,\]]+)\]?", _read(repo, rel), rel)
        .group(1)
        .strip()
    )


def _ext_protobuf_c(repo):
    # AC_INIT spans multiple lines: AC_INIT([protobuf-c],\n        [1.4.1],\n ...
    rel = "lib/protobuf-c/configure.ac"
    return (
        _search(r"AC_INIT\s*\(\s*\[[^\]]+\]\s*,\s*\[([^\]]+)\]", _read(repo, rel), rel, re.DOTALL)
        .group(1)
        .strip()
    )


def _ext_libmetal(repo):
    rel = "lib/libmetal/VERSION"
    v = _make_assigns(_read(repo, rel), ["VERSION_MAJOR", "VERSION_MINOR", "VERSION_PATCH"], rel)
    return f"{v['VERSION_MAJOR']}.{v['VERSION_MINOR']}.{v['VERSION_PATCH']}"


def _ext_open_amp(repo):
    rel = "lib/open-amp/VERSION"
    v = _make_assigns(_read(repo, rel), ["VERSION_MAJOR", "VERSION_MINOR", "VERSION_PATCH"], rel)
    return f"{v['VERSION_MAJOR']}.{v['VERSION_MINOR']}.{v['VERSION_PATCH']}"


def _ext_libhydrogen(repo):
    # library.properties is the project's canonical Arduino metadata file.
    rel = "lib/libhydrogen/library.properties"
    return _search(r"^version\s*=\s*(\S+)", _read(repo, rel), rel, re.MULTILINE).group(1)


def _ext_cyw43(repo):
    rel = "lib/cyw43-driver/src/cyw43.h"
    v = _defines(
        _read(repo, rel),
        ["CYW43_VERSION_MAJOR", "CYW43_VERSION_MINOR", "CYW43_VERSION_MICRO"],
        rel,
    )
    return f"{v['CYW43_VERSION_MAJOR']}.{v['CYW43_VERSION_MINOR']}.{v['CYW43_VERSION_MICRO']}"


def _ext_fsp(repo):
    rel = "lib/fsp/ra/fsp/inc/fsp_version.h"
    return _search(
        r'#\s*define\s+FSP_VERSION_STRING\s+\(?\s*"([^"]+)"', _read(repo, rel), rel
    ).group(1)


def _ext_nxp_driver(repo):
    rel = "lib/nxp_driver/sdk/version.txt"
    text = _read(repo, rel).strip()
    if not text:
        raise ExtractorError(f"{rel}: file is empty")
    return text


def _ext_alif_dfp(repo):
    # First <release version="X" ...> in the PDSC manifest is the latest.
    rel = "lib/alif_ensemble-cmsis-dfp/AlifSemiconductor.Ensemble.pdsc"
    return _search(r'<release\s+version="([^"]+)"', _read(repo, rel), rel).group(1)


def _ext_berkeley_db(repo):
    # The changelog is newest-first, with entries headed "<prev> -> <new>";
    # the first target is the release this tree corresponds to (1.85).
    rel = "lib/berkeley-db-1.xx/changelog"
    return _search(r"^\s*[\d.]+\s*->\s*([\d.]+)", _read(repo, rel), rel, re.MULTILINE).group(1)


def _ext_nimble(repo):
    # Deliberately not version.yml: that file carries "repo.version: 0.0.0" and
    # documents itself as always 0.0.0 on the master branch.  RELEASE_NOTES.md
    # is newest-first, so its top entry is the most recent tagged release the
    # pinned tree contains.  Note the pin may sit on development commits after
    # that release, so treat this as a lower bound rather than an exact pin.
    rel = "lib/mynewt-nimble/RELEASE_NOTES.md"
    return _search(r"Apache NimBLE\s+v([\d.]+)", _read(repo, rel), rel).group(1)


# --- vendored (non-submodule) extractors ------------------------------------
#
# Versions for vendored libraries that embed their version in source. Same
# error semantics as the submodule extractors above (raise ExtractorError on
# missing file/field).


def _vext_littlefs(rel, define):
    """Decode littlefs's 32-bit packed-int version macro."""

    def _inner(repo):
        m = _search(rf"#\s*define\s+{define}\s+0x([0-9a-fA-F]+)", _read(repo, rel), rel)
        n = int(m.group(1), 16)
        return f"{(n >> 16) & 0xFFFF}.{n & 0xFFFF}"

    return _inner


def _vext_oofatfs(repo):
    rel = "lib/oofatfs/ff.h"
    text = _read(repo, rel)
    base = _search(
        r"FatFs\s+-\s+Generic\s+FAT\s+Filesystem\s+module\s+(R[\w.]+)", text, rel
    ).group(1)
    rev = _search(r"#\s*define\s+FF_DEFINED\s+(\d+)", text, rel).group(1)
    return f"{base}-rev{rev}"


def _vext_libm_dbl(repo):
    # "...copied from the musl library,\nv1.1.16, and, unless otherwise..."
    rel = "lib/libm_dbl/README"
    return _search(r"musl library,\s*v([\d.]+)", _read(repo, rel), rel).group(1)


# Each entry describes a vendored third-party library (not a git submodule).
# ``extractor`` is optional — when absent, the component is emitted with
# version "unknown" and ``version:source=vendored-unknown``, which keeps the
# coverage gap visible to auditors instead of silently dropping the library.
VENDORED = [
    {
        "path": "lib/littlefs",
        "name": "littlefs-lfs1",
        "upstream": "https://github.com/littlefs-project/littlefs",
        "extractor": _vext_littlefs("lib/littlefs/lfs1.h", "LFS1_VERSION"),
    },
    {
        "path": "lib/littlefs",
        "name": "littlefs-lfs2",
        "upstream": "https://github.com/littlefs-project/littlefs",
        "extractor": _vext_littlefs("lib/littlefs/lfs2.h", "LFS2_VERSION"),
    },
    # micropython/oofatfs is the MP fork; upstream is ChaN's FatFs (no VCS).
    {
        "path": "lib/oofatfs",
        "name": "oofatfs",
        "upstream": "https://github.com/micropython/oofatfs",
        "extractor": _vext_oofatfs,
    },
    # re1.5 is Paul Sokolovsky's fork of Russ Cox's re1; version encoded in name.
    {
        "path": "lib/re1.5",
        "name": "re1.5",
        "upstream": "https://github.com/pfalcon/re1.5",
        "extractor": lambda _r: "1.5",
    },
    # The vendored copy is a subset of upstream's files, further modified for
    # MicroPython, and carries no version marker — pinning an upstream release
    # here would misrepresent what is actually compiled.
    {"path": "lib/uzlib", "name": "uzlib", "upstream": "https://github.com/pfalcon/uzlib"},
    # Public domain, upstream publishes no releases or tags to key off.
    {
        "path": "lib/crypto-algorithms",
        "name": "crypto-algorithms",
        "upstream": "https://github.com/B-Con/crypto-algorithms",
    },
    # libm is adapted from newlib-nano-2 (see the header comment in
    # lib/libm/fdlibm.h), itself derived from SunPro's fdlibm — not from musl.
    # No version is declared in-tree, so this stays a documented gap.
    {
        "path": "lib/libm",
        "name": "libm",
        "upstream": "https://github.com/32bitmicro/newlib-nano-2",
    },
    # libm_dbl's README names the exact musl release its files came from.
    {
        "path": "lib/libm_dbl",
        "name": "libm-dbl",
        "upstream": "https://musl.libc.org",
        "extractor": _vext_libm_dbl,
    },
]


def _vendored_purl(upstream, name, version):
    parsed = urlparse(upstream)
    host = parsed.netloc.lower()
    path = parsed.path.lstrip("/")
    if path.endswith(".git"):
        path = path[:-4]
    if host == "github.com" and path:
        return f"pkg:github/{path}@{version}"
    return f"pkg:generic/{name}@{version}?vcs_url={upstream}"


def build_vendored_component(repo, v):
    name = v["name"]
    path = v["path"]
    upstream = v["upstream"]
    extractor = v.get("extractor")
    if extractor:
        # Vendored dirs live inside the parent repo — missing tree is a hard
        # error (the repo is malformed), not a "user hasn't run submodule
        # update" situation.
        if not _is_checked_out(repo / path):
            raise ExtractorError(f"{path}: vendored directory missing")
        version = extractor(repo)
        source = "vendored-in-tree"
    else:
        version = "unknown"
        source = "vendored-unknown"
    return {
        "type": "library",
        "name": name,
        "version": version,
        "purl": _vendored_purl(upstream, name, version),
        "externalReferences": [{"type": "vcs", "url": upstream}],
        "properties": [
            {"name": "vendored:path", "value": path},
            {"name": "version:source", "value": source},
        ],
    }


EXTRACTORS = {
    "lib/lwip": _ext_lwip,
    "lib/mbedtls": _ext_mbedtls,
    "lib/tinyusb": _ext_tinyusb,
    "lib/pico-sdk": _ext_pico_sdk,
    "lib/libffi": _ext_libffi,
    "lib/protobuf-c": _ext_protobuf_c,
    "lib/libmetal": _ext_libmetal,
    "lib/open-amp": _ext_open_amp,
    "lib/libhydrogen": _ext_libhydrogen,
    "lib/cyw43-driver": _ext_cyw43,
    "lib/fsp": _ext_fsp,
    "lib/nxp_driver": _ext_nxp_driver,
    "lib/alif_ensemble-cmsis-dfp": _ext_alif_dfp,
    "lib/berkeley-db-1.xx": _ext_berkeley_db,
    "lib/mynewt-nimble": _ext_nimble,
}

# Submodules that deliberately have no extractor, and why — recorded so the
# gaps don't get re-investigated.  These fall through to the ls-remote/SHA
# layers, which is the honest result rather than a misleading number.
#
# lib/axtls        config/Rules.mak carries MAJOR/MINOR/SUBLEVEL_VERSION, but
#                  that file is BusyBox's build system (it still sets
#                  "PROG := busybox"); the 1.1.0 there is BusyBox's, not
#                  axTLS's.  No axTLS version is declared in-tree.
# lib/stm32lib     A collection of ST HAL drivers for 14 MCU families, each
#                  independently versioned; only F4 (V1.7.1) and F7 (V1.2.2)
#                  declare a version at all, so no single value is meaningful.
# lib/asf4         Same shape: per-MCU component-version.h files disagree
#                  (samd21 1.2, same51 1.1, samd51/same54 1.0).
# lib/wiznet5k     ioLibrary_Driver ships no version marker; the VERSION
#                  define in Internet/FTPServer/ftpd.h is the FTP server's.
# lib/arduino-lib  No version file; README is licence text only.
# lib/alif-security-toolkit
#                  Releases are tagged (v1.104.0, v1.110.0, v1.112.0) but only
#                  on the separate "vendor" branch, and the pinned commit is
#                  not a descendant of any of them, so neither an exact-tag
#                  match nor "git describe" resolves it.
#
# Resolving stm32lib and asf4 properly would mean emitting per-family
# sub-components rather than one component per submodule.


# CPE 2.3 templates for vulnerability scanners that match against NVD/CPE
# (e.g., Grype). These map a submodule path to the most common
# vendor:product string seen in NVD's dictionary for that project; the
# {version} placeholder is filled with the resolved version. CPEs are only
# emitted when the version came from an in-tree extractor or a tag — a raw
# commit SHA cannot be matched against a CPE version string.
#
# Note: CPE strings are inherently fuzzy. NVD frequently lists multiple CPEs
# for the same project (e.g. mbedtls under both `arm:mbed_tls` and
# `trustedfirmware:mbed_tls`), and the matches a scanner produces against
# these should be sanity-checked before being treated as confirmed.
CPE_OVERRIDES = {
    "lib/mbedtls": "cpe:2.3:a:arm:mbed_tls:{version}:*:*:*:*:*:*:*",
    "lib/lwip": "cpe:2.3:a:lwip_project:lwip:{version}:*:*:*:*:*:*:*",
    "lib/libffi": "cpe:2.3:a:libffi_project:libffi:{version}:*:*:*:*:*:*:*",
    "lib/protobuf-c": "cpe:2.3:a:protobuf-c_project:protobuf-c:{version}:*:*:*:*:*:*:*",
    "lib/tinyusb": "cpe:2.3:a:tinyusb_project:tinyusb:{version}:*:*:*:*:*:*:*",
}

# Override the .gitmodules-derived purl when scanners (notably OSV.dev)
# index the project under a different upstream URL — e.g., relocated GitHub
# orgs. The {sha} placeholder is filled with the pinned commit SHA.
PURL_OVERRIDES = {
    # github.com/ARMmbed/mbedtls is archived; the project lives at Mbed-TLS.
    "lib/mbedtls": "pkg:github/Mbed-TLS/mbedtls@{sha}",
}


def _cpe_version(v):
    """Strip a leading ``v`` from a tag-style version (NVD CPEs are bare semver)."""
    return v[1:] if len(v) > 1 and v[0] == "v" and v[1].isdigit() else v


def _is_checked_out(submod_dir):
    """A submodule directory is considered checked-out when it has any contents.

    git creates the directory but leaves it empty until ``submodule update``
    populates it.
    """
    try:
        return submod_dir.is_dir() and any(submod_dir.iterdir())
    except OSError:
        return False


def resolve_version(repo, submod, sha, *, offline):
    """Return (version_string, source_label) using the layered strategy.

    Raises ExtractorError if a registered extractor's expected file/field is
    missing — that is a real bug in the registry, not a fallback case. A
    submodule that simply isn't checked out yet emits a warning to stderr and
    falls through to ls-remote/SHA.
    """
    extractor = EXTRACTORS.get(submod["path"])
    if extractor:
        if _is_checked_out(repo / submod["path"]):
            return extractor(repo), "in-tree"
        print(
            f"warning: {submod['path']} is not checked out; "
            f"in-tree extractor skipped (run `git submodule update --init` "
            f"for a more precise version)",
            file=sys.stderr,
        )
    if not offline:
        tag = ls_remote_tags(submod["url"]).get(sha)
        if tag:
            return tag, "git-tag"
    return sha[:12], "git-sha"


def build_component(submod, sha, version, source):
    name = submod["path"].split("/")[-1]
    properties = [
        {"name": "submodule:path", "value": submod["path"]},
        {"name": "git:commit", "value": sha},
        {"name": "version:source", "value": source},
    ]
    if submod.get("branch"):
        properties.append({"name": "git:branch", "value": submod["branch"]})

    purl_template = PURL_OVERRIDES.get(submod["path"])
    purl = purl_template.format(sha=sha) if purl_template else url_to_purl(submod["url"], sha)

    component = {
        "type": "library",
        "name": name,
        "version": version,
        "purl": purl,
        "externalReferences": [{"type": "vcs", "url": submod["url"]}],
        "properties": properties,
    }

    cpe_template = CPE_OVERRIDES.get(submod["path"])
    if cpe_template and source in ("in-tree", "git-tag"):
        component["cpe"] = cpe_template.format(version=_cpe_version(version))

    return component


def repo_version(repo):
    try:
        return run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=str(repo),
        ).strip()
    except subprocess.CalledProcessError:
        return "unknown"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--repo", default=".", help="Path to the MicroPython repository (default: cwd)."
    )
    parser.add_argument(
        "--offline", action="store_true", help="Skip git ls-remote; report commit SHAs as version."
    )
    parser.add_argument("-o", "--output", default="-", help="Output file (default: stdout).")
    parser.add_argument(
        "--text", action="store_true", help="Emit `name=version` lines instead of CycloneDX JSON."
    )
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    submods = sorted(parse_gitmodules(repo), key=lambda s: s["path"])

    components = []
    extractor_failures = []
    for s in submods:
        sha = submodule_sha(repo, s["path"])
        if not sha:
            print(f"warning: no gitlink for {s['path']}", file=sys.stderr)
            continue
        print(f"resolving {s['path']}...", file=sys.stderr)
        try:
            version, source = resolve_version(repo, s, sha, offline=args.offline)
        except ExtractorError as e:
            extractor_failures.append((s["path"], str(e)))
            continue
        components.append(build_component(s, sha, version, source))

    for v in VENDORED:
        print(f"resolving {v['path']} (vendored: {v['name']})...", file=sys.stderr)
        try:
            components.append(build_vendored_component(repo, v))
        except ExtractorError as e:
            extractor_failures.append((v["path"], str(e)))

    components.sort(key=lambda c: c["name"])

    if extractor_failures:
        print(
            "\nerror: in-tree extractor(s) failed — the version source for these", file=sys.stderr
        )
        print("       libraries has likely changed upstream and EXTRACTORS needs", file=sys.stderr)
        print("       updating in tools/sbom.py:", file=sys.stderr)
        for path, msg in extractor_failures:
            print(f"  {path}: {msg}", file=sys.stderr)
        sys.exit(1)

    if args.text:
        text = "".join(f"{c['name']}={c['version']}\n" for c in components)
    else:
        bom = {
            "bomFormat": "CycloneDX",
            "specVersion": "1.5",
            "serialNumber": f"urn:uuid:{uuid.uuid4()}",
            "version": 1,
            "metadata": {
                "timestamp": datetime.datetime.now(datetime.timezone.utc)
                .replace(microsecond=0)
                .isoformat(),
                "tools": [{"vendor": "MicroPython", "name": "tools/sbom.py"}],
                "component": {
                    "type": "application",
                    "name": "micropython",
                    "version": repo_version(repo),
                },
            },
            "components": components,
        }
        text = json.dumps(bom, indent=2) + "\n"

    if args.output == "-":
        sys.stdout.write(text)
    else:
        Path(args.output).write_text(text)


if __name__ == "__main__":
    main()
