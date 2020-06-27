#include "libbuffy/buffer.h"

#include "gtest/gtest.h"

TEST(TestBuffer, add) {
    BFY_HEAP_BUFFER(buf);

    std::string const in = "Hello, World!";
    bfy_buffer_add(&buf, in.data(), in.length());
    size_t outlen;
    char* out = bfy_buffer_destruct_to_string(&buf, &outlen);
    EXPECT_EQ(in.length(), outlen);
    EXPECT_EQ(in, out);

    bfy_buffer_destruct(&buf);
}

TEST(TestBuffer, stack) {
    std::string const in = "Hello, World!";

    BFY_STACK_BUFFER(buf, 512);
    bfy_buffer_add(&buf, in.data(), in.length());
    size_t outlen;
    char* out = bfy_buffer_destruct_to_string(&buf, &outlen);

    EXPECT_EQ(buf_stack, out);
    EXPECT_EQ(in, out);
    EXPECT_EQ(in.length(), outlen);
}

TEST(TestBuffer, add_ch) {
    char const ch = 'z';

    BFY_HEAP_BUFFER(buf);
    bfy_buffer_add_ch(&buf, ch);

    EXPECT_EQ(ch, *reinterpret_cast<char*>(bfy_buffer_begin(&buf)));
    EXPECT_EQ(1, buf.length);
    EXPECT_GE(1, buf.memory.capacity);

    bfy_buffer_destruct(&buf);
}
