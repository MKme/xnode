#!/usr/bin/env python3
"""Native regression tests for production validation and fixed packet queues.

No mocked crypto is presented as interoperability testing. The test compiles
meshcore_logic.h, meshcore_packet_pool.h and upstream Packet.cpp; a no-op hash
stub only satisfies Packet.cpp linkage. No signing/encryption assertions depend
on it. Real firmware compilation and two-radio interoperability are separate.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
TEST = r'''
#include <cassert>
#include <cstring>
#include <limits>
#include "meshcore_logic.h"
#include "meshcore_packet_pool.h"
using namespace xnode_meshcore;
int main() {
    assert(valid_key_length(16) && valid_key_length(32));
    for (unsigned n = 0; n < 65; ++n) assert(valid_key_length(n) == (n == 16 || n == 32));
    assert(valid_name("XNODE", 32));
    assert(!valid_name("", 32) && !valid_name("bad: name", 32) && !valid_name("bad\nname", 32));
    char full[32]; memset(full, 'x', sizeof(full)); assert(!valid_name(full, sizeof(full)));
    char text[161]; memset(text, 'x', 160); text[160] = 0;
    text[153] = 0; assert(valid_group_text("XNODE", text));
    text[153] = 'x'; text[154] = 0; assert(!valid_group_text("XNODE", text));
    assert(!valid_group_text("XNODE", nullptr) && !valid_group_text("XNODE", ""));
    assert(valid_radio(910.525f, 62.5f, 7, 5, 22));
    assert(!valid_radio(902.0f, 62.5f, 7, 5, 22));
    assert(!valid_radio(928.0f, 62.5f, 7, 5, 22));
    assert(!valid_radio(NAN, 62.5f, 7, 5, 22));
    assert(!valid_radio(INFINITY, 62.5f, 7, 5, 22));
    assert(!valid_radio(910, NAN, 7, 5, 22));
    assert(!valid_radio(910, 500, 7, 5, 22));
    assert(!valid_radio(910, 125, 6, 5, 22));
    assert(!valid_radio(910, 125, 7, 9, 22));
    assert(!valid_radio(910, 125, 7, 5, 23));
    assert(time_due(1, UINT32_MAX) && !time_due(UINT32_MAX, 1));
    assert(valid_location(0, 0) && valid_location(-90, 180));
    assert(!valid_location(NAN, 0) && !valid_location(0, 181));
    char sender[40], body[201];
    split_group_text("Peer: hello: world", sender, sizeof(sender), body, sizeof(body));
    assert(!strcmp(sender, "Peer") && !strcmp(body, "hello: world"));
    split_group_text("unlabelled", sender, sizeof(sender), body, sizeof(body));
    assert(!strcmp(sender, "Channel peer") && !strcmp(body, "unlabelled"));
    assert(!valid_radio_frame(nullptr, 0));
    uint8_t wire[256] = {};
    wire[0] = (5 << 2) | 1; // group text flood
    assert(!valid_radio_frame(wire, 0) && !valid_radio_frame(wire, 1));
    assert(!valid_radio_frame(wire, 2) && !valid_radio_frame(wire, 20));
    assert(valid_radio_frame(wire, 21)); // hash+MAC+one AES block
    assert(!valid_radio_frame(wire, 22)); // partial ciphertext block
    assert(!valid_radio_frame(wire, 256));
    wire[0] |= 0x40; assert(!valid_radio_frame(wire, 21)); wire[0] &= 63;
    wire[1] = 0xc0; assert(!valid_radio_frame(wire, 21)); // reserved path mode
    wire[1] = 63; assert(!valid_radio_frame(wire, 21)); // truncated path
    wire[1] = 0x7f; assert(!valid_radio_frame(wire, 150)); // 126-byte path
    wire[1] = 0;
    wire[0] = (5 << 2); // four transport-code bytes required
    for (unsigned n = 0; n < 6; ++n) assert(!valid_radio_frame(wire, n));
    assert(valid_radio_frame(wire, 25));
    wire[5] = 1; assert(!valid_radio_frame(wire, 25)); assert(valid_radio_frame(wire, 26)); wire[5] = 0;
    wire[0] = (4 << 2) | 1;
    assert(!valid_radio_frame(wire, 101)); assert(!valid_radio_frame(wire, 102)); assert(valid_radio_frame(wire, 103));
    wire[102] = 0x10; assert(!valid_radio_frame(wire, 110)); assert(valid_radio_frame(wire, 111));
    wire[102] = 0x70; assert(!valid_radio_frame(wire, 114)); assert(valid_radio_frame(wire, 115)); wire[102] = 0;
    assert(valid_radio_frame(wire, 134)); assert(!valid_radio_frame(wire, 135));
    wire[0] = (2 << 2) | 1; assert(!valid_radio_frame(wire, 22)); // DMs unsupported
    // All lengths and hostile header/path byte combinations, under sanitizers.
    for (unsigned h = 0; h < 256; ++h) for (unsigned p = 0; p < 256; ++p) {
        wire[0] = h; wire[1] = p; wire[5] = p;
        for (unsigned n : {0U, 1U, 2U, 5U, 6U, 7U, 21U, 100U, 184U, 255U, 256U})
            (void)valid_radio_frame(wire, n);
    }
    MeshCorePacketPool pool;
    assert(pool.getFreeCount() == 12);
    auto a = pool.allocNew(), b = pool.allocNew(), c = pool.allocNew();
    pool.queueOutbound(b, 2, 100); // b is pool slot1, queue slot0
    pool.queueOutbound(c, 1, 100); // c is pool slot2, queue slot1
    pool.free(a); // previously deleted unrelated b's queue slot!
    assert(pool.getOutboundTotal() == 2 && pool.getFreeCount() == 10);
    assert(pool.getNextOutbound(99) == nullptr);
    assert(pool.getNextOutbound(100) == c);
    pool.free(c); assert(pool.getOutboundTotal() == 1);
    assert(pool.getNextOutbound(100) == b); pool.free(b);
    assert(pool.getFreeCount() == 12 && pool.getOutboundTotal() == 0);
    a = pool.allocNew(); b = pool.allocNew();
    pool.queueInbound(b, 1); pool.queueInbound(a, UINT32_MAX);
    assert(pool.getNextInbound(UINT32_MAX) == a); pool.free(a);
    assert(pool.getNextInbound(1) == b); pool.free(b);
    mesh::Packet *packets[12];
    for (auto &packet : packets) { packet = pool.allocNew(); assert(packet); pool.queueOutbound(packet, 0, 5); }
    assert(pool.allocNew() == nullptr && pool.getOutboundTotal() == 12);
    pool.free(packets[5]); assert(pool.getOutboundTotal() == 11);
    auto fresh = pool.allocNew(); assert(fresh == packets[5]);
    pool.queueOutbound(fresh, 0, 5);
    unsigned drained = 0;
    while (auto packet = pool.getNextOutbound(5)) { ++drained; pool.free(packet); }
    assert(drained == 12 && pool.getFreeCount() == 12);
    a = pool.allocNew(); b = pool.allocNew();
    pool.queueOutbound(b, 0, 10); pool.queueOutbound(a, 0, 20);
    assert(pool.getOutboundByIdx(0) == b && pool.removeOutboundByIdx(0) == b);
    pool.free(b); assert(pool.getOutboundTotal() == 1 && pool.getNextOutbound(20) == a); pool.free(a);
}
'''
with tempfile.TemporaryDirectory(prefix="meshcore-native-") as temp:
    directory = Path(temp)
    (directory / "Stream.h").write_text("#pragma once\nclass Stream {};\n")
    (directory / "SHA256.h").write_text("""#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
