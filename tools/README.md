# tools/

Notes on the helper scripts in this directory. See each script's module
docstring for full usage; this README only covers the bits that are easier
to learn from a paste than from reading code.

## sbom.py — readable views of the SBOM

`sbom.py` emits a CycloneDX 1.5 JSON SBOM. The `--text` flag produces a
flat `name=version` list, which is fine for sanity checks but loses the
purl, vcs URL, and version-source provenance. For richer post-processing,
pipe the JSON through `jq` + `column`.

Prerequisite (if you don't have `jq`):

```bash
curl -sSfL -o ~/.local/bin/jq \
  https://github.com/jqlang/jq/releases/latest/download/jq-linux-amd64
chmod +x ~/.local/bin/jq
```

### Full table — name, version, source, VCS URL

```bash
jq -r '.components[] |
  [.name, .version,
   (.properties[]|select(.name=="version:source")|.value),
   .externalReferences[0].url] | @tsv' sbom.json \
  | column -t -s $'\t'
```

### Gaps only — components scanners cannot usefully check

Filters to entries resolved by SHA (no upstream tag/in-tree version) or
recorded as `vendored-unknown`.

```bash
jq -r '.components[] |
  select((.properties[]|select(.name=="version:source")|.value)
         | test("sha|unknown"))
  | [.name, .version] | @tsv' sbom.json \
  | column -t -s $'\t'
```

### Summary — component count by resolution source

```bash
jq -r '[.components[]
        | (.properties[]|select(.name=="version:source")|.value)]
       | group_by(.) | map("\(.[0])\t\(length)")[]' sbom.json \
  | column -t -s $'\t'
```
