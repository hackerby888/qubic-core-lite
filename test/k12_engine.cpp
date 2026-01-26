#define NO_UEFI
#define _GNU_SOURCE

#include "platform/msvc_polyfill.h"
#include "platform/file_io.h"
#include "platform/m256.h"
#include <chrono>
#include <extensions/k12_engine.h>
#include <gtest/gtest.h>
#include <immintrin.h>
#include <iostream>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <thread>
#include <unordered_map>
#include <list>

TEST(K12EngineTest, GeneralTest) {
    size_t ONE_GB = 1024 * 1024 * 1024 - 64;
    unsigned char *state = new unsigned char[ONE_GB];
    K12Engine k12Engine(state, ONE_GB);
    memset(state, 0, ONE_GB);

    unsigned long long ranVal;
    _rdrand64_step(&ranVal);
    for (size_t i = 0; i < ONE_GB; i += 8) {
        if (i + 8 >= ONE_GB)
        {
            break;
        }
        std::memcpy(state + i, &ranVal, sizeof(unsigned long long));
        ranVal++;
    }

    m256i hash1;
    m256i hash2;
    auto startTime = std::chrono::high_resolution_clock::now();
    k12Engine.getHash(hash1.m256i_u8, 32);
    k12Engine.getHash(hash2.m256i_u8, 32);
    auto durationMilliSec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime);
    std::cout << "K12Engine getHash called twice on 1 GB input took " << durationMilliSec.count() << " ms" << std::endl;

    {
        startTime = std::chrono::high_resolution_clock::now();
        m256i xkcpHash;
        XKCP::KangarooTwelve(state, ONE_GB, xkcpHash.m256i_u8, 32);
        durationMilliSec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime);
        std::cout << "XKCP KangarooTwelve called on 1 GB input took " << durationMilliSec.count() << " ms" << std::endl;
        EXPECT_EQ(hash1, xkcpHash);
        EXPECT_EQ(hash1, hash2);
    }

    {
        // change a chunk and test again
        for (int i = 0; i < 1024 * 8; i++)
        {
            unsigned long long ran;
            _rdrand64_step(&ran);
            unsigned int chunkIndex = ran % k12Engine.getMaxChunks();
            state[chunkIndex * K12_chunkSize] ^= 0xFF; // flip first byte of the chunk
            k12Engine.markChunkChanged(chunkIndex);
        }
        startTime = std::chrono::high_resolution_clock::now();
        k12Engine.getHash(hash1.m256i_u8, 32);
        durationMilliSec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime);
        std::cout << "K12Engine getHash after modifying chunks took " << durationMilliSec.count() << " ms" << std::endl;
        EXPECT_NE(hash1, hash2);

        // compare with XKCP again
        startTime = std::chrono::high_resolution_clock::now();
        m256i xkcpHash;
        XKCP::KangarooTwelve(state, ONE_GB, xkcpHash.m256i_u8, 32);
        durationMilliSec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime);
        std::cout << "XKCP KangarooTwelve after modifying first chunk took " << durationMilliSec.count() << " ms" << std::endl;
        EXPECT_EQ(hash1, xkcpHash);
    }
}

TEST(K12EngineTest, ContractEngineTest)
{
    unsigned char *contractBuffer;
    size_t contractSize = 1024 * 1024 * 1024 + 65; // 1 GB
    ContractStateEngine::create(&contractBuffer, contractSize, 1);
    auto engine = ContractStateEngine::getEngine(1);
    engine->registerUserFaultFD();
    for (size_t i = 0; i < contractSize; i+=K12_chunkSize)
    {
        if (i + K12_chunkSize > contractSize)
        {
            break;
        }
       *((size_t*)(contractBuffer + i)) = i;
    }
    *((size_t*)(contractBuffer + (engine->getMaxChunks()-1) * K12_chunkSize)) = (engine->getMaxChunks()-1) * K12_chunkSize;

    m256i hash1;
    engine->getHash(hash1.m256i_u8, 32);
    engine->reprotectWriteRegion();
    m256i hash2;
    engine->getHash(hash2.m256i_u8, 32);
    engine->reprotectReadRegion();
    m256i hash3;
    engine->getHash(hash3.m256i_u8, 32);
    m256i hash4;
    engine->reprotectReadRegion();
    engine->getHash(hash4.m256i_u8, 32);
    m256i xkcpHash;
    XKCP::KangarooTwelve(contractBuffer, contractSize, xkcpHash.m256i_u8, 32);

    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(hash1, hash3);
    EXPECT_EQ(hash1, hash4);
    EXPECT_EQ(hash1, xkcpHash);

    engine->reprotectReadRegion();
    std::cout << *contractBuffer << std::endl;

    unsigned long long combo = ContractStateEngine::accessList.front();
    EXPECT_EQ((unsigned int)(combo >> 32), 1);
    EXPECT_EQ((unsigned int)(combo & 0xFFFFFFFF), 0);

    EXPECT_EQ(engine->getTotalMemoryInRam(), engine->paddedSize);
    EXPECT_EQ(ContractStateEngine::getRamUsageByAllEngines(), engine->getTotalMemoryInRam());

    engine->saveChunkToDisk(0);
    engine->saveChunkToDisk(4);
    engine->saveChunkToDisk(8);
    EXPECT_EQ(engine->getTotalMemoryInRam(), engine->paddedSize - (3 * K12_chunkSize));

    // try read evicted chunk
    size_t val = *((size_t*)contractBuffer);
    EXPECT_EQ(val, 0);
    val = *((size_t*)(contractBuffer + 4 * K12_chunkSize));
    EXPECT_EQ(val, 4 * K12_chunkSize);
    val = *((size_t*)(contractBuffer + 8 * K12_chunkSize));
    EXPECT_EQ(val, 8 * K12_chunkSize);

    engine->flushAllChunksToDisk();
    EXPECT_EQ(engine->getTotalMemoryInRam(), 0);
    EXPECT_EQ(engine->getMaxChunks(), (contractSize + K12_chunkSize - 1) / K12_chunkSize);

    engine->reprotectWriteRegion();
    *((size_t*)(contractBuffer + (engine->getMaxChunks()-1) * K12_chunkSize)) = 0;
    val = *((size_t*)(contractBuffer + (engine->getMaxChunks()-1) * K12_chunkSize));
    EXPECT_EQ(val, 0);
}