#!/usr/bin/env python3
"""Real MeshCore/Crypto 0.4.0 interoperability vectors, no mocked cryptography.

Pass --crypto-dir pointing at a verified rweather/Crypto 0.4.0 installation.
The runner never downloads or installs packages. Python cryptography is used as
an independent AES/Ed25519 implementation, hashlib/hmac verify framing/MAC/hash.
"""
from pathlib import Path
import argparse
import hashlib
import hmac
import json
import os
import subprocess
import tempfile
try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
except ImportError:
    raise SystemExit("Python cryptography is required for independent verification. Install it in your development environment before running this optional test.")

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser()
parser.add_argument("--crypto-dir", type=Path, help="Installed Crypto 0.4.0 root; otherwise CRYPTO_DIR or .pio/libdeps/*/Crypto")
args = parser.parse_args()
choice = args.crypto_dir or (Path(os.environ["CRYPTO_DIR"]) if os.environ.get("CRYPTO_DIR") else None)
if choice is None:
    choice = next(iter(sorted((ROOT / ".pio/libdeps").glob("*/Crypto"))), None)
if choice is None:
    parser.error("Crypto 0.4.0 is not installed. First install the project's pinned dependencies, or pass --crypto-dir /path/to/Crypto (or CRYPTO_DIR). This test does not download packages.")
crypto = choice.resolve()
if not (crypto / "AES.h").exists():
    crypto /= "src"
assert (crypto / "AES.h").is_file(), "Provide the installed Crypto 0.4.0 root or src directory"
metadata = crypto / "library.json"
if not metadata.exists(): metadata = crypto.parent / "library.json"
assert json.loads(metadata.read_text())["version"] == "0.4.0", "This test requires pinned Crypto 0.4.0"

