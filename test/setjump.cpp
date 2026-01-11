#include <gtest/gtest.h>
#include <setjmp.h>

// Global or scoped jump buffer
jmp_buf jump_buffer;

// A simulated function that "fails" and jumps back
void low_level_function(int trigger_fail) {
    if (trigger_fail) {
        // 1 is the value that setjmp will return
        longjmp(jump_buffer, 1);
    }
}

void middle_function(int trigger_fail) {
    low_level_function(trigger_fail);
}

// The Test Case
TEST(SetJmpTest, HandlesNonLocalJump) {
    // setjmp returns 0 when first called (setting the save point)
    // It returns the value passed to longjmp when jumping back
    int control_val = setjmp(jump_buffer);

    if (control_val == 0) {
        // This is the initial execution path
        EXPECT_NO_THROW(middle_function(1));

        // This line should technically never be reached if longjmp works
        ADD_FAILURE() << "longjmp did not intercept the execution flow";
    } else {
        // This is the path taken AFTER longjmp is called
        EXPECT_EQ(control_val, 1);
        SUCCEED();
    }
}

TEST(SetJmpTest, NormalExecutionNoJump) {
    int control_val = setjmp(jump_buffer);

    if (control_val == 0) {
        middle_function(0); // Pass 0 so no longjmp occurs
        SUCCEED();
    } else {
        FAIL() << "longjmp was called unexpectedly";
    }
}