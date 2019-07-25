// Include required definitions first.
#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"

#include <string.h>

#include "qrcodegen.h"

// This is the function which will be called from Python as example.add_ints(a, b).
STATIC mp_obj_t qr_add_ints(mp_obj_t a_obj, mp_obj_t b_obj) {
    // Extract the ints from the micropython input objects
    int a = mp_obj_get_int(a_obj);
    int b = mp_obj_get_int(b_obj);

    // Calculate the addition and convert to MicroPython object.
    return mp_obj_new_int(a + b);
}
// Define a Python reference to the function above
STATIC MP_DEFINE_CONST_FUN_OBJ_2(qr_add_ints_obj, qr_add_ints);


STATIC mp_obj_t encode_text(
        mp_obj_t text_obj) {
    const char* text = mp_obj_str_get_str(text_obj);

    // Text data
    uint8_t qr0[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
    bool ok = qrcodegen_encodeText(text,
        tempBuffer, qr0, qrcodegen_Ecc_MEDIUM,
        qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO, true);
    if (!ok) {
        // Raise exception
    }

    mp_obj_t row[30];

    int size = qrcodegen_getSize(qr0);
    //for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            row[x] = mp_obj_new_bool(qrcodegen_getModule(qr0, x, 0));
        }
    //}
    return mp_obj_new_tuple(size, row);
}
MP_DEFINE_CONST_FUN_OBJ_1(encode_text_obj, encode_text);


// Define all properties of the example module.
// Table entries are key/value pairs of the attribute name (a string)
// and the MicroPython object reference.
// All identifiers and strings are written as MP_QSTR_xxx and will be
// optimized to word-sized integers by the build system (interned strings).
STATIC const mp_rom_map_elem_t qr_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_qr) },
    { MP_ROM_QSTR(MP_QSTR_add_ints), MP_ROM_PTR(&qr_add_ints_obj) },
    { MP_ROM_QSTR(MP_QSTR_encode_text), MP_ROM_PTR(&encode_text_obj) }, // Oli: Maybe prefix module name ahead of encode_text_obj?
};
STATIC MP_DEFINE_CONST_DICT(qr_module_globals, qr_module_globals_table);

// Define module object.
const mp_obj_module_t qr_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&qr_module_globals,
};

// Register the module to make it available in Python
MP_REGISTER_MODULE(MP_QSTR_qr, qr_cmodule, MODULE_QR_ENABLED);