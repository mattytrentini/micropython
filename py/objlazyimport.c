/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Matt Trentini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// PEP 810-subset lazy imports: "lazy import module [as name]" and
// "lazy from module import name [as name]" (see MICROPY_MODULE_LAZY_IMPORT).
//
// Two internal object types are used, both created only from the compiler's
// lazy-import codegen (py/compile.c) and never exposed as a Python-callable
// type:
//
//  - mp_type_lazy_import: the proxy bound into a module's globals dict in
//    place of the real value. mp_load_global() (py/runtime.c) checks for
//    this type on every global lookup (when this feature is compiled in)
//    and reifies it via mp_lazy_import_reify() on first access.
//
//  - mp_type_lazy_import_pending: shared state for the sibling proxies of a
//    single "lazy from X import a, b" statement. It is never itself bound
//    to a name (and so is never seen by mp_load_global()'s check) - it only
//    ever lives transiently on the bytecode stack between
//    MP_BC_IMPORT_FROM_LAZY_START and the final POP_TOP, and permanently
//    referenced from each sibling mp_obj_lazy_import_t's pending field.

#include <string.h>

#include "py/runtime.h"

#if MICROPY_MODULE_LAZY_IMPORT

typedef struct _mp_obj_lazy_import_pending_t {
    mp_obj_base_t base;
    qstr module_name;
    mp_obj_t level;
    mp_obj_t resolved_module; // MP_OBJ_NULL until first reification of any sibling name
} mp_obj_lazy_import_pending_t;

typedef struct _mp_obj_lazy_import_t {
    mp_obj_base_t base;
    qstr name; // whole-module mode: full dotted module name; name mode: the attribute to resolve
    mp_obj_t level_or_pending; // whole-module mode: level (small int); name mode: mp_obj_lazy_import_pending_t*
    bool is_name_mode;
    bool dotted_alias_walk; // whole-module mode only, eg "lazy import a.b.c as y"
} mp_obj_lazy_import_t;

static void lazy_import_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_lazy_import_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<lazy import '%q'>", self->name);
}

static void lazy_import_pending_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_lazy_import_pending_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<lazy import pending '%q'>", self->module_name);
}

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_lazy_import, MP_QSTR_, MP_TYPE_FLAG_NONE,
    print, lazy_import_print
    );

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_lazy_import_pending, MP_QSTR_, MP_TYPE_FLAG_NONE,
    print, lazy_import_pending_print
    );

mp_obj_t mp_import_name_lazy(qstr name, mp_obj_t level, mp_obj_t walk_flag) {
    mp_obj_lazy_import_t *o = mp_obj_malloc(mp_obj_lazy_import_t, &mp_type_lazy_import);
    o->name = name;
    o->level_or_pending = level;
    o->is_name_mode = false;
    o->dotted_alias_walk = mp_obj_is_true(walk_flag);
    return MP_OBJ_FROM_PTR(o);
}

mp_obj_t mp_import_from_lazy_start(qstr module_name, mp_obj_t level) {
    mp_obj_lazy_import_pending_t *o = mp_obj_malloc(mp_obj_lazy_import_pending_t, &mp_type_lazy_import_pending);
    o->module_name = module_name;
    o->level = level;
    o->resolved_module = MP_OBJ_NULL;
    return MP_OBJ_FROM_PTR(o);
}

mp_obj_t mp_import_from_lazy(mp_obj_t pending, qstr name) {
    mp_obj_lazy_import_t *o = mp_obj_malloc(mp_obj_lazy_import_t, &mp_type_lazy_import);
    o->name = name;
    o->level_or_pending = pending;
    o->is_name_mode = true;
    o->dotted_alias_walk = false;
    return MP_OBJ_FROM_PTR(o);
}

// Walks attribute access for each '.'-separated component of dotted_name
// after the first (eg for "a.b.c", walks .b then .c off obj), for
// "lazy import a.b.c as y" where the leaf module - not the top-level
// package mp_import_name() returns - is what must end up bound to y.
static mp_obj_t lazy_import_walk_dotted_attrs(qstr dotted_name, mp_obj_t obj) {
    size_t len;
    const char *str = (const char *)qstr_data(dotted_name, &len);
    const char *end = str + len;
    const char *p = str;
    while (p < end && *p != '.') {
        ++p;
    }
    while (p < end) {
        ++p; // skip the '.'
        const char *seg_start = p;
        while (p < end && *p != '.') {
            ++p;
        }
        obj = mp_load_attr(obj, qstr_from_strn(seg_start, p - seg_start));
    }
    return obj;
}

mp_obj_t mp_lazy_import_reify(mp_obj_t self_in) {
    mp_obj_lazy_import_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->is_name_mode) {
        mp_obj_lazy_import_pending_t *pending = MP_OBJ_TO_PTR(self->level_or_pending);
        if (pending->resolved_module == MP_OBJ_NULL) {
            // Sentinel "non empty" fromlist value to force returning of the
            // leaf module for a dotted source, matching mp_import_from()'s
            // own package-submodule fallback (py/runtime.c). The specific
            // names being imported don't need to be threaded through here:
            // mp_import_from() below already handles resolving each one,
            // including its own submodule-import fallback for packages.
            mp_obj_t resolved = mp_import_name(pending->module_name, mp_const_true, pending->level);
            pending->resolved_module = resolved;
        }
        return mp_import_from(pending->resolved_module, self->name);
    } else {
        mp_obj_t result = mp_import_name(self->name, mp_const_none, self->level_or_pending);
        if (self->dotted_alias_walk) {
            result = lazy_import_walk_dotted_attrs(self->name, result);
        }
        return result;
    }
}

#endif // MICROPY_MODULE_LAZY_IMPORT
