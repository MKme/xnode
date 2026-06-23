from pathlib import Path
import subprocess

from SCons.Script import COMMAND_LINE_TARGETS

Import("env")


def _normalize_port(port):
    port = (port or "").strip().strip('"')
    return port if port and "$" not in port else ""


def _find_watchdog_esptool(python_exe):
    packages_dir = Path.home() / ".platformio" / "packages"
    candidates = [
        packages_dir / "tool-esptoolpy" / "esptool.py",
        Path(env.PioPlatform().get_package_dir("tool-esptoolpy") or "") / "esptool.py",
    ]

    for candidate in candidates:
        if not candidate.is_file():
            continue
        try:
            help_result = subprocess.run(
                [python_exe, str(candidate), "--help"],
                capture_output=True,
                text=True,
                timeout=15,
            )
        except Exception:
            continue

        if "watchdog_reset" in (help_result.stdout + help_result.stderr):
            return candidate

    return None


def _reset_tdeck_after_upload(source, target, env):
    port = _normalize_port(env.subst("$UPLOAD_PORT"))
    if not port:
        print("warning: T-Deck Plus post-upload reset skipped; upload port is unknown")
        return 0

    python_exe = env.subst("$PYTHONEXE")
    esptool = _find_watchdog_esptool(python_exe)
    if not esptool:
        print("warning: T-Deck Plus post-upload watchdog reset skipped; no local esptool with watchdog_reset was found")
        return 0

    command = [
        python_exe,
        str(esptool),
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        env.subst("$UPLOAD_SPEED"),
        "--before",
        "no_reset",
        "--after",
        "watchdog_reset",
        "chip_id",
    ]
    print("Resetting T-Deck Plus via watchdog after upload...")
    result = subprocess.run(command, capture_output=True, text=True, timeout=45)
    output = (result.stdout or "") + (result.stderr or "")
    reset_issued = "Hard resetting with a watchdog" in output
    if output:
        if reset_issued:
            reset_output = output.split("Hard resetting with a watchdog", 1)[0]
            print((reset_output + "Hard resetting with a watchdog...").strip())
            if result.returncode != 0:
                print("T-Deck Plus watchdog reset issued; USB disconnect during reset is expected.")
        else:
            print(output.strip())

    if result.returncode == 0 or reset_issued:
        return 0

    return result.returncode


if "upload" in COMMAND_LINE_TARGETS:
    env.AddPostAction("upload", _reset_tdeck_after_upload)
