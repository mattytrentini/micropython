# test "lazy from module import name [as name]" deferral, including sibling
# names sharing one deferred module import (MICROPY_MODULE_LAZY_IMPORT)
#
# The "lazy import"/"lazy from" statements below are wrapped in exec() so
# this file stays parseable by tools (eg ruff) that don't understand the
# "lazy" keyword - it's not real Python syntax to them.

import sys

assert "lazy_import_pkg" not in sys.modules

exec("lazy from lazy_import_pkg import sub, __name__ as pkgname")

# neither the package nor its submodule should be imported yet
print("lazy_import_pkg" in sys.modules)
print("lazy_import_pkg.sub" in sys.modules)

# using the first sibling name loads the package and resolves 'sub'
print(sub.LEAF_VALUE)
print("lazy_import_pkg" in sys.modules)

# the second sibling ('pkgname') was not eagerly resolved by the above -
# using it now just needs the already-cached package, no re-import
print(pkgname)

# aliased from-import
exec("lazy from lazy_import_pkg import sub as sub2")
print(sub2.LEAF_VALUE)

# dotted import, no alias: binds the top-level package
exec("lazy import lazy_import_pkg.sub")
print(lazy_import_pkg.sub.LEAF_VALUE)

# dotted import, aliased: binds the leaf module (attribute walk deferred to
# reification - the one CPython PEP 810 proxy model doesn't need to handle)
exec("lazy import lazy_import_pkg.sub as leaf")
print(leaf.LEAF_VALUE)
