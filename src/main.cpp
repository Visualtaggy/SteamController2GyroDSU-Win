#include "hiddev.h"
#include "dsu.h"
#include "triton.h"
#include <cstdio>
#include <cstring>
#include <csignal>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <chrono>
#include <string>

using namespace std::chrono_literals;
using namespace sc2;

// ── Globals ───────────────────────────────────────────────────────────────────

static std::atomic<bool> gShutdown{false};
static DsuServer* gServer = nullptr;

static void signalHandler(int sig) {
    fprintf(stderr, "\nsc2gyrodsu: signal %d — shutting down\n", sig);
    gShutdown = true;
}

// ── Per-slot reader ───────────────────────────────────────────────────────────

struct SlotReader {
    std::unique_ptr<HidSlot> slot;
    std::thread              thread;
    std::atomic<bool>        done{false};
};

static void slotReaderThread(HidSlot* slot, DsuServer* server, std::atomic<bool>& done) {
    constexpr int STALE_MAX   = 100;
    constexpr int ERROR_MAX   = 5;
    const auto    SILENCE_MAX = 2000ms;

    int  staleCnt  = 0;
    int  errorCnt  = 0;
    auto lastSample = std::chrono::steady_clock::now();
    uint32_t lastTs = 0;

    fprintf(stderr, "triton-%d: reader started (serial=%s)\n",
            slot->dsuSlot, slot->serial.c_str());

    while (!gShutdown && !done) {
        ControllerState state;
        bool ok = slot->readOne(state);

        if (ok) {
            errorCnt  = 0;
            lastSample = std::chrono::steady_clock::now();

            if (state.imu.timestamp_us == lastTs) {
                if (++staleCnt >= STALE_MAX) {
                    fprintf(stderr, "triton-%d: IMU frozen — Steam disabled IMU; disconnecting\n",
                            slot->dsuSlot);
                    break;
                }
            } else {
                staleCnt = 0;
                lastTs   = state.imu.timestamp_us;
                server->pushState(state);
            }
        } else {
            errorCnt = 0; // read returning no data is normal (non-blocking)
            if (std::chrono::steady_clock::now() - lastSample >= SILENCE_MAX) {
                fprintf(stderr, "triton-%d: no data for 2s — disconnecting\n",
                        slot->dsuSlot);
                break;
            }
        }
    }

    fprintf(stderr, "triton-%d: reader exiting\n", slot->dsuSlot);
    server->setDisconnected(slot->dsuSlot);
    done = true;
}

// ── Main ──────────────────────────────────────────────────────────────────────

static void printHelp(const char* name) {
    fprintf(stderr,
        "sc2gyrodsu — Cemuhook DSU server for up to 4 Steam Controllers 2\n"
        "\n"
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --probe          List detected Valve HID devices and exit\n"
        "  --port <PORT>    DSU UDP port (default: 26760)\n"
        "  --expose         Bind to 0.0.0.0 (default: 127.0.0.1 only)\n"
        "  --help           Show this message\n"
        "\n"
        "Point your emulator (Eden, Ryujinx, Cemu) at 127.0.0.1:26760.\n"
        "Slot 0 = first controller, Slot 1 = second, etc.\n",
        name);
}

int main(int argc, char* argv[]) {
    uint16_t    port   = 26760;
    bool        expose = false;
    bool        probe  = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--probe") == 0) {
            probe = true;
        } else if (strcmp(argv[i], "--expose") == 0) {
            expose = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printHelp(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        }
    }

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    HidManager manager;

    if (probe) {
        manager.probe();
        return 0;
    }

    DsuServer server(port, expose);
    gServer = &server;
    server.start();

    fprintf(stderr,
        "sc2gyrodsu running. Waiting for emulator to subscribe...\n"
        "Press Ctrl-C to quit.\n");

    bool slotUsed[4] = {};
    std::vector<std::unique_ptr<SlotReader>> readers;

    while (!gShutdown) {
        // Reap finished readers and free their slots
        for (auto it = readers.begin(); it != readers.end(); ) {
            auto& r = **it;
            if (r.done) {
                int s = r.slot->dsuSlot;
                fprintf(stderr, "scanner: freeing slot %d\n", s);
                slotUsed[s] = false;
                if (r.thread.joinable()) r.thread.join();
                it = readers.erase(it);
            } else {
                ++it;
            }
        }

        // Only scan when subscribers exist or always? 
        // Scan always so controllers are ready when emulator connects.
        std::vector<std::unique_ptr<HidSlot>> newSlots;
        // Build temporary active list for dedup
        struct TmpSlot { HidSlot* ptr; };
        // We need to pass the active list to scan — adapt the API
        // by collecting current serials
        std::vector<std::unique_ptr<HidSlot>> activeForScan;
        // Don't actually move — just scan for NEW devices
        // Re-use the manager.scan signature: it needs the active list for dedup
        // We pass a dummy list built from current readers' serials

        // Simpler: just rebuild from readers
        struct FakeSlot { std::string serial; int dsuSlot; };
        // Pass as plain list of serials to avoid; rebuild HidSlot wrappers
        // Actually let's just call hid_enumerate directly here for simplicity:
        {
            struct hid_device_info* devs = hid_enumerate(VID_VALVE, 0);
            struct hid_device_info* cur  = devs;
            while (cur && !gShutdown) {
                if (isTritonPid(cur->product_id) && cur->usage_page >= 0xFF00) {
                    std::string serial = cur->serial_number
                        ? std::string(reinterpret_cast<const char*>(cur->serial_number),
                                      wcslen(cur->serial_number))
                        : std::string(cur->path);

                    // Check if already active
                    bool already = false;
                    for (auto& r : readers) {
                        if (r->slot->serial == serial) { already = true; break; }
                    }
                    if (already) { cur = cur->next; continue; }

                    // Find free slot
                    int slot = -1;
                    for (int i = 0; i < 4; ++i) {
                        if (!slotUsed[i]) { slot = i; break; }
                    }
                    if (slot < 0) {
                        fprintf(stderr, "scanner: all 4 slots full\n");
                        cur = cur->next; continue;
                    }

                    hid_device* dev = hid_open_path(cur->path);
                    if (!dev) {
                        fprintf(stderr, "scanner: cannot open %s\n", cur->path);
                        cur = cur->next; continue;
                    }

                    fprintf(stderr,
                        "scanner: PID %04X (%s) serial=%s → slot %d\n",
                        cur->product_id, pidLabel(cur->product_id),
                        serial.c_str(), slot);

                    slotUsed[slot] = true;
                    auto reader = std::make_unique<SlotReader>();
                    reader->slot = std::make_unique<HidSlot>(dev, slot, cur->product_id, serial);
                    HidSlot* slotPtr = reader->slot.get();
                    std::atomic<bool>& doneRef = reader->done;
                    reader->thread = std::thread([slotPtr, &server, &doneRef](){
                        slotReaderThread(slotPtr, &server, const_cast<std::atomic<bool>&>(doneRef));
                    });
                    readers.push_back(std::move(reader));
                }
                cur = cur->next;
            }
            hid_free_enumeration(devs);
        }

        std::this_thread::sleep_for(500ms);
    }

    fprintf(stderr, "sc2gyrodsu: shutting down\n");
    server.stop();

    // Join all reader threads
    for (auto& r : readers) {
        r->done = true;
        if (r->thread.joinable()) r->thread.join();
    }

    return 0;
}
