# test syntax restrictions on "lazy import"/"lazy from ... import ..."
# (MICROPY_MODULE_LAZY_IMPORT): module-scope only, no "lazy from x import *".
# Uses exec() on string sources so the invalid syntax doesn't stop this file
# itself from parsing.


def check_syntax_error(src):
    try:
        exec(src)
        print("no SyntaxError")
    except SyntaxError:
        print("SyntaxError")


check_syntax_error("def f():\n    lazy import sys\n")
check_syntax_error("class C:\n    lazy import sys\n")
check_syntax_error("try:\n    lazy import sys\nexcept ImportError:\n    pass\n")
check_syntax_error("lazy from sys import *\n")

# 'lazy' is a reserved word when this feature is compiled in
check_syntax_error("lazy = 1\n")

# sanity: plain module-scope lazy import via exec() is fine
exec("lazy import sys\nprint(sys.byteorder in ('little', 'big'))\n")
