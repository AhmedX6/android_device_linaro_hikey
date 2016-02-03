# Primary Arch
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_VARIANT := generic
TARGET_CPU_ABI := arm64-v8a

# Secondary Arch
TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv7-a-neon
TARGET_2ND_CPU_VARIANT := cortex-a15
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi

TARGET_USES_64_BIT_BINDER := true
TARGET_SUPPORTS_32_BIT_APPS := true
TARGET_SUPPORTS_64_BIT_APPS := true

TARGET_BOARD_PLATFORM := hikey
WITH_DEXPREOPT ?= true
USE_OPENGL_RENDERER := true
ANDROID_ENABLE_RENDERSCRIPT := true

# BT configs
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := "device/linaro/hikey/bluetooth"
BOARD_HAVE_BLUETOOTH := true

# generic wifi
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
CONFIG_DRIVER_NL80211 := y

BOARD_KERNEL_CMDLINE := k3v2mem hisi_dma_print=0 vmalloc=484M no_irq_affinity loglevel=7 androidboot.hardware=hikey selinux=0

TARGET_NO_BOOTLOADER := true
TARGET_NO_KERNEL := false
TARGET_NO_RECOVERY := true
TARGET_HARDWARE_3D := true
BOARD_USES_GENERIC_AUDIO := true
USE_CAMERA_STUB := true
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1342177280
ifeq ($(TARGET_USERDATAIMAGE_4GB), true)
BOARD_USERDATAIMAGE_PARTITION_SIZE := 1595915776
else
BOARD_USERDATAIMAGE_PARTITION_SIZE := 5588893184
endif
BOARD_CACHEIMAGE_PARTITION_SIZE := 268435456
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_FLASH_BLOCK_SIZE := 131072
TARGET_USE_PAN_DISPLAY := true

BOARD_SEPOLICY_DIRS := device/linaro/hikey/sepolicy

ifeq ($(HOST_OS), linux)
ifeq ($(TARGET_SYSTEMIMAGES_USE_SQUASHFS), true)
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := squashfs
endif
endif
