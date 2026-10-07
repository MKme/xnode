"""Native tests for the exact protocol policy and coverage of the legacy facade."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("A local C++ compiler is required")
(ROOT / ".pio").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="xnode-mesh-protocol-", dir=ROOT / ".pio") as scratch:
    binary = str(Path(scratch) / "mesh_protocol_test")
    subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "src"), str(ROOT / "support/mesh_protocol_test.cpp"),
                    "-o", binary], check=True)
    subprocess.run([binary], check=True)
    # Compile the unchanged production service with small platform fixtures.
    fixture = Path(scratch)
    (fixture / "freertos").mkdir()
    (fixture / "freertos/FreeRTOS.h").write_text("#pragma once\n#define portMAX_DELAY 0xffffffff\n")
    (fixture / "freertos/semphr.h").write_text("""#pragma once
struct StaticSemaphore_t {};
using SemaphoreHandle_t = StaticSemaphore_t*;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *p) { return p; }
inline void xSemaphoreTake(SemaphoreHandle_t, unsigned) {}
inline void xSemaphoreGive(SemaphoreHandle_t) {}
""")
    (fixture / "Preferences.h").write_text("""#pragma once
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
namespace fake_nvs { extern int value; extern unsigned writes; extern bool allow_open, allow_write; }
class Preferences {
public:
 bool begin(const char *name, bool readonly) { assert(!strcmp(name, "xnode-mesh")); return fake_nvs::allow_open && (!readonly || fake_nvs::value >= 0); }
 uint8_t getUChar(const char *key, uint8_t fallback) { assert(!strcmp(key, "protocol")); return fake_nvs::value >= 0 ? fake_nvs::value : fallback; }
 size_t putUChar(const char *key, uint8_t v) { assert(!strcmp(key, "protocol")); ++fake_nvs::writes; if (!fake_nvs::allow_write) return 0; fake_nvs::value = v; return 1; }
 void end() {}
};
""")
    binary = str(fixture / "mesh_protocol_storage_test")
    subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-DUSING_TWATCH_ULTRA", "-I" + scratch, "-I" + str(ROOT / "src"),
                    str(ROOT / "support/mesh_protocol_storage_test.cpp"),
                    str(ROOT / "src/app/mesh/mesh_protocol.cpp"), "-o", binary], check=True)
    for saved in ("-1", "0", "1", "255"):
        subprocess.run([binary, saved], check=True)

# Every public compatibility API must choose the boot-selected protocol before
# entering legacy logic, including the history APIs outside the target guard.
header = (ROOT / "src/app/meshtastic/meshtastic_service.h").read_text()
source = (ROOT / "src/app/meshtastic/meshtastic_service.cpp").read_text()
names = re.findall(r"\b(meshtastic_service_\w+)\s*\([^;{}]*\);", header)
for name in names:
    matches = list(re.finditer(r"\b" + name + r"\s*\([^;{}]*\)\s*\{\s*"
                              r"MESHCORE_(RETURN|VOID)\(" + name.removeprefix("meshtastic_service_"), source))
    assert matches, "Missing protocol dispatch: " + name
assert "get_selected() == MESH_PROTOCOL_MESHCORE" not in source, "Pending choice must not change radio owner"
print(f"All {len(names)} legacy facade APIs dispatch by immutable active protocol: PASS")

# The service return only means accepted into a TX queue, including SOS/check-in.
for app, queued in (("xnode_sos", "SOS queued"), ("xnode_checkin", "Check-in queued")):
    text = (ROOT / f"src/gui/mainbar/app_tile/{app}/{app}.cpp").read_text()
    assert queued in text and "sent over mesh" not in text
print("SOS/check-in UI does not turn queue acceptance into transmission/delivery success: PASS")
