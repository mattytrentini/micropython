# check if 'lazy import'/'lazy from ... import ...' keywords are supported
#
# Wrapped in exec() so this file stays parseable by tools (eg ruff) that
# don't understand the "lazy" keyword - it's not real Python syntax to
# them, whether or not MICROPY_MODULE_LAZY_IMPORT is enabled here.
exec("lazy import sys")

print("lazy_import")
