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

#include "py/runtime.h"

#if MICROPY_PY_MACHINE_QUADSPI || MICROPY_PY_MACHINE_OCTOSPI

#include "extmod/modmachine.h"

/******************************************************************************/
// MicroPython bindings for generic machine.QuadSPI and machine.OctoSPI
//
// QuadSPI (4 data lines) and OctoSPI (8 data lines) share this implementation:
// the data-line count is entirely owned by the port's backend, so both
// machine_quadspi_type and machine_octospi_type bind their "protocol" slot to
// an instance of mp_machine_quadspi_p_t and their "locals_dict" slot to the
// mp_machine_quadspi_locals_dict defined here, the same way SPI and SoftSPI
// share mp_machine_spi_locals_dict.
//
// Unlike machine.SPI, the data lines are shared/bidirectional (half-duplex),
// so there's no combined transfer() primitive and no write_readinto(): write()
// and read()/readinto() are separate operations that a port backend and its
// caller must sequence explicitly (along with any CS handling, exactly as
// machine.SPI already requires).

static mp_obj_t machine_quadspi_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    mp_obj_base_t *s = (mp_obj_base_t *)MP_OBJ_TO_PTR(args[0]);
    mp_machine_quadspi_p_t *quadspi_p = (mp_machine_quadspi_p_t *)MP_OBJ_TYPE_GET_SLOT(s->type, protocol);
    quadspi_p->init(s, n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_quadspi_init_obj, 1, machine_quadspi_init);

static mp_obj_t machine_quadspi_deinit(mp_obj_t self) {
    mp_obj_base_t *s = (mp_obj_base_t *)MP_OBJ_TO_PTR(self);
    mp_machine_quadspi_p_t *quadspi_p = (mp_machine_quadspi_p_t *)MP_OBJ_TYPE_GET_SLOT(s->type, protocol);
    if (quadspi_p->deinit != NULL) {
        quadspi_p->deinit(s);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_quadspi_deinit_obj, machine_quadspi_deinit);

static mp_obj_t mp_machine_quadspi_read(mp_obj_t self, mp_obj_t nbytes_in) {
    mp_obj_base_t *s = (mp_obj_base_t *)MP_OBJ_TO_PTR(self);
    mp_machine_quadspi_p_t *quadspi_p = (mp_machine_quadspi_p_t *)MP_OBJ_TYPE_GET_SLOT(s->type, protocol);
    vstr_t vstr;
    vstr_init_len(&vstr, mp_obj_get_int(nbytes_in));
    quadspi_p->read(s, vstr.len, (uint8_t *)vstr.buf);
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_quadspi_read_obj, mp_machine_quadspi_read);

static mp_obj_t mp_machine_quadspi_readinto(mp_obj_t self, mp_obj_t buf_in) {
    mp_obj_base_t *s = (mp_obj_base_t *)MP_OBJ_TO_PTR(self);
    mp_machine_quadspi_p_t *quadspi_p = (mp_machine_quadspi_p_t *)MP_OBJ_TYPE_GET_SLOT(s->type, protocol);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    quadspi_p->read(s, bufinfo.len, bufinfo.buf);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_quadspi_readinto_obj, mp_machine_quadspi_readinto);

static mp_obj_t mp_machine_quadspi_write(mp_obj_t self, mp_obj_t wr_buf) {
    mp_obj_base_t *s = (mp_obj_base_t *)MP_OBJ_TO_PTR(self);
    mp_machine_quadspi_p_t *quadspi_p = (mp_machine_quadspi_p_t *)MP_OBJ_TYPE_GET_SLOT(s->type, protocol);
    mp_buffer_info_t src;
    mp_get_buffer_raise(wr_buf, &src, MP_BUFFER_READ);
    quadspi_p->write(s, src.len, (const uint8_t *)src.buf);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_quadspi_write_obj, mp_machine_quadspi_write);

static const mp_rom_map_elem_t machine_quadspi_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_quadspi_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_quadspi_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_machine_quadspi_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_machine_quadspi_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_machine_quadspi_write_obj) },

    { MP_ROM_QSTR(MP_QSTR_MSB), MP_ROM_INT(MICROPY_PY_MACHINE_SPI_MSB) },
};
MP_DEFINE_CONST_DICT(mp_machine_quadspi_locals_dict, machine_quadspi_locals_dict_table);

#endif // MICROPY_PY_MACHINE_QUADSPI || MICROPY_PY_MACHINE_OCTOSPI
