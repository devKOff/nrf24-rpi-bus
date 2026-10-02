#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <cstddef>

namespace pi_nrf24_link {

constexpr int CE_PIN = 25;
constexpr int IRQ_PIN = 24;
constexpr int SPI_CS_PIN = 8;
constexpr int SPI_SPEED_HZ = 8000000;
constexpr const char* SPI_DEVICE = "/dev/spidev0.0";

constexpr uint8_t RF_CHANNEL = 104;
constexpr uint8_t RF_DATA_RATE = 250;
constexpr uint8_t RF_PA_LEVEL = 3;
constexpr uint8_t RF_RETRY_DELAY = 15;
constexpr uint8_t RF_RETRY_COUNT = 15;
constexpr uint8_t ADDRESS_WIDTH = 5;
constexpr uint64_t PIPE_ADDRESS = 0x32344e5246ULL;

constexpr uint8_t AES_KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};
constexpr uint8_t AES_IV[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

constexpr size_t MAX_PAYLOAD_SIZE = 32;
constexpr size_t ENCRYPTED_PAYLOAD_SIZE = 19;
constexpr size_t CRC_SIZE = 4;
constexpr size_t SEQ_SIZE = 4;
constexpr size_t TIMESTAMP_SIZE = 4;
constexpr size_t CMD_SIZE = 1;

constexpr uint32_t ANTI_REPLAY_WINDOW = 1000;
constexpr uint32_t TIMESTAMP_TOLERANCE_SEC = 300;

enum class PayloadType : uint8_t {
    SENSOR_DATA = 0x01,
    CONTROL = 0x02,
    HEARTBEAT = 0x03,
    ACK = 0x04
};

struct __attribute__((packed)) RadioPacket {
    uint8_t cmd;
    uint32_t sequence;
    uint32_t timestamp;
    uint8_t encrypted_data[ENCRYPTED_PAYLOAD_SIZE];
    uint32_t crc32;
};

static_assert(sizeof(RadioPacket) == MAX_PAYLOAD_SIZE, "RadioPacket must be exactly 32 bytes");

} // namespace pi_nrf24_link

#endif // CONFIG_H