class SHA256 { public: void update(const void*, size_t) {} void finalize(uint8_t* out, size_t size) { memset(out, 0, size); } };
""")
    (directory / "test.cpp").write_text("#include <initializer_list>\n" + TEST)
    compiler = os.environ.get("CXX", "c++")
    executable = directory / "test"
    command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-reorder", "-Wno-misleading-indentation", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-I" + str(ROOT / "src/app/meshcore"), "-I" + str(ROOT / "lib/meshcore/src"), "-I" + str(directory), str(directory / "test.cpp"), str(ROOT / "lib/meshcore/src/Packet.cpp"), "-o", str(executable)]
    if os.name == "nt":
        # LLVM-MinGW ships no ASan/UBSan runtime. Run the same exhaustive assertions
        # and strict compiler diagnostics, explicitly reporting the reduced instrumentation.
        command.remove("-fsanitize=address,undefined")
        executable = directory / "test.exe"
        command[-1] = str(executable)
    subprocess.run(command, check=True)
    environment = os.environ.copy()
    # The managed executor uses ptrace; LeakSanitizer cannot run under ptrace.
    # Address/undefined-behavior instrumentation remains active. Pool has no heap.
    environment["ASAN_OPTIONS"] = "detect_leaks=0"
    subprocess.run([str(executable)], check=True, env=environment)
print("MeshCore native validation/pool tests passed (Windows assertions; sanitizer runtime unavailable)" if os.name == "nt" else "MeshCore native validation/pool tests passed (ASan + UBSan)")
