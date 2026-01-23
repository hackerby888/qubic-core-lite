#define NO_UEFI
#include "platform/m256.h"
#include <iostream>
#include <chrono>
#include <extensions/k12_engine.h>
#include <gtest/gtest.h>

TEST(K12EngineTest, PlaceholderTest) {
    size_t ONE_GB = 1024 * 1024 * 1024;
    unsigned char *state = new unsigned char[ONE_GB];
    K12Engine k12Engine(state, ONE_GB);
    m256i hash1;
    m256i hash2;
    auto startTime = std::chrono::high_resolution_clock::now();
    k12Engine.getHash128(hash1.m256i_u8, 32);
    k12Engine.getHash128(hash2.m256i_u8, 32);
    auto durationMilliSec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime);
    std::cout << "K12Engine getHash128 called twice on 1 GB input took " << durationMilliSec.count() << " ms" << std::endl;

    startTime = std::chrono::high_resolution_clock::now();
    m256i xkcpHash;
    XKCP::KangarooTwelve(state, ONE_GB, xkcpHash.m256i_u8, 32);
    durationMilliSec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime);
    std::cout << "XKCP KangarooTwelve called on 1 GB input took " << durationMilliSec.count() << " ms" << std::endl;
    EXPECT_EQ(hash1, xkcpHash);
    EXPECT_EQ(hash1, hash2);
}