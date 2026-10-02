#include "crypto_utils.h"
#include <algorithm>
#include <cstring>

namespace pi_nrf24_link {

uint32_t CRC32::table_[256] = {0};
bool CRC32::table_initialized_ = false;

void CRC32::init_table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ POLYNOMIAL;
            else
                crc >>= 1;
        }
        table_[i] = crc;
    }
    table_initialized_ = true;
}

CRC32::CRC32() : crc_(INITIAL_VALUE) {
    if (!table_initialized_) {
        init_table();
    }
}

void CRC32::update(const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        crc_ = table_[(crc_ ^ data[i]) & 0xFF] ^ (crc_ >> 8);
    }
}

void CRC32::update(uint8_t byte) {
    crc_ = table_[(crc_ ^ byte) & 0xFF] ^ (crc_ >> 8);
}

uint32_t CRC32::finalize() const {
    return crc_ ^ FINAL_XOR;
}

void CRC32::reset() {
    crc_ = INITIAL_VALUE;
}

uint32_t CryptoEngine::crc32(const uint8_t* data, size_t length) {
    CRC32 crc;
    crc.update(data, length);
    return crc.finalize();
}

uint32_t CryptoEngine::crc32(const std::array<uint8_t, 16>& data) {
    return crc32(data.data(), data.size());
}

bool CryptoEngine::verify_crc32(const uint8_t* data, size_t length, uint32_t expected_crc) {
    return crc32(data, length) == expected_crc;
}

CryptoEngine::CryptoEngine() : encrypt_ctx_(nullptr), decrypt_ctx_(nullptr), initialized_(false) {
    encrypt_ctx_ = EVP_CIPHER_CTX_new();
    decrypt_ctx_ = EVP_CIPHER_CTX_new();
}

CryptoEngine::~CryptoEngine() {
    cleanup();
    if (encrypt_ctx_) EVP_CIPHER_CTX_free(encrypt_ctx_);
    if (decrypt_ctx_) EVP_CIPHER_CTX_free(decrypt_ctx_);
}

bool CryptoEngine::initialize(const uint8_t* key, size_t key_len, const uint8_t* iv, size_t iv_len) {
    if (!encrypt_ctx_ || !decrypt_ctx_ || key_len != 16 || iv_len != 16) {
        return false;
    }

    if (EVP_EncryptInit_ex(encrypt_ctx_, EVP_aes_128_cbc(), nullptr, key, iv) != 1) {
        return false;
    }
    EVP_CIPHER_CTX_set_padding(encrypt_ctx_, 0);

    if (EVP_DecryptInit_ex(decrypt_ctx_, EVP_aes_128_cbc(), nullptr, key, iv) != 1) {
        return false;
    }
    EVP_CIPHER_CTX_set_padding(decrypt_ctx_, 0);

    initialized_ = true;
    return true;
}

void CryptoEngine::cleanup() {
    if (encrypt_ctx_) {
        EVP_CIPHER_CTX_reset(encrypt_ctx_);
    }
    if (decrypt_ctx_) {
        EVP_CIPHER_CTX_reset(decrypt_ctx_);
    }
    initialized_ = false;
}

std::optional<std::array<uint8_t, 16>> CryptoEngine::encrypt(const uint8_t* plaintext, size_t plaintext_len) {
    if (!initialized_ || plaintext_len != 16) {
        return std::nullopt;
    }

    std::array<uint8_t, 16> ciphertext{};
    int len = 0;
    int ciphertext_len = 0;

    if (EVP_EncryptInit_ex(encrypt_ctx_, nullptr, nullptr, nullptr, nullptr) != 1) {
        return std::nullopt;
    }

    if (EVP_EncryptUpdate(encrypt_ctx_, ciphertext.data(), &len, plaintext, plaintext_len) != 1) {
        return std::nullopt;
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(encrypt_ctx_, ciphertext.data() + ciphertext_len, &len) != 1) {
        return std::nullopt;
    }
    ciphertext_len += len;

    if (ciphertext_len != 16) {
        return std::nullopt;
    }

    return ciphertext;
}

std::optional<std::array<uint8_t, 16>> CryptoEngine::decrypt(const uint8_t* ciphertext, size_t ciphertext_len) {
    if (!initialized_ || ciphertext_len != 16) {
        return std::nullopt;
    }

    std::array<uint8_t, 16> plaintext{};
    int len = 0;
    int plaintext_len = 0;

    if (EVP_DecryptInit_ex(decrypt_ctx_, nullptr, nullptr, nullptr, nullptr) != 1) {
        return std::nullopt;
    }

    if (EVP_DecryptUpdate(decrypt_ctx_, plaintext.data(), &len, ciphertext, ciphertext_len) != 1) {
        return std::nullopt;
    }
    plaintext_len = len;

    if (EVP_DecryptFinal_ex(decrypt_ctx_, plaintext.data() + plaintext_len, &len) != 1) {
        return std::nullopt;
    }
    plaintext_len += len;

    if (plaintext_len != 16) {
        return std::nullopt;
    }

    return plaintext;
}

} // namespace pi_nrf24_link