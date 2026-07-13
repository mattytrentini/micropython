#define MICROPY_HW_BOARD_NAME               "LILYGO T4-S3"
#define MICROPY_HW_MCU_NAME                 "ESP32-S3R8"
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT "mpy-t4s3"

// machine.QuadSPI(1) default pins -- 2.41" RM690B0 AMOLED display (600x450).
// Source: Xinyuan-LilyGO/LilyGo-AMOLED-Series src/LilyGo_AMOLED.h,
// RM690B0_AMOLED DisplayConfigure_t (BOARD_AMOLED_241 / T4-S3).
// The display's CS (GPIO11) and RESET (GPIO13) are not part of the QuadSPI
// bus itself (this port's QuadSPI has no hardware CS -- see
// ports/esp32/machine_hw_quadspi.c) and should be driven directly as
// machine.Pin objects by the display driver. TE (tearing effect, GPIO18) is
// likewise a plain input pin, not part of the bus.
#define MICROPY_HW_QUADSPI1_SCK             (15)
#define MICROPY_HW_QUADSPI1_IO0             (14)
#define MICROPY_HW_QUADSPI1_IO1             (10)
#define MICROPY_HW_QUADSPI1_IO2             (16)
#define MICROPY_HW_QUADSPI1_IO3             (12)

// I2C0 default pins -- shared bus for touch (CS226SE) and PMU (SY6970)
#define MICROPY_HW_I2C0_SCL                 (7)
#define MICROPY_HW_I2C0_SDA                 (6)

// SPI1 default pins -- onboard microSD slot
#define MICROPY_HW_SPI1_SCK                 (3)
#define MICROPY_HW_SPI1_MOSI                (2)
#define MICROPY_HW_SPI1_MISO                (4)
