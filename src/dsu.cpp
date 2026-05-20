#include "dsu.h"
#include "triton.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <unordered_map>

using namespace std::chrono_literals;

namespace sc2 {

// ── Protocol constants ────────────────────────────────────────────────────────

static const uint8_t MAGIC_CLIENT[4] = {'D','S','U','C'};
static const uint8_t MAGIC_SERVER[4] = {'D','S','U','S'};
static const uint16_t PROTO_VERSION  = 1001;

static const uint32_t MSG_VERSION = 0x100000;
static const uint32_t MSG_PORTS   = 0x100001;
static const uint32_t MSG_DATA    = 0x100002;

static const size_t HEADER_LEN      = 16;
static const size_t HEADER_LEN_FULL = 20;
static const size_t CTRL_HDR_LEN    = 11;
static const size_t CRC_OFFSET      = 8;
static const size_t CRC_LEN         = 4;

static const size_t MAX_SLOTS = 4;
static const size_t MAX_SUBS  = 32;
static const auto   SUB_TIMEOUT = 5s;
static const auto   STAT_INTERVAL = 5s;

// Unique MAC per slot
static const uint8_t SLOT_MACS[4][6] = {
    {0,0,0,0,0,1}, {0,0,0,0,0,2}, {0,0,0,0,0,3}, {0,0,0,0,0,4}
};

static const uint8_t TRIGGER_THRESHOLD = 200;

// ── CRC32 (Cemuhook uses CRC32/ISO-HDLC) ─────────────────────────────────────

static uint32_t crc32Table[256];
static bool     crc32Ready = false;

static void buildCrc32Table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32Table[i] = c;
    }
    crc32Ready = true;
}

static uint32_t crc32(const uint8_t* data, size_t len) {
    if (!crc32Ready) buildCrc32Table();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        c = crc32Table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static uint32_t crcOverZeroed(const uint8_t* msg, size_t len) {
    // CRC is computed with the CRC field itself zeroed
    std::vector<uint8_t> tmp(msg, msg + len);
    memset(tmp.data() + CRC_OFFSET, 0, CRC_LEN);
    return crc32(tmp.data(), len);
}

// ── Wire helpers ──────────────────────────────────────────────────────────────

static uint32_t randServerId() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_nsec ^ (getpid() * 2654435761u));
}

