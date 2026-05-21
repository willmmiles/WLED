# ESP8266 only: relocate C/C++ string literals from DRAM to flash.
#
# By default, GCC places string literals in .rodata.str* sections which the
# ESP8266 linker script maps to DRAM.  This post-script injects those section
# patterns into the irom0 flash segment before the DRAM .rodata catch-all.
# Because GNU ld assigns each input section to the first matching output
# section, the injection alone is sufficient — no removal from the DRAM
# section is needed.
#
# After this patch, function-local string literals are already in flash and
# PSTR() is redundant for them.  The NON32XFER_HANDLER already required by
# the WLED build handles any unaware byte-read call sites as a performance,
# not a correctness, issue.
Import("env")
from pathlib import Path

INJECTION = (
    "\n    /* String literals relocated to flash (PSTR no longer required) */\n"
    "    *(.rodata.str1.*)\n"
    "    *(.rodata.str4.*)\n"
    "    "
)

if env.get("PIOPLATFORM") == "espressif8266":
    # The ESP8266 framework preprocesses eagle.app.v6.common.ld.h into
    # local.eagle.app.v6.common.ld in $BUILD_DIR/ld/ at build time.  Register
    # a post-action on that generated file so the injection happens after
    # C-preprocessing but before linking.
    build_ld = Path(env.subst("$BUILD_DIR")) / "ld" / "local.eagle.app.v6.common.ld"
    MARKER = "_irom0_text_end = ABSOLUTE(.);"

    def patch_esp8266_ld(target, source, env):
        original = build_ld.read_text()
        marker_pos = original.find(MARKER)
        if marker_pos < 0:
            raise RuntimeError(
                f"esp8266_str_to_flash: marker not found in linker script: {build_ld}"
            )
        build_ld.write_text(original[:marker_pos] + INJECTION + original[marker_pos:])

    env.AddPostAction(str(build_ld), patch_esp8266_ld)
