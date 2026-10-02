#include <iostream>
#include <string>
#include <cstring>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>
#include <array>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <cstdint>
#include "config.h"
#include "crypto_utils.h"
#include "radio_handler.h"

namespace pi_nrf24_link {

enum class AppMode {
    TX,
    RX,
    INVALID
};

struct AppConfig {
    AppMode mode = AppMode::INVALID;
    std::string payload_data;
    uint32_t interval_ms = 1000;
    uint32_t count = 0;
    bool verbose = false;
};

std::atomic<bool> g_shutdown_requested(false);
std::mutex g_shutdown_mutex;
std::condition_variable g_shutdown_cv;

void signal_handler(int sig) {
    std::lock_guard<std::mutex> lock(g_shutdown_mutex);
    g_shutdown_requested.store(true);
    g_shutdown_cv.notify_all();
}

void setup_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

AppMode parse_mode(const std::string& mode_str) {
    if (mode_str == "tx") return AppMode::TX;
    if (mode_str == "rx") return AppMode::RX;
    return AppMode::INVALID;
}

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " --mode <tx|rx> [options]\n"
              << "Options:\n"
              << "  --mode <tx|rx>       Operation mode (required)\n"
              << "  --data <string>      Payload data for TX mode (max 19 bytes)\n"
              << "  --interval <ms>      TX interval in milliseconds (default: 1000)\n"
              << "  --count <n>          Number of packets to send (0 = infinite)\n"
              << "  --verbose            Enable verbose output\n"
              << "  --help               Show this help\n";
}

bool parse_args(int argc, char* argv[], AppConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            config.mode = parse_mode(argv[++i]);
        } else if (arg == "--data" && i + 1 < argc) {
            config.payload_data = argv[++i];
        } else if (arg == "--interval" && i + 1 < argc) {
            config.interval_ms = std::stoul(argv[++i]);
        } else if (arg == "--count" && i + 1 < argc) {
            config.count = std::stoul(argv[++i]);
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--help") {
            return false;
        }
    }
    return config.mode != AppMode::INVALID;
}

std::string bytes_to_hex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

void print_packet(const RadioPacket& packet, const std::string& prefix) {
    std::cout << prefix << " cmd=0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(packet.cmd)
              << " seq=" << std::dec << packet.sequence
              << " ts=" << packet.timestamp
              << " data=" << bytes_to_hex(packet.encrypted_data, ENCRYPTED_PAYLOAD_SIZE)
              << " crc=0x" << std::hex << packet.crc32 << std::dec << std::endl;
}

class TxApplication {
public:
    TxApplication(const AppConfig& config) : config_(config), sequence_(0) {
        crypto_.initialize(AES_KEY, 16, AES_IV, 16);
    }

    ~TxApplication() {
        crypto_.cleanup();
    }

    int run(RadioHandler& radio) {
        if (config_.payload_data.empty()) {
            std::cerr << "Error: TX mode requires --data parameter\n";
            return 1;
        }

        if (config_.payload_data.size() > ENCRYPTED_PAYLOAD_SIZE) {
            std::cerr << "Error: Payload data exceeds maximum size of " << ENCRYPTED_PAYLOAD_SIZE << " bytes\n";
            return 1;
        }

        std::cout << "Starting TX mode, interval=" << config_.interval_ms << "ms";
        if (config_.count > 0) {
            std::cout << ", count=" << config_.count;
        }
        std::cout << std::endl;

        uint32_t sent = 0;
        while (!g_shutdown_requested.load() && (config_.count == 0 || sent < config_.count)) {
            RadioPacket packet = build_packet();

            if (config_.verbose) {
                print_packet(packet, "TX:");
            }

            bool ack = radio.transmit(packet);
            if (ack) {
                if (config_.verbose) {
                    std::cout << "TX: ACK received\n";
                }
                ++sent;
            } else {
                std::cerr << "TX: No ACK received\n";
            }

            if (config_.interval_ms > 0 && !g_shutdown_requested.load()) {
                std::unique_lock<std::mutex> lock(g_shutdown_mutex);
                g_shutdown_cv.wait_for(lock, std::chrono::milliseconds(config_.interval_ms),
                    []{ return g_shutdown_requested.load(); });
            }
        }

        std::cout << "TX: Sent " << sent << " packets\n";
        return 0;
    }

private:
    RadioPacket build_packet() {
        RadioPacket packet{};
        packet.cmd = static_cast<uint8_t>(PayloadType::SENSOR_DATA);
        packet.sequence = ++sequence_;
        packet.timestamp = static_cast<uint32_t>(time(nullptr));

        std::memset(packet.encrypted_data, 0, ENCRYPTED_PAYLOAD_SIZE);
        std::memcpy(packet.encrypted_data, config_.payload_data.data(),
                    std::min(config_.payload_data.size(), ENCRYPTED_PAYLOAD_SIZE));

        std::array<uint8_t, 16> plaintext{};
        std::memcpy(plaintext.data(), packet.encrypted_data, 16);

        auto ciphertext = crypto_.encrypt(plaintext.data(), 16);
        if (ciphertext) {
            std::memcpy(packet.encrypted_data, ciphertext->data(), 16);
        }

        uint32_t crc_data[8];
        std::memcpy(crc_data, &packet, 28);
        packet.crc32 = CryptoEngine::crc32(reinterpret_cast<const uint8_t*>(crc_data), 28);

        return packet;
    }

