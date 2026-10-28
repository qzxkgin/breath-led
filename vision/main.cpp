#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

constexpr uint8_t kHeader = 0xA5;
constexpr size_t kPacketSize = 6;
constexpr double kPi = 3.14159265358979323846;

#pragma pack(push, 1)
struct RgbPacket {
    uint8_t header;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint16_t crc;
};
#pragma pack(pop)

static_assert(sizeof(RgbPacket) == kPacketSize, "RgbPacket must be 6 bytes");

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000u) != 0)
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021u);
            else
                crc = static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

RgbPacket make_packet(uint8_t r, uint8_t g, uint8_t b) {
    RgbPacket pkt{kHeader, r, g, b, 0};
    pkt.crc = crc16_ccitt(reinterpret_cast<const uint8_t *>(&pkt), kPacketSize - 2);
    return pkt;
}

constexpr uint64_t kTargetId = 0ull;

uint64_t fnv1a(const std::string &text) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string host_signature() {
#ifdef _WIN32
    char name[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD name_size = sizeof(name);
    GetComputerNameA(name, &name_size);
    DWORD serial = 0;
    GetVolumeInformationA("C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    return std::string(name) + "|" + std::to_string(serial);
#else
    std::ifstream file("/etc/machine-id");
    std::string id;
    std::getline(file, id);
    char name[256] = {0};
    gethostname(name, sizeof(name) - 1);
    return id + "|" + name;
#endif
}

uint64_t host_fingerprint() {
    return fnv1a(host_signature());
}

class SerialPort {
public:
    ~SerialPort() { close(); }

    bool open(const std::string &device, int baud) {
#ifdef _WIN32
        std::string path = device;
        if (path.rfind("\\\\.\\", 0) != 0)
            path = "\\\\.\\" + path;
        handle_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            std::snprintf(error_, sizeof(error_), "CreateFileA failed, error %lu", GetLastError());
            return false;
        }
        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (GetCommState(handle_, &dcb) == 0) {
            std::snprintf(error_, sizeof(error_), "GetCommState failed, error %lu", GetLastError());
            return false;
        }
        dcb.BaudRate = static_cast<DWORD>(baud);
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        if (SetCommState(handle_, &dcb) == 0) {
            std::snprintf(error_, sizeof(error_), "SetCommState failed, error %lu", GetLastError());
            return false;
        }
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = 1;
        timeouts.WriteTotalTimeoutConstant = 100;
        SetCommTimeouts(handle_, &timeouts);
        return true;
#else
        fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY);
        if (fd_ < 0) {
            std::snprintf(error_, sizeof(error_), "open(%s) failed: %s", device.c_str(),
                          std::strerror(errno));
            return false;
        }
        termios tty{};
        if (tcgetattr(fd_, &tty) != 0) {
            std::snprintf(error_, sizeof(error_), "tcgetattr failed: %s", std::strerror(errno));
            return false;
        }
        cfmakeraw(&tty);
        tty.c_cflag |= CLOCAL | CREAD;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;
        const speed_t speed = baud_constant(baud);
        cfsetispeed(&tty, speed);
        cfsetospeed(&tty, speed);
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;
        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            std::snprintf(error_, sizeof(error_), "tcsetattr failed: %s", std::strerror(errno));
            return false;
        }
        return true;
#endif
    }

    bool write(const uint8_t *data, size_t len) {
#ifdef _WIN32
        DWORD written = 0;
        if (WriteFile(handle_, data, static_cast<DWORD>(len), &written, nullptr) == 0 ||
            written != len) {
            std::snprintf(error_, sizeof(error_), "WriteFile failed, error %lu", GetLastError());
            return false;
        }
        return true;
#else
        size_t offset = 0;
        while (offset < len) {
            const ssize_t n = ::write(fd_, data + offset, len - offset);
            if (n <= 0) {
                std::snprintf(error_, sizeof(error_), "write failed: %s", std::strerror(errno));
                return false;
            }
            offset += static_cast<size_t>(n);
        }
        return true;
#endif
    }

    void close() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

    const char *last_error() const { return error_; }