static void writeU16LE(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = v >> 8;
}
static void writeU32LE(uint8_t* p, uint32_t v) {
    p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF;
}
static uint16_t readU16LE(const uint8_t* p) { return p[0] | ((uint16_t)p[1]<<8); }
static uint32_t readU32LE(const uint8_t* p) {
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

static void writeHeader(uint8_t* out, size_t totalLen, uint32_t serverId, uint32_t mtype) {
    memcpy(out, MAGIC_SERVER, 4);
    writeU16LE(out+4, PROTO_VERSION);
    writeU16LE(out+6, (uint16_t)(totalLen - HEADER_LEN));
    memset(out+CRC_OFFSET, 0, CRC_LEN);
    writeU32LE(out+12, serverId);
    writeU32LE(out+16, mtype);
}

static void finalizeCrc(uint8_t* out, size_t len) {
    uint32_t crc = crcOverZeroed(out, len);
    writeU32LE(out + CRC_OFFSET, crc);
}

static void writeCtrlHeader(uint8_t* buf, uint8_t slot, bool connected) {
    buf[0] = slot;
    if (connected && slot < MAX_SLOTS) {
        buf[1] = 1; // connected
        buf[2] = 2; // gyro full
        buf[3] = 1; // USB
        memcpy(buf+4, SLOT_MACS[slot], 6);
        buf[10] = 0; // battery N/A
    } else {
        memset(buf+1, 0, 10);
    }
}

static uint8_t stickToU8(int16_t v) {
    return (uint8_t)((((int32_t)v + 32768)) >> 8);
}
static uint8_t triggerToU8(uint16_t v) {
    uint32_t r = (uint32_t)v >> 7;
    return r > 255 ? 255 : (uint8_t)r;
}

static void writeDataBody(uint8_t* body, const ControllerState& s) {
    body[0] = 1; // connected

    uint8_t l2 = triggerToU8(s.trigger_left);
    uint8_t r2 = triggerToU8(s.trigger_right);
    bool l2d = l2 >= TRIGGER_THRESHOLD;
    bool r2d = r2 >= TRIGGER_THRESHOLD;

    using namespace button;
    auto down = [&](uint32_t mask) { return (s.buttons & mask) != 0; };

    // Buttons byte 1
    body[5] = (uint8_t)(
        (down(MENU)       ? 0x01 : 0) |
        (down(L3)         ? 0x02 : 0) |
        (down(R3)         ? 0x04 : 0) |
        (down(VIEW)       ? 0x08 : 0) |
        (down(DPAD_UP)    ? 0x10 : 0) |
        (down(DPAD_RIGHT) ? 0x20 : 0) |
        (down(DPAD_DOWN)  ? 0x40 : 0) |
        (down(DPAD_LEFT)  ? 0x80 : 0)
    );
    // Buttons byte 2
    body[6] = (uint8_t)(
        (l2d        ? 0x01 : 0) |
        (r2d        ? 0x02 : 0) |
        (down(L)    ? 0x04 : 0) |
        (down(R)    ? 0x08 : 0) |
        (down(X)    ? 0x10 : 0) |
        (down(A)    ? 0x20 : 0) |
        (down(B)    ? 0x40 : 0) |
        (down(Y)    ? 0x80 : 0)
    );
    body[7] = down(STEAM) ? 1 : 0;
    body[8] = down(QAM)   ? 1 : 0;

    // Sticks
    body[9]  = stickToU8(s.left_stick[0]);
    body[10] = stickToU8(s.left_stick[1]);
    body[11] = stickToU8(s.right_stick[0]);
    body[12] = stickToU8(s.right_stick[1]);

    // Analog buttons
    auto full = [](bool on) -> uint8_t { return on ? 255 : 0; };
    body[13] = full(down(DPAD_LEFT));
    body[14] = full(down(DPAD_DOWN));
    body[15] = full(down(DPAD_RIGHT));
    body[16] = full(down(DPAD_UP));
    body[17] = full(down(Y));
    body[18] = full(down(B));
    body[19] = full(down(A));
    body[20] = full(down(X));
    body[21] = full(down(R));
    body[22] = full(down(L));
    body[23] = r2;
    body[24] = l2;
    // 25-36: reserved/touch (zero)
    memset(body+25, 0, 12);

    // Timestamp (microseconds)
    uint64_t ts = s.imu.timestamp_us;
    memcpy(body+37, &ts, 8);

    // Motion: accel X/Y/Z, gyro X/Y/Z (float32 LE)
    float motion[6] = {
        s.imu.accel_g[0], s.imu.accel_g[1], s.imu.accel_g[2],
        s.imu.gyro_dps[0], s.imu.gyro_dps[1], s.imu.gyro_dps[2],
    };
    memcpy(body+45, motion, 24);
}

// ── DsuServer ─────────────────────────────────────────────────────────────────

DsuServer::DsuServer(uint16_t port, bool expose)
    : port_(port), expose_(expose), serverId_(randServerId())
{
    memset(slotConnected_, 0, sizeof(slotConnected_));
    windowStart_ = std::chrono::steady_clock::now();
}

DsuServer::~DsuServer() {
    stop();
}

void DsuServer::start() {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) { perror("socket"); return; }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    addr.sin_addr.s_addr = expose_ ? INADDR_ANY : htonl(INADDR_LOOPBACK);

    if (bind(sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock_); sock_ = -1; return;
    }

    // 10ms read timeout
    struct timeval tv{0, 10000};
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    running_ = true;
    thread_  = std::thread(&DsuServer::serverLoop, this);

    fprintf(stderr, "sc2gyrodsu: DSU server on %s:%u  (id 0x%08X)\n",
        expose_ ? "0.0.0.0" : "127.0.0.1", port_, serverId_);
}

