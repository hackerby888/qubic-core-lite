#pragma once

#define _GNU_SOURCE
#include "contract_core/contract_def.h"

#include <K12/kangaroo_twelve_xkcp.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/userfaultfd.h>
#include <mutex>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define SYSTEM_PAGE_SIZE sysconf(_SC_PAGESIZE)

static size_t alignToPageSize(size_t address)
{
    size_t page_size = SYSTEM_PAGE_SIZE;
    return (address + page_size - 1) & ~(page_size - 1);
}

class UserFaultFD {
public:
    UserFaultFD() {
        fd = syscall(SYS_userfaultfd, O_NONBLOCK);
        if (fd < 0) throw std::runtime_error("userfaultfd failed");

        uffdio_api api{ .api = UFFD_API };
        if (ioctl(fd, UFFDIO_API, &api) == -1)
            throw std::runtime_error("UFFDIO_API failed");
    }

    ~UserFaultFD() { if (fd >= 0) close(fd); }

    int get() const { return fd; }

private:
    int fd;
};

class K12Engine
{
protected:
    struct Intermediate
    {
        unsigned char intermediate[maxCapacityInBytes];
    };

    // map from chunk index to intermediate state
    std::vector<Intermediate> intermediateMap;
    // map from chunk index to isChanged flag
    std::vector<bool> isChunkChangedMap;
    unsigned int maxChunks;
    unsigned char *_state;
    size_t _stateSize;

    unsigned char *_lastOutput;
    size_t _lastOutputSize;

