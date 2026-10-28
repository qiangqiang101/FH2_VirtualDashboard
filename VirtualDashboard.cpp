// VirtualDashboard - live telemetry dashboard served over the local network.
// Reads the car the HUD is showing, on the game's own thread (an instruction
// hook inside the HUD update), and hosts a phone-friendly page
// (gauges + raw values) on http://<pc-ip>:<port>/ . Standalone mod.
#include "fh2mod.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "dashboard_html.h"
#include "favicon_png.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

constexpr char kVersion[] = "1.1.0";
constexpr char kSettingKey[] = "DashEnabled";

// Inside the HUD speedometer update: rax = car-state object, rdi = HUD
// element, [rdi+0xE8] = the car it shows. Runs on the game thread.
constexpr std::uintptr_t kHudHookRva = 0x783E67;
constexpr std::uintptr_t kHudCarOffset = 0xE8;
constexpr std::uintptr_t kNameOffset = 0x350;    // car -> media name string
constexpr std::uintptr_t kLightsOffset = 0x51A2; // car -> headlights on (byte)

// Offsets inside the car-state object (found by probing).
constexpr std::uintptr_t kRpm = 0x148;       // engine speed, rad/s
constexpr std::uintptr_t kRedline = 0x1C8;   // where the red zone starts, rad/s
constexpr std::uintptr_t kLimiter = 0x1CC;   // hard rev limiter, rad/s
constexpr std::uintptr_t kClutch = 0xA8C;    // 0..1 (was mislabelled as brake)
constexpr std::uintptr_t kBrake = 0xDFC;     // 0..1
constexpr std::uintptr_t kHandbrake = 0xE08; // 0..1
constexpr std::uintptr_t kPowerRaw = 0x16C;  // smooth power-like value, ~42.5 per kW (fitted to the in-game page)
constexpr std::uintptr_t kBoost = 0x170;     // turbo boost
constexpr std::uintptr_t kStateOffset = 0x4C08; // car -> car-state object
constexpr std::uintptr_t kWorldRoot = 0x34ABA60; // exe global -> world -> clock/weather
constexpr std::uintptr_t kDbgOffsets[] = {0x150, 0x158, 0x160, 0x168, 0x170, 0x178, 0x180, 0x188, 0x16C, 0x18C, 0x194, 0x198, 0x19C, 0x5C0, 0xA80, 0xA88, 0xAA4, 0xC4C, 0xF48, 0xF4C, 0xFA0, 0xDD8, 0xDDC};
constexpr std::uintptr_t kGear = 0xBA4;      // int
constexpr std::uintptr_t kThrottle = 0xDB4;  // 0..1
constexpr std::uintptr_t kSteer = 0xE00;     // -1..1
constexpr std::uintptr_t kSpeed = 0xE28;     // m/s
constexpr std::uintptr_t kPos = 0xEF0;       // x, y(height), z

struct Telemetry {
    bool valid = false;
    char car[64] = {};
    float rpm = 0, redline = 0, limiter = 0, brake = 0, throttle = 0, steer = 0, speed = 0;
    float pos[3] = {};
    int gear = 0;
    int lights = 0;
    int ind = 0;
    float clutch = 0, handbrake = 0, torque = 0;
    float boost = 0, time = -1, weather[3] = {};
    float dbg[sizeof(kDbgOffsets) / sizeof(kDbgOffsets[0])] = {};
};

const FH2ModApi* g_api = nullptr;
FH2Mod* g_mod = nullptr;
std::uint8_t* g_exe = nullptr;
std::filesystem::path g_ini_path;
std::atomic<bool> g_enabled{true};
std::atomic<int> g_port{8080};

std::mutex g_json_mutex;
std::string g_json = "{\"valid\":false}";

void logf(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_api->log(g_mod, buf);
}

