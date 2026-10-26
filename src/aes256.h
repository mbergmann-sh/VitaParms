// VitaParms – AES-256 (nur Verschlüsselungsrichtung, für CTR-Modus)
// Kompakte Referenzimplementierung nach FIPS-197, ohne Fremdbibliothek.
// Wird für passwortgeschützte ZIP-Archive (WinZip-AES, AE-2) verwendet.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

class Aes256
{
public:
    explicit Aes256(const uint8_t key[32]);
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) const;

private:
    uint8_t m_roundKeys[240];   // 15 Rundenschlüssel à 16 Byte
};