    int _KangarooTwelve_Update(XKCP::KangarooTwelve_Instance *ktInstance, const unsigned char *input, size_t inputByteLen)
    {
        if (ktInstance->phase != XKCP::ABSORBING)
            return 1;

        if (ktInstance->blockNumber == 0)
        {
            /* First block, absorb in final node */
            unsigned int len = (inputByteLen < (K12_chunkSize - ktInstance->queueAbsorbedLen)) ? (unsigned int)inputByteLen : (K12_chunkSize - ktInstance->queueAbsorbedLen);
            XKCP::TurboSHAKE_Absorb(&ktInstance->finalNode, input, len);
            input += len;
            inputByteLen -= len;
            ktInstance->queueAbsorbedLen += len;
            if ((ktInstance->queueAbsorbedLen == K12_chunkSize) && (inputByteLen != 0))
            {
                /* First block complete and more input data available, finalize it */
                const unsigned char padding = 0x03; /* '110^6': message hop, simple padding */
                ktInstance->queueAbsorbedLen = 0;
                ktInstance->blockNumber = 1;
                XKCP::TurboSHAKE_Absorb(&ktInstance->finalNode, &padding, 1);
                ktInstance->finalNode.byteIOIndex = (ktInstance->finalNode.byteIOIndex + 7) & ~7; /* Zero padding up to 64 bits */
            }
        }
        else if (ktInstance->queueAbsorbedLen != 0)
        {
            /* There is data in the queue, absorb further in queue until block complete */
            unsigned int len = (inputByteLen < (K12_chunkSize - ktInstance->queueAbsorbedLen)) ? (unsigned int)inputByteLen : (K12_chunkSize - ktInstance->queueAbsorbedLen);
            XKCP::TurboSHAKE_Absorb(&ktInstance->queueNode, input, len);
            input += len;
            inputByteLen -= len;
            ktInstance->queueAbsorbedLen += len;
            if (ktInstance->queueAbsorbedLen == K12_chunkSize)
            {
                int capacityInBytes = 2 * (ktInstance->securityLevel) / 8;
                unsigned char intermediate[maxCapacityInBytes];
                // assert(capacityInBytes <= maxCapacityInBytes);
                ktInstance->queueAbsorbedLen = 0;
                ++ktInstance->blockNumber;
                XKCP::TurboSHAKE_AbsorbDomainSeparationByte(&ktInstance->queueNode, K12_suffixLeaf);
                XKCP::TurboSHAKE_Squeeze(&ktInstance->queueNode, intermediate, capacityInBytes);
                XKCP::TurboSHAKE_Absorb(&ktInstance->finalNode, intermediate, capacityInBytes);
            }
        }

        while (inputByteLen > 0)
        {
            int capacityInBytes = 2 * (ktInstance->securityLevel) / 8;
            unsigned int len = (inputByteLen < K12_chunkSize) ? (unsigned int)inputByteLen : K12_chunkSize;
            unsigned int chunkIndex = ktInstance->blockNumber;

            if (!isChunkChangedMap[chunkIndex])
            {
                if (len == K12_chunkSize)
                {
                    unsigned char *intermediate = intermediateMap[chunkIndex].intermediate;
                    XKCP::TurboSHAKE_Absorb(&ktInstance->finalNode, intermediate, capacityInBytes);
                    input += len;
                    inputByteLen -= len;
                    ++ktInstance->blockNumber;
                    continue;
                }
            }

            XKCP::TurboSHAKE_Initialize(&ktInstance->queueNode, ktInstance->securityLevel);
            XKCP::TurboSHAKE_Absorb(&ktInstance->queueNode, input, len);
            input += len;
            inputByteLen -= len;
            if (len == K12_chunkSize)
            {
                unsigned char intermediate[maxCapacityInBytes];
                // assert(capacityInBytes <= maxCapacityInBytes);
                ++ktInstance->blockNumber;
                XKCP::TurboSHAKE_AbsorbDomainSeparationByte(&ktInstance->queueNode, K12_suffixLeaf);
                XKCP::TurboSHAKE_Squeeze(&ktInstance->queueNode, intermediate, capacityInBytes);
                XKCP::TurboSHAKE_Absorb(&ktInstance->finalNode, intermediate, capacityInBytes);

                // cache the intermediate state
                Intermediate &inter = intermediateMap[chunkIndex];
                std::memcpy(inter.intermediate, intermediate, capacityInBytes);
                isChunkChangedMap[chunkIndex] = false;
            }
            else
            {
                ktInstance->queueAbsorbedLen = len;
            }
        }

        return 0;
    }

public:
    K12Engine(unsigned char *state, size_t stateSize)
    {
        _state = state;
        _stateSize = stateSize;
        maxChunks = (stateSize + K12_chunkSize - 1) / K12_chunkSize;

        isChunkChangedMap.resize(maxChunks);
        intermediateMap.resize(maxChunks);
        for (unsigned int i = 0; i < maxChunks; i++)
        {
            isChunkChangedMap[i] = true;
        }
        for (unsigned int i = 0; i < maxChunks; i++)
        {
            std::memset(intermediateMap[i].intermediate, 0, maxCapacityInBytes);
        }

        _lastOutput = new unsigned char[1024];
        _lastOutputSize = 0;
    }

    int getHash(unsigned char* output, size_t outputByteLen)
    {
        if (_lastOutput && outputByteLen == _lastOutputSize && isAllChunksUnchanged())
        {
            // return cached output
            std::memcpy(output, _lastOutput, outputByteLen);
            return 0;
        }

        XKCP::KangarooTwelve_Instance ktInstance;

        if (outputByteLen == 0)
            return 1;
        XKCP::KangarooTwelve_Initialize(&ktInstance, 128, outputByteLen);
        if (_KangarooTwelve_Update(&ktInstance, _state, _stateSize) != 0)
            return 1;
        int ok =  XKCP::KangarooTwelve_Final(&ktInstance, output, nullptr, 0);

        // cache last output
        std::memcpy(_lastOutput, output, outputByteLen);
        _lastOutputSize = outputByteLen;

        return ok;
    }

    void markChunkChanged(unsigned int chunkIndex)
    {
        if (chunkIndex < maxChunks)
        {
            isChunkChangedMap[chunkIndex] = true;
        }
    }

