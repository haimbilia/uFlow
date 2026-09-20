#pragma once

#include <stdint.h>

#ifdef __cplusplus
#include <array>
using Sha1Hash = std::array<uint8_t, 20>;
#else
typedef struct { uint8_t data[20]; } Sha1Hash;
#endif

// Signature type constants for certificates, tickets, and TMDs
#define CERT_SIG_RSA4096 0x00010000
#define CERT_SIG_RSA2048 0x00010001
#define CERT_SIG_ECDSA   0x00010002

// Certificate Public Key type constants
#define CERT_KEY_RSA4096 0
#define CERT_KEY_RSA2048 1
#define CERT_KEY_ECC     2

#ifdef __cplusplus
extern "C" {
#endif

// Content record in TMD
typedef struct __attribute__((packed)) TitleContentRecord {
    uint32_t contentId;
    uint16_t index;
    uint16_t type;
    uint64_t size;
    Sha1Hash hash;
} TitleContentRecord;

// Entry in /shared1/content.map (maps 8-char hex name to SHA-1 hash)
typedef struct __attribute__((packed)) ContentMapEntry {
    char name[8];
    Sha1Hash hash;
} ContentMapEntry;


// Complete TMD structure mapping
typedef struct __attribute__((packed)) TitleTmd {
    uint32_t signatureType;      // 0x000
    uint8_t  signature[256];     // 0x004
    uint8_t  padding1[60];       // 0x104
    char     issuer[64];         // 0x140
    uint8_t  version;            // 0x180
    uint8_t  caCrlVersion;       // 0x181
    uint8_t  signerCrlVersion;   // 0x182
    uint8_t  padding2;           // 0x183
    uint64_t sysVersion;         // 0x184
    uint64_t titleId;            // 0x18C
    uint32_t titleType;          // 0x194
    uint16_t groupId;            // 0x198
    uint8_t  reserved1[58];      // 0x19A
    uint16_t fakeBootIndex;      // 0x1D4 (used for brute forcing)
    uint16_t reserved2;          // 0x1D6
    uint32_t accessRights;       // 0x1D8
    uint16_t titleVersion;       // 0x1DC
    uint16_t numContents;        // 0x1DE
    uint16_t bootIndex;          // 0x1E0
    uint16_t padding3;           // 0x1E2
    TitleContentRecord contents[]; // 0x1E4
} TitleTmd;

// Complete Ticket structure mapping
typedef struct __attribute__((packed)) TitleTicket {
    uint32_t signatureType;      // 0x000
    uint8_t  signature[256];     // 0x004
    uint8_t  padding1[60];       // 0x104
    char     issuer[64];         // 0x140
    uint8_t  ecdhData[60];       // 0x180
    uint8_t  formatVersion;      // 0x1BC
    uint16_t reserved1;          // 0x1BD
    uint8_t  titleKey[16];       // 0x1BF
    uint8_t  unknown1;           // 0x1CF
    uint64_t ticketId;           // 0x1D0
    uint32_t consoleId;          // 0x1D8
    uint64_t titleId;            // 0x1DC
    uint16_t unknown2;           // 0x1E4
    uint16_t ticketTitleVersion; // 0x1E6
    uint32_t permittedTitlesMask;// 0x1E8
    uint32_t permitMask;         // 0x1EC
    uint8_t  titleExportAllowed; // 0x1F0
    uint8_t  commonKeyIndex;     // 0x1F1
    uint8_t  unknown3[6];        // 0x1F2
    uint16_t padding2;           // 0x1F8 (used for brute forcing)
    uint8_t  unknown4[40];       // 0x1FA
    uint8_t  contentAccess[64];  // 0x222
    uint16_t padding3;           // 0x262
    uint8_t  limits[64];         // 0x264 (v1 limit records: 8 * 8 bytes)
} TitleTicket;

// RSA-2048 Certificate (used by CP00000004, XS00000003, MS00000002)
typedef struct __attribute__((packed)) CertRsa2048 {
    uint32_t signatureType;      // 0x000 (0x00010001 = CERT_SIG_RSA2048)
    uint8_t  signature[256];     // 0x004
    uint8_t  padding1[60];       // 0x104
    char     issuer[64];         // 0x140
    uint32_t keyType;            // 0x180 (1 = CERT_KEY_RSA2048)
    char     name[64];           // 0x184
    uint32_t keyId;              // 0x1C4
    uint8_t  modulus[256];       // 0x1C8 (RSA-2048 public key modulus)
    uint32_t exponent;           // 0x2C8 (e.g. 0x00010001)
    uint8_t  padding2[52];       // 0x2CC
} CertRsa2048;

// RSA-4096 signed Certificate with RSA-2048 key (used by CA00000001)
typedef struct __attribute__((packed)) CertRsa4096Rsa2048 {
    uint32_t signatureType;      // 0x000 (0x00010000 = CERT_SIG_RSA4096)
    uint8_t  signature[512];     // 0x004
    uint8_t  padding1[60];       // 0x204
    char     issuer[64];         // 0x240
    uint32_t keyType;            // 0x280 (1 = CERT_KEY_RSA2048)
    char     name[64];           // 0x284
    uint32_t keyId;              // 0x2C4
    uint8_t  modulus[256];       // 0x2C8 (RSA-2048 public key modulus)
    uint32_t exponent;           // 0x3C8 (e.g. 0x00010001)
    uint8_t  padding2[52];       // 0x3CC
} CertRsa4096Rsa2048;

// RSA-4096 signed Certificate with RSA-4096 key (used by Root)
typedef struct __attribute__((packed)) CertRsa4096Rsa4096 {
    uint32_t signatureType;      // 0x000 (0x00010000 = CERT_SIG_RSA4096)
    uint8_t  signature[512];     // 0x004
    uint8_t  padding1[60];       // 0x204
    char     issuer[64];         // 0x240
    uint32_t keyType;            // 0x280 (0 = CERT_KEY_RSA4096)
    char     name[64];           // 0x284
    uint32_t keyId;              // 0x2C4
    uint8_t  modulus[512];       // 0x2C8 (RSA-4096 public key modulus)
    uint32_t exponent;           // 0x4C8 (e.g. 0x00010001)
    uint8_t  padding2[52];       // 0x4CC
} CertRsa4096Rsa4096;

#ifdef __cplusplus
static_assert(sizeof(TitleContentRecord) == 36, "TitleContentRecord size mismatch");
static_assert(sizeof(ContentMapEntry) == 28, "ContentMapEntry size mismatch");
static_assert(sizeof(TitleTmd) == 0x1E4, "TitleTmd size mismatch");
static_assert(sizeof(TitleTicket) == 0x2A4, "TitleTicket size mismatch");
static_assert(sizeof(CertRsa2048) == 0x300, "CertRsa2048 size mismatch");
static_assert(sizeof(CertRsa4096Rsa2048) == 0x400, "CertRsa4096Rsa2048 size mismatch");
static_assert(sizeof(CertRsa4096Rsa4096) == 0x500, "CertRsa4096Rsa4096 size mismatch");
}
#endif