STREAM = r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
class Stream {
public:
    virtual size_t readBytes(uint8_t *, size_t) { return 0; }
    virtual size_t write(const uint8_t *, size_t) { return 0; }
    void print(char) {} void print(const char *) {} void println() {}
};
'''
ARDUINO = r'''
#pragma once
#include "Stream.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
inline char *ltoa(long value, char *out, int) { sprintf(out, "%ld", value); return out; }
'''
TEST = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>
#include "meshcore_packet_pool.h"
#include "meshcore_logic.h"
class TestRadio : public mesh::Radio {
public:
 int recvRaw(uint8_t *, int) override { return 0; }
 uint32_t getEstAirtimeFor(int) override { return 1; }
 float packetScore(float, int) override { return 0; }
 bool startSendRaw(const uint8_t *, int) override { return true; }
 bool isSendComplete() override { return true; }
 void onSendFinished() override {}
 bool isInRecvMode() const override { return true; }
};
class TestClock : public mesh::MillisecondClock, public mesh::RTCClock {
public:
 unsigned long getMillis() override { return 100; }
 uint32_t getCurrentTime() override { return 1700000000; }
 void setCurrentTime(uint32_t) override {}
};
class TestRng : public mesh::RNG {
public:
 uint8_t seed[32];
 TestRng() { mesh::Utils::fromHex(seed, sizeof(seed), "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"); }
 void random(uint8_t *out, size_t size) override { for (size_t i = 0; i < size; ++i) out[i] = seed[i % 32]; }
};
class TestChat : public BaseChatMesh {
public:
 mesh::GroupChannel group = {};
 int messages = 0, adverts = 0;
 char text[201] = {};
 double lat = 0, lon = 0;
 TestChat(TestRadio &r, TestClock &c, TestRng &rng, MeshCorePacketPool &p, SimpleMeshTables &t)
 : BaseChatMesh(r,c,rng,c,p,t) {
   mesh::Utils::fromHex(group.secret, 16, "8b3387e9c5cdea6ac9e5edbaa115cd72");
   mesh::Utils::sha256(group.hash, 1, group.secret, 16);
   self_id = mesh::LocalIdentity(&rng);
 }
 void receive(const uint8_t *wire, unsigned n) {
   assert(xnode_meshcore::valid_radio_frame(wire, n));
   mesh::Packet p; assert(tryParsePacket(&p, wire, n)); onRecvPacket(&p);
 }
protected:
 bool allowPacketForward(const mesh::Packet *) override { return false; }
 void onDiscoveredContact(ContactInfo &, bool, uint8_t, const uint8_t *) override {}
 ContactInfo *processAck(const uint8_t *) override { return nullptr; }
 void onContactPathUpdated(const ContactInfo &) override {}
 void onMessageRecv(const ContactInfo &, mesh::Packet *, uint32_t, const char *) override {}
 void onCommandDataRecv(const ContactInfo &, mesh::Packet *, uint32_t, const char *) override {}
 void onSignedMessageRecv(const ContactInfo &, mesh::Packet *, uint32_t, const uint8_t *, const char *) override {}
 uint32_t calcFloodTimeoutMillisFor(uint32_t) const override { return 10; }
 uint32_t calcDirectTimeoutMillisFor(uint32_t, uint8_t) const override { return 10; }
 void onSendTimeout() override {}
 void onChannelMessageRecv(const mesh::GroupChannel &, mesh::Packet *, uint32_t t, const char *s) override {
   assert(t == 1700000000); ++messages; strcpy(text, s);
 }
 uint8_t onContactRequest(const ContactInfo &, uint32_t, const uint8_t *, uint8_t, uint8_t *) override { return 0; }
 void onContactResponse(const ContactInfo &, const uint8_t *, uint8_t) override {}
 int searchChannelsByHash(const uint8_t *hash, mesh::GroupChannel *out, int max) override {
   if (max && *hash == group.hash[0]) { out[0] = group; return 1; } return 0;
 }
 void onAdvertRecv(mesh::Packet *, const mesh::Identity &, uint32_t, const uint8_t *data, size_t n) override {
   AdvertDataParser parser(data,n); assert(parser.isValid() && parser.hasLatLon());
   assert(!strcmp(parser.getName(), "XNODE")); lat = parser.getLat(); lon = parser.getLon(); ++adverts;
 }
};
void print_hex(const char *name, const uint8_t *bytes, size_t n) {
 printf("%s=", name); for (size_t i = 0; i < n; ++i) printf("%02x", bytes[i]); puts("");
}
int main() {
 TestRadio radio; TestClock clock; TestRng rng;
 MeshCorePacketPool pool1, pool2; SimpleMeshTables table1, table2;
 TestChat sender(radio,clock,rng,pool1,table1);
 uint8_t expected[64], signature[64];
 assert(mesh::Utils::fromHex(expected,32,"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
 assert(!memcmp(sender.self_id.pub_key,expected,32));
 sender.self_id.sign(signature,reinterpret_cast<const uint8_t *>(""),0);
 assert(mesh::Utils::fromHex(expected,64,"e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"));
 assert(!memcmp(signature,expected,64));
 assert(sender.self_id.verify(signature,reinterpret_cast<const uint8_t *>(""),0));
 signature[0] ^= 1; assert(!sender.self_id.verify(signature,reinterpret_cast<const uint8_t *>(""),0));
 uint8_t identity[96], restored[96]; assert(sender.self_id.writeTo(identity,sizeof(identity)) == sizeof(identity));
 mesh::LocalIdentity clone; clone.readFrom(identity,sizeof(identity)); assert(clone.writeTo(restored,sizeof(restored)) == sizeof(restored));
 assert(!memcmp(identity,restored,sizeof(identity)) && clone.matches(sender.self_id));
 rng.seed[0] ^= 1; TestChat receiver(radio,clock,rng,pool2,table2);
 sender.begin(); receiver.begin();
 const char *body = "MeshCore interoperability";
 assert(sender.sendGroupMessage(1700000000,sender.group,"XNODE",body,strlen(body)));
 auto packet = pool1.getOutboundByIdx(0); assert(packet);
 uint8_t wire[255]; unsigned n = packet->writeTo(wire); print_hex("GROUP",wire,n);
 uint8_t hash[MAX_HASH_SIZE]; packet->calculatePacketHash(hash); print_hex("HASH",hash,sizeof(hash));
 receiver.receive(wire,n); assert(receiver.messages == 1 && !strcmp(receiver.text,"XNODE: MeshCore interoperability"));
 receiver.receive(wire,n); assert(receiver.messages == 1); // deduplicated
 wire[n-1] ^= 1; receiver.receive(wire,n); assert(receiver.messages == 1); // bad MAC rejected
 pool1.free(packet);
 for (unsigned i = 16; i < 32; ++i) sender.group.secret[i] = receiver.group.secret[i] = i;
 mesh::Utils::sha256(sender.group.hash,1,sender.group.secret,32); receiver.group.hash[0] = sender.group.hash[0];
 assert(sender.sendGroupMessage(1700000000,sender.group,"XNODE",body,strlen(body)));
 packet = pool1.getOutboundByIdx(0); assert(packet); n = packet->writeTo(wire); print_hex("GROUP32",wire,n);
 receiver.receive(wire,n); assert(receiver.messages == 2); pool1.free(packet);
 auto advert = sender.createSelfAdvert("XNODE",37.7749,-122.4194); assert(advert);
 sender.sendFlood(advert); n = advert->writeTo(wire); print_hex("ADVERT",wire,n);
 receiver.receive(wire,n); assert(receiver.adverts == 1 && fabs(receiver.lat-37.7749)<0.000002 && fabs(receiver.lon+122.4194)<0.000002);
 wire[2+32+4] ^= 1; receiver.receive(wire,n); assert(receiver.adverts == 1); // forged signature rejected
}
'''
with tempfile.TemporaryDirectory(prefix="meshcore-crypto-") as name:
    temp = Path(name)
    (temp / "Arduino.h").write_text(ARDUINO)
    (temp / "Stream.h").write_text(STREAM)
    windows_rng_link = '''
#undef SEED_SIZE
#include "RNG.h"
#include <cstdlib>
// PE/COFF retains dormant Crypto RNG references that ELF section GC discards.
// MeshCore uses the explicit RFC8032 TestRng above. Fail if this unused path runs.
RNGClass::RNGClass() {}
RNGClass::~RNGClass() {}
void RNGClass::rand(uint8_t *, size_t) { std::abort(); }
RNGClass RNG;
''' if os.name == "nt" else ""
    (temp / "test.cpp").write_text(TEST + windows_rng_link)
    objects = []
    for source in (ROOT / "lib/meshcore_ed25519").glob("*.c"):
        obj = temp / (source.stem + ".o")
        subprocess.run([os.environ.get("CC", "cc"), "-O1", "-DED25519_NO_SEED=1", "-ffunction-sections", "-fdata-sections", "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    core = ROOT / "lib/meshcore/src"
    sources = list(core.glob("*.cpp")) + list((core / "helpers").glob("*.cpp"))
    crypto_sources = [crypto / (n + ".cpp") for n in ("AES128", "AESCommon", "BlockCipher", "Crypto", "Hash", "SHA256", "SHA512", "Ed25519", "Curve25519", "BigNumberUtil")]
    executable = temp / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-O1", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-I"+str(core), "-I"+str(ROOT / "lib/meshcore_ed25519"), "-I"+str(ROOT / "src/app/meshcore"), "-I"+str(crypto), "-I"+str(temp), str(temp / "test.cpp"), *map(str,sources+crypto_sources), *objects, "-o", str(executable)], check=True)
    output = subprocess.check_output([str(executable)], text=True)
    vectors = dict(line.split("=",1) for line in output.strip().splitlines())

