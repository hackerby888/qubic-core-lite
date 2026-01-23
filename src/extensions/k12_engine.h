#pragma once

#define _GNU_SOURCE
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

        isChunkChangedMap.reserve(maxChunks);
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
        if (outputByteLen == _lastOutputSize && isAllChunksUnchanged())
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
    UserFaultFD uffd;
    size_t nonPaddedSize;
    unsigned int contractIndex;
public:
    ContractStateEngine(unsigned char **state, size_t stateSize, unsigned int contractIndex)
    {
        size_t padded_size = alignToPageSize(stateSize);
        void* buf = mmap(nullptr,
                         padded_size,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS,
                         -1, 0);

        if (buf == MAP_FAILED) {
            std::cout << "mmap failed" << std::endl;
            throw std::bad_alloc();
        }
        // Zero out allocated memory
        *state = buf;
        setMem(*state, padded_size, 0);

        // NOTE: K12Engine must use the original stateSize for correct hashing
        K12Engine::K12Engine(*state, stateSize);

        uffd = UserFaultFD();

        this->nonPaddedSize = stateSize;
        this->contractIndex = contractIndex;
    }

    void registerUserFaultFD()
    {
        // register region for write-protect tracking
        uffdio_register reg{};
        reg.range.start = (uint64_t)_state;
        reg.range.len = _stateSize;
        reg.mode = UFFDIO_REGISTER_MODE_WP;

        if (ioctl(uffd.get(), UFFDIO_REGISTER, &reg) == -1)
            throw std::runtime_error("UFFDIO_REGISTER failed");

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

                    auto accessAddress = msg.arg.pagefault.address;
                    auto pageAddress = msg.arg.pagefault.address & ~(page_size - 1);

                    // TODO
                    size_t offset = accessAddress - (uint64_t)_state;
                    unsigned int chunkIndex = offset / K12_chunkSize;
                    markChunkChanged(chunkIndex);

                    // remove write-protect so write can continue
                    uffdio_writeprotect uwp{};
                    uwp.range.start = pageAddress;
                    uwp.range.len = page_size;
                    uwp.mode = 0;

                    ioctl(uffd.get(), UFFDIO_WRITEPROTECT, &uwp);
                }
            });
        handler.detach();
    }

    void reprotectRegion() {
        uffdio_writeprotect wp {};

        wp.range.start = (uint64_t)_state;
        wp.range.len   = _stateSize;

        // UFFDIO_WRITEPROTECT_MODE_WP: Sets the write-protect bit
        // (If you set mode to 0, it removes protection)
        wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

        if (ioctl(uffd.get(), UFFDIO_WRITEPROTECT, &wp) == -1) {
            perror("Reprotect ioctl failed");
        }
    }
};