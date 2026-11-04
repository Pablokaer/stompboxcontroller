# PlatformIO post-build step: copies the firmware to ../release/ as
#   stompbox-esp32c3-app.bin     for updates over Wi-Fi (http://<device>/update)
#   stompbox-esp32c3-full.bin    bootloader + partitions + app, flash at 0x0 (first install)
import os, shutil, subprocess, sys
Import("env")  # noqa: F821


def merge(source, target, env):
    build = env.subst("$BUILD_DIR")
    rel = os.path.join(env.subst("$PROJECT_DIR"), "release")
    os.makedirs(rel, exist_ok=True)
    app = os.path.join(build, "firmware.bin")
    shutil.copy(app, os.path.join(rel, "stompbox-esp32c3-app.bin"))
    images = env.get("FLASH_EXTRA_IMAGES", [])
    args = [sys.executable, os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py"),
            "--chip", "esp32c3", "merge_bin", "-o", os.path.join(rel, "stompbox-esp32c3-full.bin"),
            "--flash_mode", "dio", "--flash_size", "4MB"]
    for addr, path in images:
        args += [addr, env.subst(path)]
    args += [env.subst("$ESP32_APP_OFFSET"), app]
    subprocess.check_call(args)
    print("merge_bin: release/stompbox-esp32c3-full.bin and release/stompbox-esp32c3-app.bin written")


env.AddPostAction("$BUILD_DIR/firmware.bin", merge)  # noqa: F821