void DsuServer::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (sock_ >= 0) { close(sock_); sock_ = -1; }
}

bool DsuServer::hasSubscribers() const {
    std::lock_guard<std::mutex> lock(subMutex_);
    return !subscribers_.empty();
}

void DsuServer::setDisconnected(int slot) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (slot >= 0 && slot < 4) slotConnected_[slot] = false;
}

void DsuServer::pushState(const ControllerState& state) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        int s = state.slot;
        if (s >= 0 && s < 4) slotConnected_[s] = true;
    }
    samplesInWindow_++;
    broadcastData(state);
}

void DsuServer::serverLoop() {
    uint8_t buf[1024];
    while (running_) {
        struct sockaddr_in src{};
        socklen_t srcLen = sizeof(src);
        int n = recvfrom(sock_, buf, sizeof(buf), 0,
                         (struct sockaddr*)&src, &srcLen);
        if (n > 0) {
            requestsInWindow_++;
            handleMessage(buf, n, src);
        }
        cleanupSubscribers();

        // Stats every 5s
        auto now = std::chrono::steady_clock::now();
        if (now - windowStart_ >= STAT_INTERVAL) {
            float secs = std::chrono::duration<float>(now - windowStart_).count();
            size_t nsubs;
            { std::lock_guard<std::mutex> lock(subMutex_); nsubs = subscribers_.size(); }
            fprintf(stderr,
                "dsu: %zu sub(s) | %.1f samples/s | %.1f packets/s | %.1f reqs/s\n",
                nsubs,
                samplesInWindow_  / secs,
                packetsInWindow_  / secs,
                requestsInWindow_ / secs);
            samplesInWindow_ = packetsInWindow_ = requestsInWindow_ = 0;
            windowStart_ = now;
        }
    }
}

void DsuServer::handleMessage(const uint8_t* buf, int n, struct sockaddr_in& src) {
    if (n < (int)HEADER_LEN_FULL) return;
    if (memcmp(buf, MAGIC_CLIENT, 4) != 0) return;
    if (readU16LE(buf+4) != PROTO_VERSION) return;

    uint16_t payLen  = readU16LE(buf+6);
    if (n < (int)(HEADER_LEN + payLen)) return;

    uint32_t claimedCrc = readU32LE(buf+CRC_OFFSET);
    if (claimedCrc != crcOverZeroed(buf, HEADER_LEN + payLen)) return;

    uint32_t clientId = readU32LE(buf+12);
    uint32_t mtype    = readU32LE(buf+16);

    const uint8_t* payload = buf + HEADER_LEN_FULL;
    int plen = n - HEADER_LEN_FULL;

    switch (mtype) {
        case MSG_VERSION:
            sendVersion(src);
            break;
        case MSG_PORTS:
            if (plen >= 4) {
                uint32_t amount = readU32LE(payload);
                amount = amount > 4 ? 4 : amount;
                for (uint32_t i = 0; i < amount && (int)(4+i) <= plen; ++i)
                    sendSlotInfo(src, payload[4+i]);
            }
            break;
        case MSG_DATA: {
            if (plen < 8) break;
            uint8_t regType = payload[0];
            uint8_t slot    = payload[1];
            uint8_t mac[6]; memcpy(mac, payload+2, 6);

            bool wantsUs = (regType == 0) ||
                           (regType & 1 && slot < MAX_SLOTS) ||
                           (regType & 2 && memcmp(mac, SLOT_MACS[slot < 4 ? slot : 0], 6) == 0);
            if (!wantsUs) break;

            std::lock_guard<std::mutex> lock(subMutex_);
            if (subscribers_.size() >= MAX_SUBS && subscribers_.find(clientId) == subscribers_.end())
                break;

            bool wasEmpty = subscribers_.empty();
            auto& sub = subscribers_[clientId];
            sub.addr = src;
            sub.lastRequest = std::chrono::steady_clock::now();
            if (wasEmpty)
                fprintf(stderr, "dsu: first subscriber %08X — waking controllers\n", clientId);
            break;
        }
    }
}

