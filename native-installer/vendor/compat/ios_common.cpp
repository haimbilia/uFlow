#include "ios_common.h"
#include "installer.h"
#include "content_map.h"
#include "FSAUtils.h"
#include "log.h"
#include "EndianUtils.h"
#include <mbedtls/sha1.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sstream>
#include <iomanip>

extern "C" {
#include "wad_tools/tools.h"
}

#include "wad.h"

extern FSAClientHandle fsaClient;

MemIOS::~MemIOS() {
    if (tmd) free(tmd);
    if (ticket) free(ticket);
    if (contents) {
        for (uint32_t i = 0; i < numContents; i++) {
            if (contents[i].data) free(contents[i].data);
        }
        free(contents);
    }
}

void Patcher_Log(const std::string& msg) {
    WUPI_putstr(msg.c_str());
}

std::string ToHexString(uint32_t val, int width) {
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(width) << std::hex << val;
    return ss.str();
}

std::unique_ptr<MemIOS> ReadBaseIOS(uint32_t baseIos) {
    auto outIos = std::unique_ptr<MemIOS>(new MemIOS());
    // Read Ticket
    std::string path = "/vol/slccmpt01/ticket/00000001/" + ToHexString(baseIos, 8) + ".tik";
    uint8_t* tikBuf = nullptr;
    if (!ReadFileToBuffer(path, &tikBuf, &outIos->ticketSize)) {
        Patcher_Log("Failed to read ticket for base IOS " + std::to_string(baseIos) + "\n");
        return nullptr;
    }
    outIos->ticket = (TitleTicket*)tikBuf;
    if (outIos->ticketSize < sizeof(TitleTicket) || outIos->ticket->commonKeyIndex != 2 ||
        (FromBE64(outIos->ticket->titleId) & 0xffffffffULL) != baseIos) return nullptr;

    // Read TMD
    path = "/vol/slccmpt01/title/00000001/" + ToHexString(baseIos, 8) + "/content/title.tmd";
    uint8_t* tmdBuf = nullptr;
    if (!ReadFileToBuffer(path, &tmdBuf, &outIos->tmdSize)) {
        Patcher_Log("Failed to read TMD for base IOS " + std::to_string(baseIos) + "\n");
        return nullptr;
    }
    outIos->tmd = (TitleTmd*)tmdBuf;
    if (outIos->tmdSize < sizeof(TitleTmd) || (FromBE64(outIos->tmd->titleId) & 0xffffffffULL) != baseIos) return nullptr;

    // Parse TMD contents
    uint16_t numContents = FromBE16(outIos->tmd->numContents);
    if (!numContents || numContents > 128 || outIos->tmdSize < sizeof(TitleTmd) + numContents*sizeof(TitleContentRecord)) return nullptr;
    outIos->numContents = numContents;
    outIos->maxContents = numContents + 10; // Extra space for modules
    outIos->contents = (MemContent*)calloc(outIos->maxContents, sizeof(MemContent));
    if (!outIos->contents) { outIos->numContents=0; return nullptr; }

    // Reallocate TMD buffer to have space for maxContents
    uint32_t maxTmdSize = outIos->tmdSize + (10 * sizeof(TitleContentRecord));
    uint8_t* newTmd = (uint8_t*)memalign(0x40, (maxTmdSize + 0x3F) & ~0x3F);
    if (!newTmd) {
        Patcher_Log("Failed to reallocate TMD buffer\n");
        return nullptr;
    }
    memset(newTmd, 0, (maxTmdSize + 0x3F) & ~0x3F);
    memcpy(newTmd, outIos->tmd, outIos->tmdSize);
    free(outIos->tmd);
    outIos->tmd = (TitleTmd*)newTmd;

    for (uint16_t i = 0; i < numContents; i++) {
        uint32_t cid = FromBE32(outIos->tmd->contents[i].contentId);
        outIos->contents[i].cid = cid;

        uint16_t cType = FromBE16(outIos->tmd->contents[i].type);
        if ((cType & 0x8000) != 0) {
            int32_t sharedIndex = FindSharedContentIndex(outIos->tmd->contents[i].hash.data());
            if (sharedIndex < 0) {
                Patcher_Log("Failed to find shared content for cid " + ToHexString(cid, 8) + "\n");
                return nullptr;
            }
            path = "/vol/slccmpt01/shared1/" + ToHexString(sharedIndex, 8) + ".app";
        } else {
            path = "/vol/slccmpt01/title/00000001/" + ToHexString(baseIos, 8) + "/content/" + ToHexString(cid, 8) + ".app";
        }

        if (!ReadFileToBuffer(path, &outIos->contents[i].data, &outIos->contents[i].size)) {
            Patcher_Log("Failed to read content " + ToHexString(cid, 8) + ".app\n");
            return nullptr;
        }
        uint8_t digest[20];
        sha(outIos->contents[i].data,outIos->contents[i].size,digest);
        if (outIos->contents[i].size != FromBE64(outIos->tmd->contents[i].size) ||
            memcmp(digest,outIos->tmd->contents[i].hash.data(),20)) return nullptr;
    }

    return outIos;
}