key = bytes.fromhex("8b3387e9c5cdea6ac9e5edbaa115cd72")
plaintext = (1700000000).to_bytes(4,"little") + b"\x00XNODE: MeshCore interoperability"
padded = plaintext + b"\0" * ((-len(plaintext)) % 16)
cipher = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
encrypted = cipher.update(padded) + cipher.finalize()
mac = hmac.new(key + b"\0"*16, encrypted, hashlib.sha256).digest()[:2]
expected = b"\x15\x00" + hashlib.sha256(key).digest()[:1] + mac + encrypted
actual = bytes.fromhex(vectors["GROUP"])
assert actual == expected, "Independent AES/HMAC/group wire encoding differs"
assert bytes.fromhex(vectors["HASH"]) == hashlib.sha256(b"\x05"+actual[2:]).digest()[:8]
key32 = key + bytes(range(16,32))
expected32 = b"\x15\x00" + hashlib.sha256(key32).digest()[:1] + hmac.new(key32, encrypted, hashlib.sha256).digest()[:2] + encrypted
assert bytes.fromhex(vectors["GROUP32"]) == expected32, "32-byte PSK key hash/MAC differs"
advert = bytes.fromhex(vectors["ADVERT"])
assert advert[0] == 0x11 and advert[1] == 0
payload = advert[2:]
Ed25519PublicKey.from_public_bytes(payload[:32]).verify(payload[36:100], payload[:36] + payload[100:])
print("MeshCore real-crypto tests passed: RFC8032 identity/signature, 96-byte persistence roundtrip, independent AES/HMAC/group wire/hash, group RX/dedup/tamper rejection, signed position adverts")
