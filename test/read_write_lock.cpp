#define NO_UEFI

#include "gtest/gtest.h"
#include "platform/read_write_lock.h"

class ReadWriteLockTest : public ::testing::Test {
protected:
    ReadWriteLock lock;
};

// Test reset functionality
TEST_F(ReadWriteLockTest, Reset) {
    lock.reset();
    EXPECT_EQ(lock.getCurrentReaderLockCount(), 0);
    EXPECT_FALSE(lock.isLockedForWriting());
}

// Test acquiring and releasing a read lock
TEST_F(ReadWriteLockTest, AcquireAndReleaseRead) {
    lock.reset();
    lock.acquireRead();
    EXPECT_EQ(lock.getCurrentReaderLockCount(), 1);
    lock.releaseRead();
    EXPECT_EQ(lock.getCurrentReaderLockCount(), 0);
}

// Test acquiring and releasing a write lock
TEST_F(ReadWriteLockTest, AcquireAndReleaseWrite) {
    lock.reset();
    lock.acquireWrite();
    EXPECT_TRUE(lock.isLockedForWriting());
    lock.releaseWrite();
    EXPECT_FALSE(lock.isLockedForWriting());
    EXPECT_EQ(lock.getCurrentReaderLockCount(), 0);
}

// Test tryAcquireRead functionality
TEST_F(ReadWriteLockTest, TryAcquireRead) {
    lock.reset();
    EXPECT_TRUE(lock.tryAcquireRead());
    EXPECT_TRUE(lock.tryAcquireRead());
    EXPECT_EQ(lock.getCurrentReaderLockCount(), 2);
    lock.releaseRead();
    EXPECT_EQ(lock.getCurrentReaderLockCount(), 1);
    // extpect fail when try write
    EXPECT_FALSE(lock.tryAcquireWrite());
    lock.releaseRead();
    EXPECT_EQ(lock.getCurrentReaderLockCount(), 0);
}

// Test tryAcquireWrite functionality
TEST_F(ReadWriteLockTest, TryAcquireWrite) {
    lock.reset();
    EXPECT_TRUE(lock.tryAcquireWrite());
    EXPECT_TRUE(lock.isLockedForWriting());
    lock.releaseWrite();
}

// Test writer priority over readers
TEST_F(ReadWriteLockTest, WriterPriority) {
    lock.reset();
    lock.acquireWrite();
    EXPECT_FALSE(lock.tryAcquireRead());
    lock.releaseWrite();
    EXPECT_TRUE(lock.tryAcquireRead());
    lock.releaseRead();
}