static void BruteTmd(TitleTmd* tmd, uint32_t size) {
    uint8_t hash[20];
    for (uint32_t fill = 0; fill < 65535; fill++) {
        tmd->fakeBootIndex = fill;
        sha((uint8_t*)tmd + offsetof(TitleTmd, issuer), size - offsetof(TitleTmd, issuer), hash);
        if (hash[0] == 0) return;
    }
}

static void BruteTicket(TitleTicket* ticket, uint32_t size) {
    uint8_t hash[20];
    for (uint32_t fill = 0; fill < 65535; fill++) {
        ticket->padding2 = fill;
        sha((uint8_t*)ticket + offsetof(TitleTicket, issuer), size - offsetof(TitleTicket, issuer), hash);
        if (hash[0] == 0) return;
    }
}

bool WritePatchedIOS(uint32_t titleIdLow, MemIOS& ios) {
    if (titleIdLow < 248 || titleIdLow > 251) return false;
    if (ios.ticketSize >= sizeof(TitleTicket)) {
        memset(ios.ticket->signature, 0, sizeof(ios.ticket->signature));

        uint8_t common_key[16];
        if (!GetCommonKeyFromOTP(ios.ticket->commonKeyIndex, common_key)) {
            Patcher_Log("Failed to read common key from OTP\n");
            return false;
        }

        uint8_t titleKey[16];
        uint8_t iv[16] = {0};
        memcpy(iv, &ios.ticket->titleId, 8);
        aes_cbc_dec(common_key, iv, ios.ticket->titleKey, 16, titleKey);

        ios.ticket->titleId = ToBE64(((uint64_t)1 << 32) | titleIdLow);

        memset(iv, 0, 16);
        memcpy(iv, &ios.ticket->titleId, 8);
        aes_cbc_enc(common_key, iv, titleKey, 16, ios.ticket->titleKey);

        BruteTicket(ios.ticket, ios.ticketSize);
    } else {
        Patcher_Log("Ticket size too small\n");
        return false;
    }

    if (ios.tmdSize >= offsetof(TitleTmd, contents) && ios.numContents > 0) {
        memset(ios.tmd->signature, 0, sizeof(ios.tmd->signature));
        ios.tmd->titleId = ToBE64(((uint64_t)1 << 32) | titleIdLow);
        ios.tmd->titleVersion = ToBE16(0xFFFF);
    } else {
        Patcher_Log("TMD size too small\n");
        return false;
    }

    TitleContentRecord* records = ios.tmd->contents;
    for (uint32_t i = 0; i < ios.numContents; i++) {
        // Keep cIOS contents private: never update the console's shared IOS files/map.
        records[i].type = ToBE16(FromBE16(records[i].type) & ~0x8000);
        uint32_t cid = FromBE32(records[i].contentId);
        for (uint32_t j = 0; j < ios.numContents; j++) {
            if (ios.contents[j].cid == cid) {
                sha(ios.contents[j].data, ios.contents[j].size, records[i].hash.data());
                break;
            }
        }
    }

    BruteTmd(ios.tmd, ios.tmdSize);

    CINS_Content* cins_contents = (CINS_Content*)malloc(sizeof(CINS_Content) * ios.numContents);
    if (!cins_contents) return false;

    for (uint32_t i = 0; i < ios.numContents; i++) {
        cins_contents[i].data = ios.contents[i].data;
        cins_contents[i].length = ios.contents[i].size;
    }

    uint64_t fullTitleId = 0x0000000100000000ULL | titleIdLow;
    int32_t ret = CINS_Install(fullTitleId, ios.ticket, ios.ticketSize, ios.tmd, ios.tmdSize, cins_contents, ios.numContents);

    free(cins_contents);

    return ret >= 0;
}
