QR_MOD_DIR := $(USERMOD_DIR)

# Add all C files to SRC_USERMOD.
SRC_USERMOD += $(QR_MOD_DIR)/qrcodegen.c $(QR_MOD_DIR)/qr.c

# We can add our module folder to include paths if needed
# This is not actually needed in this example.
CFLAGS_USERMOD += -I$(QR_MOD_DIR)