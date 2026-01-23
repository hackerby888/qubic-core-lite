#pragma once
#include <K12/kangaroo_twelve_xkcp.h>
#include <cstring>
#include <unordered_map>

class K12Engine
{
    struct Intermediate
    {
        unsigned char intermediate[maxCapacityInBytes];
    };

    std::unordered_map<unsigned int, Intermediate> intermediateMap;
    std::unordered_map<unsigned int, bool> isChunkChangedMap;
    unsigned int maxChunks;
    unsigned char *_state;
    size_t _stateSize;

private:
    int _KangarooTwelve_Update(XKCP::KangarooTwelve_Instance* ktInstance, const unsigned char* input, size_t inputByteLen)
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

        for (unsigned int i = 0; i < maxChunks; i++)
        {
            isChunkChangedMap[i] = true;
        }
    }

    int getHash128( unsigned char* output, size_t outputByteLen)
    {
        XKCP::KangarooTwelve_Instance ktInstance;

        if (outputByteLen == 0)
            return 1;
        XKCP::KangarooTwelve_Initialize(&ktInstance, 128, outputByteLen);
        if (_KangarooTwelve_Update(&ktInstance, _state, _stateSize) != 0)
            return 1;
        return XKCP::KangarooTwelve_Final(&ktInstance, output, nullptr, 0);
    }

    void markChunkChanged(unsigned int chunkIndex)
    {
        if (chunkIndex < maxChunks)
        {
            isChunkChangedMap[chunkIndex] = true;
        }
    }
};