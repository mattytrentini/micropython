# test basic "lazy import module [as name]" deferral (MICROPY_MODULE_LAZY_IMPORT)
#
# The "lazy import" statements below are wrapped in exec() so this file
# stays parseable by tools (eg ruff) that don't understand the "lazy"
# keyword - it's not real Python syntax to them.

import sys

# ensure a clean starting point
assert "lazy_import_basic_mod" not in sys.modules

exec("lazy import lazy_import_basic_mod")

# the module must not have been executed or cached yet
print("lazy_import_basic_mod" in sys.modules)

# first use triggers the real import
print(lazy_import_basic_mod.VALUE)

# now it's a normal, fully-imported module
print("lazy_import_basic_mod" in sys.modules)
print(lazy_import_basic_mod.VALUE)

# aliasing
exec("lazy import lazy_import_basic_mod as aliased")
print(aliased.VALUE)

# re-importing (lazily or not) an already-loaded module just returns the
# cached module, and does not re-execute it
lazy_import_basic_mod.load_count += 1
exec("lazy import lazy_import_basic_mod as aliased2")
print(aliased2.load_count)