    const AppConfig& config_;
    CryptoEngine crypto_;
    std::atomic<uint32_t> sequence_;
};

class RxApplication {
public:
    RxApplication(const AppConfig& config) : config_(config), last_sequence_(0), last_timestamp_(0) {
        crypto_.initialize(AES_KEY, 16, AES_IV, 16);
    }

    ~RxApplication() {
        crypto_.cleanup();
    }

    int run(RadioHandler& radio) {
        std::cout << "Starting RX mode\n";

        radio.start_listening();

        std::mutex rx_mutex;
        std::condition_variable rx_cv;
        std::queue<RadioPacket> local_queue;
        bool rx_callback_registered = false;

        radio.start_listening();

        auto rx_callback = [&](const RadioPacket& packet) {
            std::lock_guard<std::mutex> lock(rx_mutex);
            local_queue.push(packet);
            rx_cv.notify_one();
        };

        auto tx_callback = [&](bool success) {
            if (config_.verbose) {
                std::cout << "TX callback: " << (success ? "ACK" : "NACK") << std::endl;
            }
        };

        radio.initialize(rx_callback, tx_callback);
        radio.start_listening();

        while (!g_shutdown_requested.load()) {
            RadioPacket packet;
            {
                std::unique_lock<std::mutex> lock(rx_mutex);
                if (rx_cv.wait_for(lock, std::chrono::milliseconds(100),
                    [&]{ return !local_queue.empty() || g_shutdown_requested.load(); })) {
                    if (!local_queue.empty()) {
                        packet = local_queue.front();
                        local_queue.pop();
                    } else {
                        continue;
                    }
                } else {
                    continue;
                }
            }

            if (process_packet(packet)) {
                if (config_.verbose) {
                    print_packet(packet, "RX:");
                }
                print_decrypted(packet);
            }
        }

        radio.stop_listening();
        std::cout << "RX: Shutdown complete\n";
        return 0;
    }

private:
    bool process_packet(const RadioPacket& packet) {
        uint32_t crc_data[8];
        std::memcpy(crc_data, &packet, 28);
        uint32_t computed_crc = CryptoEngine::crc32(reinterpret_cast<const uint8_t*>(crc_data), 28);

        if (computed_crc != packet.crc32) {
            if (config_.verbose) {
                std::cerr << "RX: CRC mismatch (expected 0x" << std::hex << packet.crc32
                          << ", got 0x" << computed_crc << ")" << std::dec << std::endl;
            }
            return false;
        }

        if (packet.sequence <= last_sequence_) {
            if (config_.verbose) {
                std::cerr << "RX: Replay attack detected (seq=" << packet.sequence
                          << ", last=" << last_sequence_ << ")" << std::endl;
            }
            return false;
        }

        uint32_t current_time = static_cast<uint32_t>(time(nullptr));
        if (current_time > packet.timestamp + TIMESTAMP_TOLERANCE_SEC ||
            packet.timestamp > current_time + TIMESTAMP_TOLERANCE_SEC) {
            if (config_.verbose) {
                std::cerr << "RX: Timestamp out of tolerance (pkt=" << packet.timestamp
                          << ", now=" << current_time << ")" << std::endl;
            }
            return false;
        }

        last_sequence_ = packet.sequence;
        last_timestamp_ = packet.timestamp;
        return true;
    }

    void print_decrypted(const RadioPacket& packet) {
        std::array<uint8_t, 16> ciphertext{};
        std::memcpy(ciphertext.data(), packet.encrypted_data, 16);

        auto plaintext = crypto_.decrypt(ciphertext.data(), 16);
        if (plaintext) {
            std::string decrypted(reinterpret_cast<char*>(plaintext->data()), 16);
            size_t end = decrypted.find('\0');
            if (end != std::string::npos) {
                decrypted = decrypted.substr(0, end);
            }
            std::cout << "RX: Decrypted payload: " << decrypted << std::endl;
        }
    }

    const AppConfig& config_;
    CryptoEngine crypto_;
    std::atomic<uint32_t> last_sequence_;
    std::atomic<uint32_t> last_timestamp_;
};

} // namespace pi_nrf24_link

int main(int argc, char* argv[]) {
    using namespace pi_nrf24_link;

    AppConfig config;
    if (!parse_args(argc, argv, config)) {
        print_usage(argv[0]);
        return 1;
    }

    setup_signal_handlers();

    RadioHandler radio;
    auto rx_cb = [](const RadioPacket&) {};
    auto tx_cb = [](bool) {};

    if (!radio.initialize(rx_cb, tx_cb)) {
        std::cerr << "Failed to initialize radio\n";
        return 1;
    }

    int result = 1;
    if (config.mode == AppMode::TX) {
        TxApplication app(config);
        result = app.run(radio);
    } else if (config.mode == AppMode::RX) {
        RxApplication app(config);
        result = app.run(radio);
    }

    radio.shutdown();
    return result;
}