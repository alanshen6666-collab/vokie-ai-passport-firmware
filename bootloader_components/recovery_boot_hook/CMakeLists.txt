# $ENV{IDF_PATH} uses native separators on Windows (e.g. C:\Espressif\...).
# CMake re-parses list elements in foreach(), where a backslash starts an
# escape sequence, so normalize to forward slashes first (no-op on Unix).
file(TO_CMAKE_PATH "$ENV{IDF_PATH}" recovery_boot_hook_idf_path)

idf_component_register(
    SRCS "recovery_boot_hook.c"
    PRIV_REQUIRES bootloader_support
    PRIV_INCLUDE_DIRS "${recovery_boot_hook_idf_path}/components/bootloader_support/private_include"
)
