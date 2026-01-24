#define NO_UEFI
#include "platform/msvc_polyfill.h"
#include "platform/m256.h"
#include <chrono>
#include <extensions/k12_engine.h>
#include <gtest/gtest.h>
#include <immintrin.h>
#include <iostream>

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