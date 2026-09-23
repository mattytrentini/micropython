#!/usr/bin/env python3
"""Render a human-readable GitHub Actions summary from SBOM and Grype JSON.

Usage:
    python3 tools/sbom_report.py --sbom sbom.cdx.json --grype grype.json \
        --output report.md
"""

import argparse
import json
from pathlib import Path


SEVERITY_ORDER = ("Critical", "High", "Medium", "Low", "Negligible", "Unknown")
SEVERITY_ICON = {
    "Critical": "🟣",
    "High": "🔴",
    "Medium": "🟠",
    "Low": "🟡",
    "Negligible": "⚪",
    "Unknown": "⚫",
}


def markdown_escape(value):
    """Make a string safe for use inside a Markdown table cell."""
    return str(value).replace("|", r"\|").replace("\n", " ")


def severity(value):
    value = (value or "Unknown").capitalize()
    return value if value in SEVERITY_ICON else "Unknown"


def components(component_list):
    """Return all library components, including nested MCU-family components."""
    found = []
    for component in component_list:
        if component.get("type") == "library":
            found.append(component)
        found.extend(components(component.get("components", [])))
    return found


def advisory_url(vulnerability_id):
    if vulnerability_id.startswith("CVE-"):
        return f"https://nvd.nist.gov/vuln/detail/{vulnerability_id}"
    if vulnerability_id.startswith("GHSA-"):
        return f"https://github.com/advisories/{vulnerability_id}"
    return None


def format_finding(match):
    vulnerability = match["vulnerability"]
    vulnerability_id = vulnerability.get("id", "Unknown advisory")
    url = advisory_url(vulnerability_id)
    label = f"[{vulnerability_id}]({url})" if url else vulnerability_id
    finding_severity = severity(vulnerability.get("severity"))
    return f"{SEVERITY_ICON[finding_severity]} {label} — {finding_severity}"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sbom", required=True, type=Path)
    parser.add_argument("--grype", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    sbom = json.loads(args.sbom.read_text())
    grype = json.loads(args.grype.read_text())
    matches = grype.get("matches", [])

    findings_by_purl = {}
    findings_by_name_version = {}
    for match in matches:
        artifact = match.get("artifact", {})
        if artifact.get("purl"):
            findings_by_purl.setdefault(artifact["purl"], []).append(match)
        key = (artifact.get("name"), artifact.get("version"))
        findings_by_name_version.setdefault(key, []).append(match)

    counts = {level: 0 for level in SEVERITY_ORDER}
    for match in matches:
        counts[severity(match.get("vulnerability", {}).get("severity"))] += 1

    library_components = components(sbom.get("components", []))
    library_components.sort(
        key=lambda component: (component.get("name", "").lower(), component.get("version", ""))
    )

    metadata = sbom.get("metadata", {})
    source = metadata.get("component", {})
    lines = [
        "# SBOM vulnerability report",
        "",
        f"**Component:** `{markdown_escape(source.get('name', 'unknown'))}` "
        f"`{markdown_escape(source.get('version', 'unknown'))}`  ",
        f"**Generated:** {markdown_escape(metadata.get('timestamp', 'unknown'))}",
        "",
        "## Overview",
        "",
        "| Libraries | Findings | Critical | High | Medium | Low |",
        "|---:|---:|---:|---:|---:|---:|",
        f"| {len(library_components)} | {len(matches)} | {counts['Critical']} | "
        f"{counts['High']} | {counts['Medium']} | {counts['Low']} |",
        "",
        "This is an automated match report. Review each finding for applicability "
        "to the pinned source and MicroPython build configuration.",
        "",
        "## Libraries",
        "",
        "| Library | Version | Vulnerabilities |",
        "|---|---|---|",
    ]

    for component in library_components:
        component_matches = findings_by_purl.get(component.get("purl"))
        if component_matches is None:
            component_matches = findings_by_name_version.get(
                (component.get("name"), component.get("version")), []
            )
        # A component can match by both purl and name/version; report each CVE once.
        unique_matches = {m.get("vulnerability", {}).get("id"): m for m in component_matches}
        findings = "<br>".join(format_finding(match) for match in unique_matches.values())
        if not findings:
            findings = "✅ None found"
        lines.append(
            f"| {markdown_escape(component.get('name', 'unknown'))} | "
            f"{markdown_escape(component.get('version', 'unknown'))} | {findings} |"
        )

    args.output.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
