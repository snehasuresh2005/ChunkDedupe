#include "hasher.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <array>
#include <stdexcept>
#include <cstring>

namespace chunkdedupe {

namespace internal {

// Standard SHA-256 implementation (FIPS 180-2 compliant)
class SHA256 {
public:
    SHA256() {
        Reset();
    }

    void Reset() {
        m_state[0] = 0x6a09e667;
        m_state[1] = 0xbb67ae85;
        m_state[2] = 0x3c6ef372;
        m_state[3] = 0xa54ff53a;
        m_state[4] = 0x510e527f;
        m_state[5] = 0x9b05688c;
        m_state[6] = 0x1f83d9ab;
        m_state[7] = 0x5be0cd19;
        m_count = 0;
        m_bufferPos = 0;
    }

    void Update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            m_buffer[m_bufferPos++] = data[i];
            m_count += 8;
            if (m_bufferPos == 64) {
                Transform(m_buffer.data());
                m_bufferPos = 0;
            }
        }
    }

    std::string FinalHex() {
        uint8_t bits[8];
        for (int i = 0; i < 8; ++i) {
            bits[i] = static_cast<uint8_t>((m_count >> (56 - i * 8)) & 0xFF);
        }

        uint8_t pad = 0x80;
        Update(&pad, 1);
        pad = 0x00;
        while (m_bufferPos != 56) {
            Update(&pad, 1);
        }
        Update(bits, 8);

        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < 8; ++i) {
            ss << std::setw(8) << m_state[i];
        }
        return ss.str();
    }

private:
    static inline uint32_t RoTr(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    static inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (~x & z);
    }

    static inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    static inline uint32_t Sig0(uint32_t x) {
        return RoTr(x, 2) ^ RoTr(x, 13) ^ RoTr(x, 22);
    }

    static inline uint32_t Sig1(uint32_t x) {
        return RoTr(x, 6) ^ RoTr(x, 11) ^ RoTr(x, 25);
    }

    static inline uint32_t sig0(uint32_t x) {
        return RoTr(x, 7) ^ RoTr(x, 18) ^ (x >> 3);
    }

    static inline uint32_t sig1(uint32_t x) {
        return RoTr(x, 17) ^ RoTr(x, 19) ^ (x >> 10);
    }

    void Transform(const uint8_t* chunk) {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef4a3f7, 0xc67178f2
        };

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) |
                   (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(chunk[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = sig1(w[i - 2]) + w[i - 7] + sig0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];
        uint32_t f = m_state[5];
        uint32_t g = m_state[6];
        uint32_t h = m_state[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + Sig1(e) + Ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = Sig0(a) + Maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    uint32_t m_state[8];
    uint64_t m_count;
    std::array<uint8_t, 64> m_buffer;
    size_t m_bufferPos;
};

} // namespace internal

std::string ComputeSHA256(const uint8_t* data, size_t length) {
    internal::SHA256 hasher;
    hasher.Update(data, length);
    return hasher.FinalHex();
}

std::string ComputeSHA256(const std::vector<uint8_t>& data) {
    return ComputeSHA256(data.data(), data.size());
}

std::string ComputeSHA256(const std::string& str) {
    return ComputeSHA256(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

std::string ComputeSHA256File(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for SHA-256 calculation: " + filepath);
    }

    internal::SHA256 hasher;
    std::vector<char> buffer(65536);
    while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
        hasher.Update(reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<size_t>(file.gcount()));
    }
    return hasher.FinalHex();
}

} // namespace chunkdedupe