void DsuServer::sendVersion(struct sockaddr_in& src) {
    uint8_t out[HEADER_LEN_FULL + 2] = {};
    writeHeader(out, sizeof(out), serverId_, MSG_VERSION);
    writeU16LE(out + HEADER_LEN_FULL, PROTO_VERSION);
    finalizeCrc(out, sizeof(out));
    sendto(sock_, out, sizeof(out), 0, (struct sockaddr*)&src, sizeof(src));
}

void DsuServer::sendSlotInfo(struct sockaddr_in& src, uint8_t slot) {
    uint8_t out[HEADER_LEN_FULL + CTRL_HDR_LEN + 1] = {};
    writeHeader(out, sizeof(out), serverId_, MSG_PORTS);
    bool conn;
    { std::lock_guard<std::mutex> lock(stateMutex_); conn = slot < 4 && slotConnected_[slot]; }
    writeCtrlHeader(out + HEADER_LEN_FULL, slot, conn);
    finalizeCrc(out, sizeof(out));
    sendto(sock_, out, sizeof(out), 0, (struct sockaddr*)&src, sizeof(src));
}

void DsuServer::broadcastData(const ControllerState& state) {
    std::lock_guard<std::mutex> lock(subMutex_);
    if (subscribers_.empty()) return;

    uint8_t slot = (uint8_t)(state.slot < 4 ? state.slot : 0);
    // Total packet: header + ctrl_header + 1 (connected byte) + 79 (body)
    // Body layout: 1 connected + 4 buttons + 4 sticks + 12 analog + 12 reserved + 8 ts + 24 motion = 65
    // DSU spec: HEADER_LEN_FULL + CTRL_HDR_LEN + 1 + 79 = 20 + 11 + 1 + 79 = 111... 
    // Actually standard DSU data packet is HEADER(20) + CTRL_HDR(11) + DATA(80) = 111 bytes
    constexpr size_t PKT_LEN = HEADER_LEN_FULL + CTRL_HDR_LEN + 80;
    uint8_t out[PKT_LEN] = {};

    writeHeader(out, PKT_LEN, serverId_, MSG_DATA);
    writeCtrlHeader(out + HEADER_LEN_FULL, slot, true);
    writeDataBody(out + HEADER_LEN_FULL + CTRL_HDR_LEN, state);

    // Packet counter is at HEADER_LEN_FULL + CTRL_HDR_LEN + 1
    constexpr size_t PCNT_OFF = HEADER_LEN_FULL + CTRL_HDR_LEN + 1;

    for (auto& [id, sub] : subscribers_) {
        sub.packetCounters[slot]++;
        writeU32LE(out + PCNT_OFF, sub.packetCounters[slot]);
        finalizeCrc(out, PKT_LEN);
        sendto(sock_, out, PKT_LEN, 0, (struct sockaddr*)&sub.addr, sizeof(sub.addr));
        packetsInWindow_++;
    }
}

void DsuServer::cleanupSubscribers() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(subMutex_);
    bool wasEmpty = subscribers_.empty();
    for (auto it = subscribers_.begin(); it != subscribers_.end(); ) {
        if (now - it->second.lastRequest >= SUB_TIMEOUT)
            it = subscribers_.erase(it);
        else
            ++it;
    }
    if (!wasEmpty && subscribers_.empty())
        fprintf(stderr, "dsu: last subscriber timed out\n");
}

} // namespace sc2