// The media name is a string object at car+0x350: short names (under 16
// characters) are stored inline, longer ones through a heap pointer.
// Never fails: an unreadable name just comes out empty.
void read_name(const std::uint8_t* c, char* out) {
    out[0] = 0;
    __try {
        const std::uint64_t cap =
            *reinterpret_cast<const std::uint64_t*>(c + kNameOffset + 0x18);
        const char* name = cap >= 16
            ? *reinterpret_cast<const char* const*>(c + kNameOffset)
            : reinterpret_cast<const char*>(c + kNameOffset);
        int n = 0;
        while (n < 63 && name[n] >= 0x20 && name[n] < 0x7f) {
            out[n] = name[n];
            ++n;
        }
        out[n] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
    }
}

bool read_sample(const void* car, const void* state, Telemetry* t) {
    __try {
        const auto* c = static_cast<const std::uint8_t*>(car);
        const auto* s = static_cast<const std::uint8_t*>(state);
        if (!c || !s) return false;
        t->rpm = *reinterpret_cast<const float*>(s + kRpm);
        t->redline = *reinterpret_cast<const float*>(s + kRedline);
        t->limiter = *reinterpret_cast<const float*>(s + kLimiter);
        t->brake = *reinterpret_cast<const float*>(s + kBrake);
        t->clutch = *reinterpret_cast<const float*>(s + kClutch);
        t->handbrake = *reinterpret_cast<const float*>(s + kHandbrake);
        {
            const float om = *reinterpret_cast<const float*>(s + kRpm);
            float kw = *reinterpret_cast<const float*>(s + kPowerRaw) / 42.5f;
            if (!(kw > 0) || kw > 2000 || !(om > 50)) kw = 0;
            t->torque = kw * 1000.0f / om;
        }
        t->boost = *reinterpret_cast<const float*>(s + kBoost);
        for (std::size_t i = 0; i < sizeof(kDbgOffsets) / sizeof(kDbgOffsets[0]); ++i)
            t->dbg[i] = *reinterpret_cast<const float*>(s + kDbgOffsets[i]);
        t->throttle = *reinterpret_cast<const float*>(s + kThrottle);
        t->steer = *reinterpret_cast<const float*>(s + kSteer);
        t->speed = *reinterpret_cast<const float*>(s + kSpeed);
        t->gear = *reinterpret_cast<const int*>(s + kGear);
        for (int i = 0; i < 3; ++i)
            t->pos[i] = *reinterpret_cast<const float*>(s + kPos + 4 * i);
        t->lights = c[kLightsOffset] ? 1 : 0;
        read_name(c, t->car);
        t->valid = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float clean(float v) {
    return (v == v && v > -1e9f && v < 1e9f) ? v : 0.0f;
}

// Nitro level (0-100) from FnFNitro.dll if it is loaded, else -1. Looked up lazily: the mods load in any order.
int read_nitro() {
    using NitroFn = int (*)();
    static NitroFn fn = nullptr;
    if (!fn) {
        if (HMODULE m = GetModuleHandleW(L"FnFNitro.dll"))
            fn = reinterpret_cast<NitroFn>(GetProcAddress(m, "FnFNitro_GetNitroPercent"));
    }
    return fn ? fn() : -1;
}

void publish(const Telemetry& t) {
    char buf[1536];
    if (!t.valid) {
        std::snprintf(buf, sizeof(buf), "{\"valid\":false}");
    } else {
        const float omega = clean(t.rpm);
        int n = std::snprintf(
            buf, sizeof(buf),
            "{\"valid\":true,\"car\":\"%s\",\"speed\":%.2f,\"rpm\":%.1f,"
            "\"redline\":%.1f,\"limiter\":%.1f,\"gear\":%d,\"throttle\":%.3f,\"brake\":%.3f,"
            "\"clutch\":%.3f,\"handbrake\":%.3f,\"torque\":%.1f,\"boost\":%.2f,\"power\":%.1f,"
            "\"steer\":%.3f,\"lights\":%d,\"ind\":%d,\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,"
            "\"time\":%.0f,\"w0\":%.3f,\"w1\":%.3f,\"w2\":%.3f,\"nitro\":%d,\"dbg\":{",
            t.car, clean(t.speed), omega * 9.5492966f, clean(t.redline) * 9.5492966f,
            clean(t.limiter) * 9.5492966f, t.gear, clean(t.throttle), clean(t.brake),
            clean(t.clutch), clean(t.handbrake), clean(t.torque), clean(t.boost),
            clean(t.torque) * omega / 1000.0f, clean(t.steer), t.lights, t.ind, clean(t.pos[0]),
            clean(t.pos[1]), clean(t.pos[2]), clean(t.time), clean(t.weather[0]),
            clean(t.weather[1]), clean(t.weather[2]), read_nitro());
        for (std::size_t i = 0; i < sizeof(kDbgOffsets) / sizeof(kDbgOffsets[0]) && n > 0 &&
                                n < static_cast<int>(sizeof(buf)) - 40; ++i)
            n += std::snprintf(buf + n, sizeof(buf) - n, "%s\"%zX\":%.3f", i ? "," : "",
                               static_cast<std::size_t>(kDbgOffsets[i]), clean(t.dbg[i]));
        std::snprintf(buf + n, sizeof(buf) - n, "}}");
    }
    std::lock_guard<std::mutex> lock(g_json_mutex);
    g_json = buf;
}

std::atomic<std::uint64_t> g_last_sample{0};
std::atomic<bool> g_logged_car{false};
char g_logged_name[64] = {};
std::atomic<const void*> g_car{nullptr};
std::atomic<int> g_ind{0};
std::atomic<float> g_steer{0.0f};
char g_car_name[64] = {};

// Game clock and weather, read from memory only (same chain the WeatherMenu tool uses).
void fill_world(Telemetry& t) {
    t.time = -1;
    __try {
        const auto rd = [](std::uintptr_t p) { return *reinterpret_cast<const std::uintptr_t*>(p); };
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_exe);
        std::uintptr_t ctx = rd(base + kWorldRoot);
        ctx = rd(rd(ctx + 0x48) + 0x98);
        const std::uintptr_t clock = rd(ctx + 0x480), weather = rd(ctx + 0x488);
        if (clock) {
            const float v = *reinterpret_cast<const float*>(clock + 0x10);
            if (v >= 0 && v <= 86401) t.time = v;
        }
        if (weather) {
            t.weather[0] = *reinterpret_cast<const float*>(weather + 0x8);
            t.weather[1] = *reinterpret_cast<const float*>(weather + 0x5C);
            t.weather[2] = *reinterpret_cast<const float*>(weather + 0x18);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void hud_hook(FH2Context* ctx, void*) {
    if (!g_enabled.load()) return;
    const std::uint64_t now = GetTickCount64();
    if (now - g_last_sample.load() < 30) return;
    const void* hud = reinterpret_cast<const void*>(ctx->rdi);
    const void* car = nullptr;
    __try {
        car = *reinterpret_cast<void* const*>(
            static_cast<const std::uint8_t*>(hud) + kHudCarOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    Telemetry t;
    if (!read_sample(car, reinterpret_cast<const void*>(ctx->rax), &t)) return;
    g_last_sample.store(now);
    std::memcpy(g_car_name, t.car, sizeof(g_car_name));
    g_car.store(car);
    g_steer.store(clean(t.steer));
    t.ind = g_ind.load();
    fill_world(t);
    publish(t);
    if (std::strcmp(g_logged_name, t.car) != 0) {
        std::memcpy(g_logged_name, t.car, sizeof(g_logged_name));
        logf("Reading telemetry from %s (red zone %.0f rpm, limiter %.0f rpm).", t.car,
             clean(t.redline) * 9.5492966f, clean(t.limiter) * 9.5492966f);
    }
}

// Turn signals are emulated from the D-pad (same gestures as Extra Options):
// hold left/right 1 s to toggle, tap up for hazards, steering cancels.
// XInput is polled here, never on the game thread.
struct XPad {
    WORD buttons;
    BYTE lt, rt;
    SHORT lx, ly, rx, ry;
};
using XGetState = DWORD(WINAPI*)(DWORD, void*);

void update_indicators(std::uint64_t now, WORD btn, float steer) {
    static std::uint64_t down[3] = {};
    static bool fired[3] = {};
    static bool turned = false;
    const WORD masks[3] = {0x4, 0x8, 0x1};
    int ind = g_ind.load();
    for (int i = 0; i < 3; ++i) {
        const bool pressed = (btn & masks[i]) != 0;
        if (pressed && !down[i]) { down[i] = now; fired[i] = false; }
        if (pressed && i < 2 && !fired[i] && now - down[i] >= 1000) {
            fired[i] = true;
            ind = ind == i + 1 ? 0 : i + 1;
            turned = false;
        }
        if (!pressed && down[i]) {
            if (i == 2 && now - down[i] < 500) { ind = ind == 3 ? 0 : 3; turned = false; }
            down[i] = 0;
        }
    }
    if (ind == 1 || ind == 2) {
        const float s = ind == 1 ? -steer : steer;
        if (s > 0.35f) turned = true;
        else if (turned && s < 0.1f) { ind = 0; turned = false; }
    }
    if (ind != g_ind.load()) {
        g_ind.store(ind);
        logf("Indicators now %d (1=left 2=right 3=hazard).", ind);
    }
}

// Keeps the dashboard alive in menus (the HUD hook doesn't fire there) by
// re-reading the last known car from memory only. Never calls game code.
void watchdog() {
    XGetState get_state = nullptr;
    if (HMODULE x = LoadLibraryA("xinput1_4.dll"))
        get_state = reinterpret_cast<XGetState>(GetProcAddress(x, "XInputGetState"));
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const std::uint64_t now = GetTickCount64();
        WORD btn = 0;
        if (get_state) {
            XPad pad = {};
            struct { DWORD packet; XPad gp; } st = {};
            if (get_state(0, &st) == 0) { pad = st.gp; btn = pad.buttons; }
        }
        update_indicators(now, btn, g_steer.load());
        if (now - g_last_sample.load() <= 100) continue;
        const void* car = g_car.load();
        Telemetry t;
        bool ok = false;
        if (car) {
            __try {
                const void* state = *reinterpret_cast<void* const*>(
                    static_cast<const std::uint8_t*>(car) + kStateOffset);
                ok = read_sample(car, state, &t) && t.car[0] &&
                     std::strcmp(t.car, g_car_name) == 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ok = false;
            }
        }
        if (ok) {
            t.valid = true;
            fill_world(t);
            t.ind = g_ind.load();
            publish(t);
        } else {
            g_car.store(nullptr);
            publish(Telemetry{});
        }
    }
}

void install_hook() {
    void* base = g_api->wait_for_code(30000);
    if (!base) {
        logf("Timed out waiting for game code.");
        return;
    }
    g_exe = static_cast<std::uint8_t*>(base);
    if (g_api->api_version < 7 || !g_api->hook_instruction) {
        logf("This platform has no instruction hooks; update the platform.");
        return;
    }
    FH2Hook* h = g_api->hook_instruction(g_mod, "VirtualDashboard: HUD speedometer update",
                                         g_exe + kHudHookRva, hud_hook, nullptr);
    if (!h || !g_api->enable_hooks(g_mod, h)) {
        logf("Could not install the HUD hook.");
        return;
    }
    logf("Telemetry hook active.");
}

void send_all(SOCKET s, const char* data, int len) {
    while (len > 0) {
        const int n = send(s, data, len, 0);
        if (n <= 0) return;
        data += n;
        len -= n;
    }
}

void reply(SOCKET s, const char* status, const char* type,
           const std::string& body) {
    char head[256];
    const int n = std::snprintf(
        head, sizeof(head),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        status, type, body.size());
    send_all(s, head, n);
    send_all(s, body.data(), static_cast<int>(body.size()));
}

void handle_client(SOCKET s) {
    DWORD timeout = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout),
               sizeof(timeout));
    char req[1024];
    const int n = recv(s, req, sizeof(req) - 1, 0);
    if (n > 0) {
        req[n] = 0;
        if (!g_enabled.load()) {
            reply(s, "503 Service Unavailable", "text/plain",
                  "VirtualDashboard is turned off in the Mods menu.");
        } else if (std::strncmp(req, "GET /favicon", 12) == 0) {
            reply(s, "200 OK", "image/png",
                  std::string(reinterpret_cast<const char*>(kFaviconPng),
                              kFaviconPngSize));
        } else if (std::strncmp(req, "GET /api", 8) == 0) {
            std::string body;
            {
                std::lock_guard<std::mutex> lock(g_json_mutex);
                body = g_json;
            }
            reply(s, "200 OK", "application/json", body);
        } else if (std::strncmp(req, "GET / ", 6) == 0 ||
                   std::strncmp(req, "GET /?", 6) == 0 ||
                   std::strncmp(req, "GET /index", 10) == 0) {
            reply(s, "200 OK", "text/html; charset=utf-8", kDashboardHtml);
        } else {
            reply(s, "404 Not Found", "text/plain", "Not found");
        }
    }
    shutdown(s, SD_BOTH);
    closesocket(s);
}

void server() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        logf("WSAStartup failed.");
        return;
    }
    const int port = g_port.load();
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port));
    BOOL reuse = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse),
               sizeof(reuse));
    if (ls == INVALID_SOCKET ||
        bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(ls, 16) != 0) {
        logf("Could not listen on port %d (error %d). Change Port in "
             "VirtualDashboard.ini.",
             port, WSAGetLastError());
        return;
    }
    char host[256] = {};
    gethostname(host, sizeof(host));
    logf("Dashboard on port %d. Open http://<this PC's IP>:%d/ on your phone "
         "(PC name: %s).",
         port, port, host);
    for (;;) {
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        std::thread(handle_client, c).detach();
    }
}