private:
#ifndef _WIN32
    static speed_t baud_constant(int baud) {
        switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: return B115200;
        }
    }
#endif

#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
    char error_[160] = {0};
};

struct Options {
    std::string port;
    int baud = 115200;
    int base_r = 0;
    int base_g = 160;
    int base_b = 255;
    double freq = 0.5;
    double fps = 50.0;
    double gamma = 1.0;
    double duration = 0.0;
    bool log = false;
    bool hex = false;
    bool preview = false;
    bool hold = false;
    bool fingerprint = false;
};

void print_usage(const char *argv0) {
    std::printf(
        "usage: %s --port <device> [options]\n"
        "       %s --hex | --log | --preview [options]\n"
        "\n"
        "  --port <dev>     serial device, /dev/ttyUSB0 or COM3\n"
        "  --baud <n>       bitrate, default 115200\n"
        "  --color R,G,B    base color, default 0,160,255\n"
        "  --freq <hz>      breathing frequency, default 0.5\n"
        "  --fps <n>        frames per second, default 50\n"
        "  --gamma <g>      brightness curve exponent, default 1.0\n"
        "  --duration <s>   run time in seconds, 0 means forever\n"
        "  --log            print hex and rgb every frame, no serial\n"
        "  --hex            print hex only, one frame per line\n"
        "  --preview        draw an ANSI color bar in place\n"
        "  --hold           send the base color without breathing\n"
        "  --fingerprint    print this machine id and exit\n"
        "  --help\n",
        argv0, argv0);
}

bool parse_options(int argc, char **argv, Options &opt) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string inline_value;
        if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-') {
            const size_t eq = arg.find('=');
            if (eq != std::string::npos) {
                inline_value = arg.substr(eq + 1);
                arg.resize(eq);
            }
        }
        auto next = [&](const char *name) -> const char * {
            if (!inline_value.empty())
                return inline_value.c_str();
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--port") {
            const char *v = next("--port");
            if (v == nullptr) return false;
            opt.port = v;
        } else if (arg == "--baud") {
            const char *v = next("--baud");
            if (v == nullptr) return false;
            opt.baud = std::atoi(v);
        } else if (arg == "--color") {
            const char *v = next("--color");
            if (v == nullptr) return false;
            if (std::sscanf(v, "%d,%d,%d", &opt.base_r, &opt.base_g, &opt.base_b) != 3) {
                std::fprintf(stderr, "bad --color, expected R,G,B\n");
                return false;
            }
        } else if (arg == "--freq") {
            const char *v = next("--freq");
            if (v == nullptr) return false;
            opt.freq = std::atof(v);
        } else if (arg == "--fps") {
            const char *v = next("--fps");
            if (v == nullptr) return false;
            opt.fps = std::atof(v);
        } else if (arg == "--gamma") {
            const char *v = next("--gamma");
            if (v == nullptr) return false;
            opt.gamma = std::atof(v);
        } else if (arg == "--duration") {
            const char *v = next("--duration");
            if (v == nullptr) return false;
            opt.duration = std::atof(v);
        } else if (arg == "--log") {
            opt.log = true;
        } else if (arg == "--hex") {
            opt.hex = true;
        } else if (arg == "--preview") {
            opt.preview = true;
        } else if (arg == "--hold") {
            opt.hold = true;
        } else if (arg == "--fingerprint") {
            opt.fingerprint = true;
        } else if (arg == "--help" || arg == "-h") {
            return false;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return false;
        }
    }
    if (opt.port.empty() && !opt.log && !opt.hex && !opt.preview && !opt.fingerprint)
        return false;
    if (opt.base_r < 0 || opt.base_r > 255 || opt.base_g < 0 || opt.base_g > 255 ||
        opt.base_b < 0 || opt.base_b > 255) {
        std::fprintf(stderr, "--color values must be 0..255\n");
        return false;
    }
    if (opt.fps <= 0.0 || opt.fps > 1000.0) {
        std::fprintf(stderr, "--fps must be in (0, 1000]\n");
        return false;
    }
    if (opt.freq <= 0.0 || opt.freq > 10.0) {
        std::fprintf(stderr, "--freq must be in (0, 10]\n");
        return false;
    }
    return true;
}