    bool isAllChunksUnchanged() const
    {
        for (unsigned int i = 0; i < maxChunks; i++)
        {
            if (isChunkChangedMap[i])
            {
                return false;
            }
        }
        return true;
    }

    unsigned int getMaxChunks() const { return maxChunks; }
};

// linux userfaultfd integration into K12Engine
class ContractStateEngine : public K12Engine
{
    // IO related
    static constexpr size_t MAX_IO_NAME_LEN = 128;
    static inline constexpr CHAR16 BASE_DIR[] = L"contract_states/";

    // Lazy loading related
    static inline std::vector<ContractStateEngine*> allEngines;
    UserFaultFD uffd;
    size_t nonPaddedSize;
    size_t paddedSize;
    unsigned int contractIndex;
    bool isUffdRegistered = false;

public:
    static void* allocateState(size_t size) {
        size = alignToPageSize(size);
        void* buf = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (buf == MAP_FAILED) throw std::bad_alloc();
        memset(buf, 0, size);
        return buf;
    }

    static bool create(unsigned char **state, size_t stateSize, unsigned int contractIndex) {
        static std::once_flag flag;
        std::call_once(flag, []() {
            allEngines.resize(contractCount);
        });

        auto engine = new ContractStateEngine(state, stateSize, contractIndex);
        allEngines[contractIndex] = engine;

        return true;
    }

    static void registerAllUserFaultFDs() {
        for (auto engine : allEngines) {
            if (engine) {
                engine->registerUserFaultFD();
            }
        }
    }

    static ContractStateEngine* getEngine(unsigned int contractIndex) {
        if (contractIndex < allEngines.size()) {
            return allEngines[contractIndex];
        }
        return nullptr;
    }

    ContractStateEngine(unsigned char **state, size_t stateSize, unsigned int contractIndex)
        : K12Engine((unsigned char*)allocateState(stateSize), stateSize)
    {
        *state = _state;
        this->contractIndex = contractIndex;
        this->nonPaddedSize = stateSize;
        this->paddedSize = alignToPageSize(stateSize);

        // ensure data pages directory exists
        CHAR16 dir[MAX_IO_NAME_LEN] = {};
        getDirectory(dir);
        createDir(dir);
    }

    void getDirectory(CHAR16 outDirectory[MAX_IO_NAME_LEN])
    {
        CHAR16 contractAssetName[MAX_IO_NAME_LEN] = {};
        string_to_wchar_t(contractDescriptions[contractIndex].assetName, contractAssetName);

        setText(outDirectory, BASE_DIR);
        if (contractIndex != 0)
        {
            appendText(outDirectory, contractAssetName);
        } else
        {
            appendText(outDirectory, "Contract0State");
        }
    }

    void getPageId(CHAR16 pageName[MAX_IO_NAME_LEN], unsigned int chunkIndex)
    {
        const ContractDescription *desc = &contractDescriptions[contractIndex];
        struct
        {
            ContractDescription desc;
            unsigned int chunkIndex;
        } pageIdStruct;
        std::memcpy(&pageIdStruct.desc, desc, sizeof(ContractDescription));
        pageIdStruct.chunkIndex = chunkIndex;

        m256i digest{};
        KangarooTwelve(&pageIdStruct, sizeof(pageIdStruct), digest.m256i_u8, sizeof(digest));
        getIdentity(digest.m256i_u8, pageName, true);
        setMem(pageName + 10, 8, 0);
    }

