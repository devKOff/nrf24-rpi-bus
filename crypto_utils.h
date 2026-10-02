#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>
#include <string>
#include <openssl/evp.h>
#include <openssl/err.h>

namespace pi_nrf24_link {

class CryptoEngine {
public:
    CryptoEngine();
    ~CryptoEngine();

    CryptoEngine(const CryptoEngine&) = delete;
    CryptoEngine& operator=(const CryptoEngine&) = delete;
    CryptoEngine(CryptoEngine&&) = delete;
    CryptoEngine& operator=(CryptoEngine&&) = delete;

    bool initialize(const uint8_t* key, size_t key_len, const uint8_t* iv, size_t iv_len);
    void cleanup();

    std::optional<std::array<uint8_t, 16>> encrypt(const uint8_t* plaintext, size_t plaintext_len);
    std::optional<std::array<uint8_t, 16>> decrypt(const uint8_t* ciphertext, size_t ciphertext_len);

    static uint32_t crc32(const uint8_t* data, size_t length);
    static uint32_t crc32(const std::array<uint8_t, 16>& data);
    static bool verify_crc32(const uint8_t* data, size_t length, uint32_t expected_crc);

private:
    EVP_CIPHER_CTX* encrypt_ctx_;
    EVP_CIPHER_CTX* decrypt_ctx_;
    bool initialized_;
};

class CRC32 {
public:
    static constexpr uint32_t POLYNOMIAL = 0xEDB88320;
    static constexpr uint32_t INITIAL_VALUE = 0xFFFFFFFF;
    static constexpr uint32_t FINAL_XOR = 0xFFFFFFFF;

    CRC32();
    void update(const uint8_t* data, size_t length);
    void update(uint8_t byte);
    uint32_t finalize() const;
    void reset();

private:
    static uint32_t table_[256];
    static bool table_initialized_;
    uint32_t crc_;
    static void init_table();
};

} // namespace pi_nrf24_link

#endif // CRYPTO_UTILS_H