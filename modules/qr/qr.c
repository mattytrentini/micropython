// Include required definitions first.
#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"

#include "string.h"
#include "qrcodegen.h"

STATIC mp_obj_t my_function(mp_obj_t my_input_arg) {
    mp_obj_t *my_input = NULL;
    size_t my_input_len = 0;

    mp_obj_get_array(my_input_arg, &my_input_len, &my_input);
    if (my_input_len != 3) {
        // Expected 3 arguments, raise exception
        mp_raise_ValueError("my_input must be of length 3!");
    }

    mp_obj_t ret_val[] = {
        my_input[0],
        my_input[1],
        my_input[2] };

    // Function code here
    return mp_obj_new_list(3, ret_val);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(my_function_obj, my_function);

// Generate QR code
uint8_t qr0[qrcodegen_BUFFER_LEN_MAX];
uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];

STATIC mp_obj_t encode_text2(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum { ARG_text, ARG_ecc };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_text, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_ecc,                    MP_ARG_INT, {.u_int = qrcodegen_Ecc_MEDIUM} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    bool ok = qrcodegen_encodeText(mp_obj_str_get_str(args[ARG_text].u_obj),
        tempBuffer, qr0, args[ARG_ecc].u_int,
        qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO, true);
    if (!ok) {
        mp_raise_ValueError("Error generating QR code");
    }

    int size = qrcodegen_getSize(qr0);
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
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(encode_text2_obj, 0, encode_text2);

// STATIC mp_obj_t encode_text(
//         mp_obj_t text_obj) {
//     const char* text = mp_obj_str_get_str(text_obj);

//     return encode_text2(1, mp_obj_new_str(text));
// }
// MP_DEFINE_CONST_FUN_OBJ_1(encode_text_obj, encode_text);

typedef struct _mp_obj_qr_t {
    mp_obj_base_t base;
    uint8_t qr0[qrcodegen_BUFFER_LEN_MAX];
} mp_obj_qr_t;

STATIC const mp_obj_type_t qrcode_type;

STATIC mp_obj_t qrcode_encode_text(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum { ARG_text, ARG_ecc };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_text, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_ecc,                    MP_ARG_INT, {.u_int = qrcodegen_Ecc_MEDIUM} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Generate QR code
    mp_obj_qr_t *qr = m_new_obj(mp_obj_qr_t);
    qr->base.type = &qrcode_type;

    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
    mp_uint_t len;
    bool ok = qrcodegen_encodeText(mp_obj_str_get_data(args[ARG_text].u_obj, &len),
        tempBuffer, qr->qr0, args[ARG_ecc].u_int,
        qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO, true);
    if (!ok) {
        mp_raise_ValueError("Error generating QR code"); // Not quite the right Exception...
    }
    return MP_OBJ_FROM_PTR(qr);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(qrcode_encode_text_fun_obj, 0, qrcode_encode_text);
STATIC MP_DEFINE_CONST_STATICMETHOD_OBJ(qrcode_encode_text_obj, MP_ROM_PTR(&qrcode_encode_text_fun_obj));

STATIC mp_obj_t qrcode_get_size(mp_obj_t self_in) {
    mp_obj_qr_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(qrcodegen_getSize(self->qr0));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(qrcode_get_size_obj, qrcode_get_size);

STATIC mp_obj_t qrcode_get_module(mp_obj_t self_in, mp_obj_t x, mp_obj_t y) {
    mp_obj_qr_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(qrcodegen_getModule(self->qr0, mp_obj_get_int(x), mp_obj_get_int(y)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(qrcode_get_module_obj, qrcode_get_module);

STATIC const mp_rom_map_elem_t qrcode_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_encode_text), MP_ROM_PTR(&qrcode_encode_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_size), MP_ROM_PTR(&qrcode_get_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_module), MP_ROM_PTR(&qrcode_get_module_obj) },
};
STATIC MP_DEFINE_CONST_DICT(qrcode_locals_dict, qrcode_locals_dict_table);

STATIC const mp_obj_type_t qrcode_type = {
    { &mp_type_type },
    .name = MP_QSTR_QrCode,
    .locals_dict = (void*)&qrcode_locals_dict,
};





// Define the contents of the module
STATIC const mp_rom_map_elem_t qr_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_qr) },
    { MP_ROM_QSTR(MP_QSTR_QrCode), MP_ROM_PTR(&qrcode_type) },
    //{ MP_ROM_QSTR(MP_QSTR_encode_text), MP_ROM_PTR(&encode_text_obj) }, // Oli: Maybe prefix module name ahead of encode_text_obj?
    { MP_ROM_QSTR(MP_QSTR_encode_text2), MP_ROM_PTR(&encode_text2_obj) },
    { MP_ROM_QSTR(MP_QSTR_ECC_LOW), MP_ROM_INT(qrcodegen_Ecc_LOW) }, // Any way to make these some sort of Enum
    { MP_ROM_QSTR(MP_QSTR_ECC_MEDIUM), MP_ROM_INT(qrcodegen_Ecc_MEDIUM) },
    { MP_ROM_QSTR(MP_QSTR_ECC_QUARTILE), MP_ROM_INT(qrcodegen_Ecc_QUARTILE) },
    { MP_ROM_QSTR(MP_QSTR_ECC_HIGH), MP_ROM_INT(qrcodegen_Ecc_HIGH) },
    { MP_ROM_QSTR(MP_QSTR_my_function), MP_ROM_PTR(&my_function_obj) },

};
STATIC MP_DEFINE_CONST_DICT(qr_module_globals, qr_module_globals_table);

// Define module object.
const mp_obj_module_t qr_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&qr_module_globals,
};

// Register the module to make it available in Python
MP_REGISTER_MODULE(MP_QSTR_qr, qr_cmodule, MODULE_QR_ENABLED);