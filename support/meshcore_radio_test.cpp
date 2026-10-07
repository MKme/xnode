// Executes the production SX1262 adapter against a deterministic fake radio.
// Does not simulate RF, SPI electrical behavior, or cryptographic interop.
#include <Dispatcher.h>
#include "app/meshcore/meshcore_logic.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
using namespace xnode_meshcore;
constexpr int RADIOLIB_ERR_NONE = 0;
constexpr uint32_t RADIOLIB_SX126X_IRQ_TX_DONE=1, RADIOLIB_SX126X_IRQ_RX_DONE=2,
    RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED=4, RADIOLIB_SX126X_IRQ_HEADER_VALID=16,
    RADIOLIB_SX126X_IRQ_HEADER_ERR=32, RADIOLIB_SX126X_IRQ_CRC_ERR=64,
    RADIOLIB_SX126X_IRQ_TIMEOUT=512;
static uint32_t now = 100;
uint32_t millis() { return now; }
volatile bool radio_irq = false;
void radio_isr() { radio_irq = true; }
void status(const char *, ...) {}
struct SX1262 {
    uint32_t flags=0;
    int rx_result=0, tx_result=0, finish_result=0, read_result=0;
    unsigned starts=0, finishes=0, reads=0;
    size_t length=0;
    uint8_t bytes[256]={};
    void setDio1Action(void (*)()) {}
#if RADIOLIB_VERSION_MAJOR >= 7
    uint32_t getIrqFlags() { return flags; }
#else
    uint16_t getIrqStatus() { return flags; }
#endif
    int startReceive() { flags=0; return rx_result; }
    void standby() {}
    size_t getPacketLength() { return length; }
    int readData(uint8_t *out, size_t n) { ++reads; memcpy(out,bytes,n); return read_result; }
    float getRSSI() { return -80; }
    float getSNR() { return 7.5; }
    unsigned long getTimeOnAir(int bytes) { return bytes * 1000; }
    int startTransmit(uint8_t *, int) { ++starts; flags=0; return tx_result; }
    int finishTransmit() { ++finishes; return finish_result; }
} hardware;
SX1262 &hardware_radio() { return hardware; }
#include "meshcore_radio_actual.inc"

void incoming(size_t n, uint32_t flags=RADIOLIB_SX126X_IRQ_RX_DONE) {
    hardware.length=n; hardware.flags=flags; radio_irq=true;
}
int main() {
    uint8_t out[255]={};
    radio.begin();
    assert(radio.isInRecvMode());
    assert(radio.recvRaw(out,sizeof(out))==0);
    // Valid group frame: header/path + hash/MAC + one cipher block.
    hardware.bytes[0]=(5<<2)|1; hardware.bytes[1]=0;
    incoming(21);
    assert(radio.recvRaw(out,sizeof(out))==21);
    assert(radio.isInRecvMode() && !radio_irq);
    assert(radio.getLastRSSI()==-80 && radio.getLastSNR()==7.5);
    incoming(21); radio_irq=false; radio.loop(); // Lost RX DIO is recovered by poll.
    assert(radio.recvRaw(out,sizeof(out))==21);
    incoming(21,RADIOLIB_SX126X_IRQ_RX_DONE|RADIOLIB_SX126X_IRQ_CRC_ERR);
    assert(radio.recvRaw(out,sizeof(out))==0 && radio.isInRecvMode());
    incoming(21,RADIOLIB_SX126X_IRQ_RX_DONE|RADIOLIB_SX126X_IRQ_HEADER_ERR);
    assert(radio.recvRaw(out,sizeof(out))==0);
    hardware.read_result=-1; incoming(21);
    assert(radio.recvRaw(out,sizeof(out))==0); hardware.read_result=0;
    incoming(1); assert(radio.recvRaw(out,sizeof(out))==0);
    hardware.bytes[0]=(5<<2)|0; // Incomplete optional transport header.
    incoming(5); assert(radio.recvRaw(out,sizeof(out))==0);
    hardware.bytes[0]=(5<<2)|1;
    incoming(256); unsigned reads=hardware.reads;
    assert(radio.recvRaw(out,sizeof(out))==0 && hardware.reads==reads);
    incoming(21); assert(radio.recvRaw(out,20)==0);
    assert(!radio.startSendRaw(out,0));
    assert(!radio.startSendRaw(out,256));
    radio_irq=true; assert(!radio.startSendRaw(out,21)); radio_irq=false;
    hardware.tx_result=-1;
    assert(!radio.startSendRaw(out,21) && radio.isInRecvMode()); hardware.tx_result=0;
    assert(radio.startSendRaw(out,21) && !radio.isInRecvMode());
    assert(!radio.startSendRaw(out,21)); // Never interrupt an already active TX.
    assert(!radio.isSendComplete());
    hardware.flags=RADIOLIB_SX126X_IRQ_TX_DONE; radio_irq=false; // Lost IRQ, polling recovers.
    assert(radio.isSendComplete() && radio.isInRecvMode());
    const auto finishes=hardware.finishes;
    radio.onSendFinished(); assert(hardware.finishes==finishes);
    assert(!radio.isSendComplete());
    assert(radio.startSendRaw(out,21));
    hardware.flags=RADIOLIB_SX126X_IRQ_TX_DONE|RADIOLIB_SX126X_IRQ_TIMEOUT;
    assert(!radio.isSendComplete());
    radio.onSendFinished(); assert(radio.isInRecvMode());
    assert(radio.startSendRaw(out,21)); hardware.flags=RADIOLIB_SX126X_IRQ_TX_DONE;
    hardware.finish_result=-1;
    assert(!radio.isSendComplete() && radio.isInRecvMode());
    radio.onSendFinished(); hardware.finish_result=0;
    hardware.flags=RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED;
    assert(radio.isReceiving());
    now+=1300; assert(!radio.isReceiving() && radio.isInRecvMode()); // False preamble recovery.
    hardware.rx_result=-1; radio.start_rx(); assert(!radio.isInRecvMode());
    hardware.rx_result=0; radio.loop(); assert(radio.isInRecvMode());
    puts("MeshCore production radio adapter: malformed/CRC/oversize RX, busy/start/finish errors, timeout, lost IRQ, RX recovery PASS");
}