#ifdef _WIN32
void enable_ansi() {
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode) != 0)
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#endif

}  // namespace

int main(int argc, char **argv) {
    Options opt;
    if (!parse_options(argc, argv, opt)) {
        print_usage(argv[0]);
        return 1;
    }

    if (opt.fingerprint) {
        std::printf("0x%016llx\n", static_cast<unsigned long long>(host_fingerprint()));
        return 0;
    }
    if (kTargetId != 0ull && host_fingerprint() != kTargetId) {
        std::fprintf(stderr, "this build is not licensed for this machine\n");
        return 4;
    }

    SerialPort serial;
    bool serial_open = false;
    if (!opt.port.empty()) {
        if (!serial.open(opt.port, opt.baud)) {
            std::fprintf(stderr, "cannot open %s: %s\n", opt.port.c_str(), serial.last_error());
            return 2;
        }
        serial_open = true;
        std::fprintf(stderr, "opened %s at %d 8N1\n", opt.port.c_str(), opt.baud);
    }

#ifdef _WIN32
    enable_ansi();
#endif
    std::signal(SIGINT, on_signal);
#ifndef _WIN32
    std::signal(SIGTERM, on_signal);
#endif

    const long long period_us = static_cast<long long>(std::llround(1e6 / opt.fps));
    const auto start = std::chrono::steady_clock::now();
    long long frame = 0;
    long long frame_us = 0;

    while (!g_stop) {
        const double t = static_cast<double>(frame_us) / 1e6;
        if (opt.duration > 0.0 && t >= opt.duration)
            break;

        const double envelope =
            opt.hold ? 1.0 : 0.5 * (1.0 - std::cos(2.0 * kPi * opt.freq * t));
        const double level = opt.gamma == 1.0 ? envelope : std::pow(envelope, opt.gamma);
        const uint8_t r = static_cast<uint8_t>(std::lround(opt.base_r * level));
        const uint8_t g = static_cast<uint8_t>(std::lround(opt.base_g * level));
        const uint8_t b = static_cast<uint8_t>(std::lround(opt.base_b * level));

        const RgbPacket pkt = make_packet(r, g, b);
        uint8_t bytes[kPacketSize];
        std::memcpy(bytes, &pkt, kPacketSize);

        if (serial_open && !serial.write(bytes, kPacketSize)) {
            std::fprintf(stderr, "write failed: %s\n", serial.last_error());
            serial.close();
            return 3;
        }

        if (opt.hex) {
            std::printf("%02x %02x %02x %02x %02x %02x\n", bytes[0], bytes[1], bytes[2], bytes[3],
                        bytes[4], bytes[5]);
            std::fflush(stdout);
        } else if (opt.log) {
            std::printf("[%7.3fs] %02x %02x %02x %02x %02x %02x  rgb(%3u,%3u,%3u)\n", t, bytes[0],
                        bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], static_cast<unsigned>(r),
                        static_cast<unsigned>(g), static_cast<unsigned>(b));
            std::fflush(stdout);
        }

        if (opt.preview) {
            std::printf("\r\x1b[48;2;%u;%u;%um%*s\x1b[0m %6.2fs rgb(%3u,%3u,%3u)", r, g, b, 40, "",
                        t, r, g, b);
            std::fflush(stdout);
        }

        ++frame;
        frame_us += period_us;
        std::this_thread::sleep_until(start + std::chrono::microseconds(frame_us));
    }

    if (serial_open) {
        const RgbPacket off = make_packet(0, 0, 0);
        uint8_t bytes[kPacketSize];
        std::memcpy(bytes, &off, kPacketSize);
        serial.write(bytes, kPacketSize);
        serial.close();
    }
    if (opt.preview)
        std::printf("\n");
    std::fprintf(stderr, "stopped after %lld frames\n", frame);
    return 0;
}
