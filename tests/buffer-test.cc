#include <cerrno>

#include "libbuffy/buffer.h"

#include "gtest/gtest.h"

TEST(Buffer, new_and_free) {
    struct bfy_buffer* buf = bfy_buffer_new();
    EXPECT_NE(nullptr, buf);
    EXPECT_EQ(0, bfy_buffer_get_writable_size(buf));
    EXPECT_EQ(0, bfy_buffer_get_readable_size(buf));
    bfy_buffer_free(buf);
}

TEST(Buffer, init_and_destruct) {
    struct bfy_buffer buf = bfy_buffer_init();
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add) {
    BFY_HEAP_BUFFER(buf);

    std::string const in = "Hello, World!";
    bfy_buffer_add(&buf, in.data(), in.length());
    char* out;
    size_t outlen;
    auto const rv = bfy_buffer_take_string(&buf, &out, &outlen);

    EXPECT_EQ(0, rv);
    EXPECT_EQ(in.length(), outlen);
    EXPECT_EQ(in, out);

    free(out);
}

TEST(Buffer, add_ch) {
    char const ch = 'z';

    BFY_HEAP_BUFFER(buf);
    bfy_buffer_add_ch(&buf, ch);

    EXPECT_EQ(ch, *reinterpret_cast<char*>(bfy_buffer_begin(&buf)));
    EXPECT_EQ(1, bfy_buffer_get_readable_size(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add_printf) {
    BFY_HEAP_BUFFER(buf);
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));

    int rv = bfy_buffer_add_printf(&buf, "%s, %s!", "Hello", "World");
    EXPECT_EQ(0, rv);
    std::string const expected = "Hello, World!";
    EXPECT_EQ(expected.length(), bfy_buffer_get_readable_size(&buf));

    char* out = NULL;
    size_t outlen = 0;
    rv = bfy_buffer_take_string(&buf, &out, &outlen);
    EXPECT_EQ(0, rv);
    EXPECT_EQ(expected.length(), outlen);
    EXPECT_EQ(expected, out);

    free(out);
}

TEST(Buffer, clear) {
    BFY_HEAP_BUFFER(buf)

    std::string const in = "1234567890";
    bfy_buffer_add(&buf, in.data(), in.length());
    EXPECT_EQ(in.length(), bfy_buffer_get_readable_size(&buf));

    bfy_buffer_clear(&buf);
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, stack) {
    BFY_STACK_BUFFER(buf, 512);
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(512, bfy_buffer_get_writable_size(&buf));

    std::string const in = "Hello, World!";
    bfy_buffer_add(&buf, in.data(), in.length());
    EXPECT_EQ(in.length(), bfy_buffer_get_readable_size(&buf));

    char* out;
    size_t outlen;
    auto const rv = bfy_buffer_take_string(&buf, &out, &outlen);
    EXPECT_EQ(0, rv);
    EXPECT_EQ(buf_stack, out);
    EXPECT_EQ(in, out);
    EXPECT_EQ(in.length(), outlen);
}

TEST(Buffer, add_too_much) {
    const char Fill = 'z';
    char array[5];
    memset(array, Fill, sizeof(array));

    const size_t Limit = sizeof(array) - 1;
    bfy_buffer buf = bfy_buffer_init_unowned(array, Limit);

    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(Limit, bfy_buffer_get_writable_size(&buf));

    std::string const first_half = "123";
    auto rv = bfy_buffer_add(&buf, first_half.data(), first_half.size());
    EXPECT_EQ(0, rv);
    size_t length = bfy_buffer_get_readable_size(&buf);
    EXPECT_EQ(first_half.size(), length);
    size_t available = bfy_buffer_get_writable_size(&buf);
    EXPECT_EQ(Limit - length, available);
    EXPECT_TRUE(!memcmp(first_half.data(), bfy_buffer_begin(&buf), length));

    std::string const second_half = "456";
    errno = 0;
    rv = bfy_buffer_add(&buf, second_half.data(), second_half.size());
    EXPECT_EQ(-1, rv);
    EXPECT_EQ(ENOMEM, errno);
    EXPECT_EQ(length, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(available, bfy_buffer_get_writable_size(&buf));

    errno = 0;
    rv = bfy_buffer_add(&buf, second_half.data(), 2);
    EXPECT_EQ(-1, rv);
    EXPECT_EQ(ENOMEM, errno);
    EXPECT_EQ(length, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(available, bfy_buffer_get_writable_size(&buf));

    errno = 0;
    rv = bfy_buffer_add(&buf, second_half.data(), 1);
    EXPECT_EQ(0, rv);
    EXPECT_EQ(0, errno);
    EXPECT_EQ(Limit, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));
    EXPECT_TRUE(!memcmp("1234", bfy_buffer_begin(&buf), Limit));

    errno = 0;
    char* out = NULL;
    size_t outlen = 0;
    rv = bfy_buffer_take_string(&buf, &out, &outlen);
    EXPECT_EQ(-1, rv);
    EXPECT_EQ(ENOMEM, errno);
    EXPECT_EQ(Limit, outlen);
    EXPECT_TRUE(!memcmp(out, (first_half+second_half).data(), Limit));
    EXPECT_EQ(array, out);
    EXPECT_EQ(Fill, array[Limit]);
}
