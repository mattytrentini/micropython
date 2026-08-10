# test that a failed lazy import raises at first-use (not at the "lazy"
# statement itself), and that a failed reification can be retried
# (MICROPY_MODULE_LAZY_IMPORT)
#
# The "lazy import"/"lazy from" statements below are wrapped in exec() so
# this file stays parseable by tools (eg ruff) that don't understand the
# "lazy" keyword - it's not real Python syntax to them.

print("before lazy import")
exec("lazy import lazy_import_does_not_exist")
print("lazy import statement did not raise")

for i in range(2):
    try:
        lazy_import_does_not_exist.foo
        print("no error")
    except ImportError:
        print("attempt", i, "raised ImportError")

exec("lazy from lazy_import_pkg import does_not_exist_either")
try:
    does_not_exist_either.foo
    print("no error")
except ImportError:
    print("from-import attribute lookup raised ImportError")
