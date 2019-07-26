// Include required definitions first.
#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"

#include "qrcodegen.h"

// The maximum row/column is 177 when version 40 QR code are employed
#define MAX_QR_SIZE (177)

STATIC mp_obj_t encode_text(
        mp_obj_t text_obj) {
    const char* text = mp_obj_str_get_str(text_obj);

    // Generate QR code
    uint8_t qr0[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
    bool ok = qrcodegen_encodeText(text,
        tempBuffer, qr0, qrcodegen_Ecc_MEDIUM,
        qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO, true);
    if (!ok) {
        mp_raise_ValueError("Error generating QR code"); // Not quite the right Exception...
    }

    int size = qrcodegen_getSize(qr0);
    assert(size <= MAX_QR_SIZE);
    mp_obj_tuple_t *row = MP_OBJ_TO_PTR(mp_obj_new_tuple(size, NULL));
    for (int y = 0; y < size; y++) {
        mp_obj_tuple_t *column = MP_OBJ_TO_PTR(mp_obj_new_tuple(size, NULL));
        for (int x = 0; x < size; x++) {
            column->items[x] = mp_obj_new_bool(qrcodegen_getModule(qr0, x, y));
        }
        row->items[y] = MP_OBJ_FROM_PTR(column);
    }
    return MP_OBJ_FROM_PTR(row);
}
MP_DEFINE_CONST_FUN_OBJ_1(encode_text_obj, encode_text);


// Define the contents of the module
STATIC const mp_rom_map_elem_t qr_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_qr) },
    { MP_ROM_QSTR(MP_QSTR_encode_text), MP_ROM_PTR(&encode_text_obj) }, // Oli: Maybe prefix module name ahead of encode_text_obj?
    { MP_ROM_QSTR(MP_QSTR_ECC_LOW), MP_ROM_INT(qrcodegen_Ecc_LOW) }, // Any way to make these some sort of Enum
    { MP_ROM_QSTR(MP_QSTR_ECC_MEDIUM), MP_ROM_INT(qrcodegen_Ecc_MEDIUM) },
    { MP_ROM_QSTR(MP_QSTR_ECC_QUARTILE), MP_ROM_INT(qrcodegen_Ecc_QUARTILE) },
    { MP_ROM_QSTR(MP_QSTR_ECC_HIGH), MP_ROM_INT(qrcodegen_Ecc_HIGH) },
};
STATIC MP_DEFINE_CONST_DICT(qr_module_globals, qr_module_globals_table);

// Define module object.
const mp_obj_module_t qr_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&qr_module_globals,
};

// Register the module to make it available in Python
MP_REGISTER_MODULE(MP_QSTR_qr, qr_cmodule, MODULE_QR_ENABLED);