    void registerUserFaultFD()
    {
        if (isUffdRegistered) return;

        // register region for write-protect tracking
        uffdio_register reg{};
        reg.range.start = (uint64_t)_state;
        reg.range.len = paddedSize;
        reg.mode = UFFDIO_REGISTER_MODE_WP | UFFDIO_REGISTER_MODE_MISSING;

        if (ioctl(uffd.get(), UFFDIO_REGISTER, &reg) == -1)
            throw std::runtime_error("UFFDIO_REGISTER failed");

        isUffdRegistered = true;

        // write-protect whole region
        reprotectRegion();

        std::thread handler([=]()
            {
                size_t page_size = SYSTEM_PAGE_SIZE;
                pollfd pfd{ uffd.get(), POLLIN, 0 };
                while (true) {
                    poll(&pfd, 1, -1);
                    uffd_msg msg;
                    if (read(uffd.get(), &msg, sizeof(msg)) != sizeof(msg))
                        continue;

                    if (msg.event != UFFD_EVENT_PAGEFAULT) continue;

                    auto flags = msg.arg.pagefault.flags;

                    bool is_wp = flags & UFFD_PAGEFAULT_FLAG_WP;
                    bool is_minor = flags & UFFD_PAGEFAULT_FLAG_MINOR;

                    // If neither is set → MISSING
                    bool is_missing = !is_wp && !is_minor;

                    auto accessAddress = msg.arg.pagefault.address;
                    auto pageAddress = msg.arg.pagefault.address & ~(page_size - 1);

                    size_t offset = accessAddress - (size_t)_state;
                    unsigned int chunkIndex = offset / K12_chunkSize;

                    auto startRange = (size_t)_state + (chunkIndex * (size_t)K12_chunkSize);
                    // if there is only 1 page left (system memory page) then just need to cover to the last system page (cover full K12 chunk will go beyond the state size)
                    size_t lenRange = std::min(paddedSize - (chunkIndex * (size_t)K12_chunkSize), (size_t)K12_chunkSize);
                    // handle write-protect page fault
                    if (is_wp)
                    {
                        markChunkChanged(chunkIndex);
                        printf("Contract %u: page fault at address 0x%llx, chunk %u marked changed\n",
                               contractIndex, (unsigned long long)accessAddress, chunkIndex);

                        // remove write-protect so write can continue
                        uffdio_writeprotect uwp{};
                        uwp.range.start = startRange;
                        uwp.range.len = lenRange;
                        uwp.mode = 0;

                        if (ioctl(uffd.get(), UFFDIO_WRITEPROTECT, &uwp) == -1)
                        {
                            std::cout << "Contract " << contractIndex << ": UFFDIO_WRITEPROTECT remove failed\n";
                        }
                    }

                    // handle missing page fault
                    if (is_missing)
                    {
                        uffdio_zeropage zp{};
                        zp.range.start = pageAddress;
                        zp.range.len = page_size;

                        printf("Page missing at address 0x%llx, zeroing page\n",
                               (unsigned long long)accessAddress);

                        if (ioctl(uffd.get(), UFFDIO_ZEROPAGE, &zp) == -1)
                        {
                            std::cout << "Contract " << contractIndex << ": UFFDIO_ZEROPAGE failed\n";
                        }
                    }
                }
            });
        handler.detach();
    }

    void reprotectRegion() {
        if (!isUffdRegistered) return;

        uffdio_writeprotect wp {};

        wp.range.start = (uint64_t)_state;
        wp.range.len   = paddedSize;

        // UFFDIO_WRITEPROTECT_MODE_WP: Sets the write-protect bit
        // (If you set mode to 0, it removes protection)
        wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

        if (ioctl(uffd.get(), UFFDIO_WRITEPROTECT, &wp) == -1) {
            std::cout << "Contract " << contractIndex << ": UFFDIO_WRITEPROTECT failesd\n";

            // mark all chunks as changed to be safe
            for (unsigned int i = 0; i < maxChunks; i++)
            {
                isChunkChangedMap[i] = true;
            }
        }
    }

    int getHashAndReprotect(unsigned char* output, size_t outputByteLen)
    {
        int res = getHash(output, outputByteLen);
        reprotectRegion();
        return res;
    }
};