void on_enabled_changed(void*, const char* key, int value) {
    if (!key || std::strcmp(key, kSettingKey) != 0) return;
    g_enabled.store(value != 0);
    WritePrivateProfileStringW(L"VirtualDashboard", L"Enabled",
                               value ? L"1" : L"0", g_ini_path.c_str());
}

}  // namespace

FH2MOD_EXPORT int fh2mod_init(const FH2ModApi* api) {
    if (!api || api->api_version < 3 || !api->register_mod || !api->log) {
        return 1;
    }
    FH2ModInfo info{};
    info.size = sizeof(info);
    info.name = "VirtualDashboard";
    info.version = kVersion;
    info.platform_version = "2.7.1";
    info.check_updates = 0;
    info.description = "Live telemetry dashboard for your phone over Wi-Fi.";
    g_api = api;
    g_mod = api->register_mod(&info);
    if (!g_mod) {
        return 1;
    }

    std::wstring path(32768, L'\0');
    const DWORD len = GetModuleFileNameW(
        reinterpret_cast<HMODULE>(&__ImageBase), path.data(),
        static_cast<DWORD>(path.size()));
    path.resize(len);
    g_ini_path = std::filesystem::path(path).parent_path() / L"VirtualDashboard.ini";
    const bool enabled =
        GetPrivateProfileIntW(L"VirtualDashboard", L"Enabled", 1, g_ini_path.c_str()) != 0;
    int port = static_cast<int>(
        GetPrivateProfileIntW(L"VirtualDashboard", L"Port", 8080, g_ini_path.c_str()));
    if (port < 1024 || port > 65535) port = 8080;
    g_enabled.store(enabled);
    g_port.store(port);
    WritePrivateProfileStringW(L"VirtualDashboard", L"Enabled", enabled ? L"1" : L"0",
                               g_ini_path.c_str());
    WritePrivateProfileStringW(L"VirtualDashboard", L"Port",
                               std::to_wstring(port).c_str(), g_ini_path.c_str());

    if (api->api_version >= 4 && api->add_setting) {
        FH2Setting setting{};
        setting.size = sizeof(setting);
        setting.key = kSettingKey;
        setting.label = "Phone dashboard";
        setting.description =
            "Serve live gauges on your local network (port set in VirtualDashboard.ini).";
        setting.value = enabled ? 1 : 0;
        setting.needs_restart = 0;
        setting.on_change = on_enabled_changed;
        if (!api->add_setting(g_mod, &setting)) {
            logf("Could not add the option to the Mods menu.");
        }
    }

    std::thread(install_hook).detach();
    std::thread(watchdog).detach();
    std::thread(server).detach();
    logf("%s registered", kVersion);
    return 0;
}
