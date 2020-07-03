/*
 * Copyright 2020 Mnemosyne LLC
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstring>  // memcmp()
#include <numeric>
#include <string_view>
#include <type_traits>

#include "libbuffy/buffer.h"
#include "../src/portable-endian.h"

#include "gtest/gtest.h"

///

bool operator== (bfy_iovec const& a, struct bfy_iovec const& b) {
    return a.iov_base == b.iov_base && a.iov_len == b.iov_len;
}

namespace {

size_t buffer_count_blocks(bfy_buffer const* buf, size_t n_bytes = SIZE_MAX) {
    return bfy_buffer_peek(buf, n_bytes, nullptr, 0);
}

auto buffer_get_blocks(bfy_buffer const* buf, size_t n_bytes = SIZE_MAX) {
    auto const n = buffer_count_blocks(buf, n_bytes);
    auto vecs = std::vector<struct bfy_iovec>(n);
    bfy_buffer_peek(buf, n_bytes, std::data(vecs), std::size(vecs));
    return vecs;
}

auto buffer_copyout(bfy_buffer const* buf, size_t n_bytes = SIZE_MAX) {
    n_bytes = std::min(n_bytes, bfy_buffer_get_content_len(buf));
    auto bytes = std::vector<char>(n_bytes);
    bfy_buffer_copyout(buf, std::data(bytes), n_bytes);
    return bytes;
}

auto buffer_remove_string(bfy_buffer* buf) {
    size_t len;
    auto* pch = bfy_buffer_remove_string(buf, &len);
    auto str = std::string(pch, len);
    free(pch);
    return str;
}

template<std::size_t N>
class BufferWithLocalArray {
 public:
    std::array<char, N>  array = {};
    bfy_buffer buf;
    BufferWithLocalArray() {
        buf = bfy_buffer_init_unmanaged(std::data(array), std::size(array));
    }
    ~BufferWithLocalArray() {
        bfy_buffer_destruct(&buf);
    }
};

class BufferWithReadonlyStrings {
 public:
    bfy_buffer buf;
    static std::string_view constexpr str1 = { "Earth" };
    static std::string_view constexpr str2 = { "Vs." };
    static std::string_view constexpr str3 = { "Soup" };
    static std::array<std::string_view, 3> constexpr strs = { str1, str2, str3 };
    std::string allstrs;
    BufferWithReadonlyStrings() {
      buf = bfy_buffer_init();
      for (auto const& str : strs) {
        allstrs += str;
        bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));
      }
    }
    ~BufferWithReadonlyStrings() {
      bfy_buffer_destruct(&buf);
    }
};

}  // anonymous namespace

///

TEST(Buffer, init_and_destruct) {
    // test that you can instantiate and destruct a buffer on the stack
    auto buf = bfy_buffer_init();
    EXPECT_EQ(0, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(0, bfy_buffer_get_space_len(&buf));
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, new_and_free) {
    // test that you can create and destroy a buffer on the heap
    auto* buf = bfy_buffer_new();
    EXPECT_NE(nullptr, buf);
    EXPECT_EQ(0, bfy_buffer_get_content_len(buf));
    EXPECT_EQ(0, bfy_buffer_get_space_len(buf));
    bfy_buffer_free(buf);
}

TEST(Buffer, init_unmanaged) {
    // create a buffer with a stack-managed chunk of memory to use
    auto array = std::array<char, 32>{};
    auto buf = bfy_buffer_init_unmanaged(std::data(array), std::size(array));
    EXPECT_EQ(0, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(std::size(array), bfy_buffer_get_space_len(&buf));

    // Write something to the buffer and peek it.
    // That written data should be readable and located in `array`.
    auto constexpr str = std::string_view("Hello There!");
    EXPECT_TRUE(bfy_buffer_add(&buf, std::data(str), std::size(str)));
    auto constexpr n_expected_vecs = 1;
    auto vecs = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs), std::size(vecs)));
    auto const expected_vecs = std::array<bfy_iovec, n_expected_vecs>{ { std::data(array), std::size(str) } };
    EXPECT_EQ(expected_vecs, vecs);
    EXPECT_EQ(std::size(array) - std::size(str), bfy_buffer_get_space_len(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add_readonly) {
    auto buf = bfy_buffer_init();

    // add a read-only string.
    // it should show up as readable in the buffer.
    auto constexpr str1 = std::string_view{"Hello, "};
    EXPECT_TRUE(bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1)));
    auto expected_readable_size = std::size(str1);
    EXPECT_EQ(expected_readable_size, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(0, bfy_buffer_get_space_len(&buf));

    // add another, to test that buffer knows the end block is readonly
    // and will allocate another block.
    auto constexpr str2 = std::string_view{"World!"};
    EXPECT_TRUE(bfy_buffer_add_readonly(&buf, std::data(str2), std::size(str2)));
    expected_readable_size += std::size(str2);
    EXPECT_EQ(expected_readable_size, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(0, bfy_buffer_get_space_len(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, peek) {
    auto buf = bfy_buffer_init();

    auto constexpr pt1 = std::string_view("Hello");
    auto constexpr pt2 = std::string_view("World");
    EXPECT_TRUE(bfy_buffer_add_readonly(&buf, std::data(pt1), std::size(pt1)));
    EXPECT_TRUE(bfy_buffer_add_readonly(&buf, std::data(pt2), std::size(pt2)));

    EXPECT_EQ(1, buffer_count_blocks(&buf, std::size(pt1)));
    EXPECT_EQ(2, buffer_count_blocks(&buf, std::size(pt1)+1));
    EXPECT_EQ(2, buffer_count_blocks(&buf, std::size(pt1)+std::size(pt2)));
    EXPECT_EQ(2, buffer_count_blocks(&buf, std::size(pt1)+std::size(pt2)+1));
    EXPECT_EQ(2, buffer_count_blocks(&buf));

    const bfy_iovec JunkVec = {
       .iov_base = reinterpret_cast<void*>(0xBADF00D),
       .iov_len = 666
    };

    // test that a single-vec peek works
    auto vecs = std::array<struct bfy_iovec, 4>{};
    std::fill(std::begin(vecs), std::end(vecs), JunkVec);
    EXPECT_EQ(1, bfy_buffer_peek(&buf, std::size(pt1), std::data(vecs), std::size(vecs)));
    EXPECT_EQ(std::data(pt1), vecs[0].iov_base);
    EXPECT_EQ(std::size(pt1), vecs[0].iov_len);
    EXPECT_EQ(JunkVec, vecs[1]);

    // test that a single-vec peek with a null iovec works
    EXPECT_EQ(1, buffer_count_blocks(&buf, std::size(pt1)));

    // test that a multivec peek works
    std::fill(std::begin(vecs), std::end(vecs), JunkVec);
    EXPECT_EQ(2, bfy_buffer_peek(&buf, std::size(pt1)+1, std::data(vecs), std::size(vecs)));
    EXPECT_EQ(std::data(pt1), vecs[0].iov_base);
    EXPECT_EQ(std::size(pt1), vecs[0].iov_len);
    EXPECT_EQ(std::data(pt2), vecs[1].iov_base);
    EXPECT_EQ(1, vecs[1].iov_len);
    EXPECT_EQ(JunkVec, vecs[2]);

    // test that a multivec peek with a null iovec works
    EXPECT_TRUE(buffer_count_blocks(&buf, std::size(pt1)));

    // test that the number extents needed is returned
    // even if it's greater than the number of extents passed in
    std::fill(std::begin(vecs), std::end(vecs), JunkVec);
    EXPECT_EQ(2, bfy_buffer_peek(&buf, std::size(pt1)+std::size(pt2), std::data(vecs), 1));
    EXPECT_EQ(std::data(pt1), vecs[0].iov_base);
    EXPECT_EQ(std::size(pt1), vecs[0].iov_len);
    EXPECT_EQ(JunkVec, vecs[2]);

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add) {
    auto buf = bfy_buffer_init();

    char constexpr ch = 'y';
    EXPECT_TRUE(bfy_buffer_add_ch(&buf, ch));
    EXPECT_EQ(1, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(1, buffer_count_blocks(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add_after_readonly) {
    auto pt1 = std::array<char, 6>{ 'H', 'e', 'l', 'l', 'o', ' ' };
    auto pt2 = std::array<char, 5>{ 'T', 'h', 'e', 'r', 'e' };

    // add the first part as read-only
    auto buf = bfy_buffer_init();
    EXPECT_TRUE(bfy_buffer_add_readonly(&buf, std::data(pt1), std::size(pt1)));

    // add the second part as writable
    EXPECT_TRUE(bfy_buffer_add(&buf, std::data(pt2), std::size(pt2)));
    EXPECT_EQ(std::size(pt1) + std::size(pt2), bfy_buffer_get_content_len(&buf));
    auto constexpr n_expected_vecs = 2;
    auto vecs = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs), std::size(vecs)));
    EXPECT_EQ(std::data(pt1), vecs[0].iov_base);
    EXPECT_EQ(std::size(pt1), vecs[0].iov_len);
    EXPECT_NE(std::data(pt2), vecs[1].iov_base);
    EXPECT_EQ(std::size(pt2), vecs[1].iov_len);

    // now, poke the arrays we own.
    // the readonly part of buf should change with it
    pt1[0] = 'J';
    pt2[0] = 'W';
    EXPECT_FALSE(memcmp(std::data(pt1), vecs[0].iov_base, std::size(pt1)));
    EXPECT_TRUE(memcmp(std::data(pt2), vecs[1].iov_base, std::size(pt2)));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add_printf) {
    BufferWithLocalArray<64> local;

    // printf into the buffer
    EXPECT_TRUE(bfy_buffer_add_printf(&local.buf, "%s, %s!", "Hello", "World"));
    EXPECT_EQ(1, buffer_count_blocks(&local.buf));

    // confirm that the string was written into the memory used by the buffer
    auto constexpr expected = std::string_view { "Hello, World!" };
    EXPECT_EQ(expected.length(), bfy_buffer_get_content_len(&local.buf));
    EXPECT_EQ(std::size(local.array) - expected.length(), bfy_buffer_get_space_len(&local.buf));
    EXPECT_EQ(expected, std::data(local.array));
}

TEST(Buffer, add_printf_when_not_enough_space) {
    BufferWithLocalArray<4> local;

    // printf into the buffer
    EXPECT_TRUE(bfy_buffer_add_printf(&local.buf, "%s, %s!", "Hello", "World"));

    // confirm that the string was written into the buffer
    auto constexpr expected = std::string_view { "Hello, World!" };
    EXPECT_EQ(expected, buffer_remove_string(&local.buf));
}

TEST(Buffer, make_contiguous_when_only_one_block) {
    BufferWithLocalArray<64> local;

    auto constexpr str1 = std::string_view { "General" };
    bfy_buffer_add(&local.buf, std::data(str1), std::size(str1));

    // confirm adding str1 fit inside the existing block
    auto constexpr n_expected_vecs = 1;
    auto vecs1 = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&local.buf, std::data(vecs1), std::size(vecs1)));
    EXPECT_EQ(std::data(local.array), vecs1[0].iov_base);
    EXPECT_EQ(std::size(str1), vecs1[0].iov_len);

    // confirm that making contiguous changes nothing
    // because it was already a single block
    auto* rv = bfy_buffer_make_all_contiguous(&local.buf);
    auto vecs2 = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&local.buf, std::data(vecs2), std::size(vecs2)));
    EXPECT_EQ(vecs1[0].iov_base, vecs2[0].iov_base);

    // confirm that make_contiguous() returned the right value
    EXPECT_EQ(vecs1[0].iov_base, rv);
}

namespace
{

auto constexpr str1 = std::string_view { "Earth" };
auto constexpr str2 = std::string_view { "Vs." };
auto constexpr str3 = std::string_view { "Soup" };
auto constexpr strs = std::array<std::string_view, 3> { str1, str2, str3 };

}  // anonymous namespace

TEST(Buffer, make_contiguous_when_small_request) {
    auto constexpr n_blocks_in = 2;
    auto constexpr begin = std::begin(strs);
    auto constexpr end = begin + n_blocks_in;

    // build a buffer that holds two read-only blocks
    auto buf = bfy_buffer_init();
    auto const action = [&buf](auto const& str){bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));};
    std::for_each(begin, end, action);
    auto const n_readable = bfy_buffer_get_content_len(&buf);
    auto const acc = [](auto& acc, auto const& str){return acc + std::size(str);};
    auto const n_expected_readable = std::accumulate(begin, end, size_t{}, acc);
    EXPECT_EQ(n_blocks_in, buffer_count_blocks(&buf));
    EXPECT_EQ(n_expected_readable, n_readable);

    // confirm that nothing happens when you request a block
    // that is already contiguous
    auto constexpr n_contiguous = std::size(strs.front());
    for (int i=0; i < n_contiguous; ++i) {
        auto const* rv = bfy_buffer_make_contiguous(&buf, n_contiguous);
        EXPECT_EQ(rv, std::data(strs.front()));
        EXPECT_EQ(n_blocks_in, buffer_count_blocks(&buf));
        EXPECT_EQ(n_expected_readable, bfy_buffer_get_content_len(&buf));
    }

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, make_contiguous_when_readonly_blocks) {
    auto constexpr n_blocks_in = 2;
    auto constexpr begin = std::begin(strs);
    auto constexpr end = begin + n_blocks_in;

    // setup: build a block with some read-only blocks
    auto buf = bfy_buffer_init();
    auto const action = [&buf](auto const& str){bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));};
    std::for_each(begin, end, action);
    auto const n_readable = bfy_buffer_get_content_len(&buf);
    auto const n_expected_readable = std::accumulate(begin, end, size_t{}, [](auto& acc, auto const& str){return acc + std::size(str);});
    EXPECT_EQ(n_expected_readable, n_readable);
    EXPECT_EQ(n_blocks_in, buffer_count_blocks(&buf));

    // confirm that make_contiguous put 'em in one block
    auto const* rv = bfy_buffer_make_all_contiguous(&buf);
    EXPECT_EQ(1, buffer_count_blocks(&buf));
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_content_len(&buf));
    std::for_each(begin, end, [this, rv](auto const& str){ EXPECT_NE(std::data(str), rv); });

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, make_contiguous_when_aligned_with_block) {
    BufferWithReadonlyStrings local;
    auto const n_expected_readable = std::size(local.allstrs);
    auto constexpr n_blocks_in = std::size(local.strs);
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_content_len(&local.buf));
    EXPECT_EQ(n_blocks_in, buffer_count_blocks(&local.buf));

    // try to make the first two blocks contiguous
    auto constexpr n_expected_vecs = n_blocks_in - 1;
    auto const n_bytes_contiguous = n_expected_readable - std::size(strs.back());
    bfy_buffer_make_contiguous(&local.buf, n_bytes_contiguous);

    // test that the blocks look right
    auto vecs = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&local.buf, std::data(vecs), n_expected_vecs));
    EXPECT_EQ(n_bytes_contiguous, vecs[0].iov_len);
    EXPECT_NE(std::data(str1), vecs[0].iov_base);
    EXPECT_EQ(std::size(strs.back()), vecs[1].iov_len);
    EXPECT_EQ(std::data(strs.back()), vecs[1].iov_base);
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_content_len(&local.buf));
}

TEST(Buffer, make_contiguous_when_not_aligned_with_block) {
    BufferWithReadonlyStrings local;
    auto const n_expected_readable = std::size(local.allstrs);
    auto constexpr n_blocks_in = std::size(local.strs);
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_content_len(&local.buf));
    EXPECT_EQ(n_blocks_in, buffer_count_blocks(&local.buf));

    // try to make 2-and-some blocks contiguous
    auto constexpr n_expected_vecs = 2;
    auto const n_bytes_contiguous = n_expected_readable - 1;
    bfy_buffer_make_contiguous(&local.buf, n_bytes_contiguous);

    // test that the blocks look right
    auto vecs = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&local.buf, std::data(vecs), n_expected_vecs));
    EXPECT_EQ(n_bytes_contiguous, vecs[0].iov_len);
    EXPECT_EQ(1, vecs[1].iov_len);
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_content_len(&local.buf));
}

TEST(Buffer, ensure_writable_when_already_available) {
    BufferWithLocalArray<128> local;
    for (size_t i=0; i < std::size(local.array); ++i) {
        EXPECT_TRUE(bfy_buffer_ensure_space(&local.buf, i));
        EXPECT_EQ(std::size(local.array), bfy_buffer_get_space_len(&local.buf));
        EXPECT_EQ(0, bfy_buffer_get_content_len(&local.buf));
    }
}

TEST(Buffer, ensure_writable_when_not_enough_available) {
    BufferWithLocalArray<128> local;
    auto const n_wanted = std::size(local.array) * 2;
    EXPECT_TRUE(bfy_buffer_ensure_space(&local.buf, n_wanted));
    EXPECT_EQ(0, bfy_buffer_get_content_len(&local.buf));
    EXPECT_LE(n_wanted, bfy_buffer_get_space_len(&local.buf));
}

TEST(Buffer, ensure_writable_when_readonly) {
    auto buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    EXPECT_EQ(0, bfy_buffer_get_space_len(&buf));
    EXPECT_EQ(std::size(str1), bfy_buffer_get_content_len(&buf));

    auto const n_available = 10;  // arbitrary
    EXPECT_TRUE(bfy_buffer_ensure_space(&buf, n_available));
    EXPECT_EQ(std::size(str1), bfy_buffer_get_content_len(&buf));
    EXPECT_LE(n_available, bfy_buffer_get_space_len(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, drain_on_block_boundary) {
    // setup: build a buffer with two blocks
    auto buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    bfy_buffer_add_readonly(&buf, std::data(str2), std::size(str2));
    EXPECT_EQ(2, buffer_count_blocks(&buf));
    EXPECT_EQ(0, bfy_buffer_get_space_len(&buf));
    EXPECT_EQ(std::size(str1) + std::size(str2), bfy_buffer_get_content_len(&buf));

    // drain the first block -- the second one should remain
    EXPECT_TRUE(bfy_buffer_drain(&buf, std::size(str1)));
    auto constexpr n_expected_vecs = 1;
    auto vecs = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs), std::size(vecs)));
    EXPECT_EQ(std::data(str2), vecs.front().iov_base);
    EXPECT_EQ(std::size(str2), vecs.front().iov_len);
    EXPECT_EQ(0, bfy_buffer_get_space_len(&buf));
    EXPECT_EQ(std::size(str2), bfy_buffer_get_content_len(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, drain_part_of_first_block) {
    // setup: build a buffer with two blocks
    auto buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    bfy_buffer_add_readonly(&buf, std::data(str2), std::size(str2));
    EXPECT_EQ(2, buffer_count_blocks(&buf));
    auto expected_readable_size = std::size(str1) + std::size(str2);
    auto constexpr expected_writable_size = 0;
    EXPECT_EQ(expected_readable_size, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(expected_writable_size, bfy_buffer_get_space_len(&buf));

    // drain _part_ of the first block
    auto constexpr n_drain = std::size(str1) / 2;
    EXPECT_TRUE(bfy_buffer_drain(&buf, n_drain));
    expected_readable_size -= n_drain;

    // part of the first block and all of the last block should remain
    auto constexpr n_expected_vecs = 2;
    auto vecs = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs), std::size(vecs)));
    EXPECT_EQ(std::data(str1) + n_drain, vecs.front().iov_base);
    EXPECT_EQ(std::size(str1) - n_drain, vecs.front().iov_len);
    EXPECT_EQ(std::data(str2), vecs.back().iov_base);
    EXPECT_EQ(std::size(str2), vecs.back().iov_len);
    EXPECT_EQ(expected_readable_size, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(expected_writable_size, bfy_buffer_get_space_len(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, drain_zero) {
    // setup: build a buffer with two blocks
    auto buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    bfy_buffer_add_readonly(&buf, std::data(str2), std::size(str2));
    auto constexpr n_expected_vecs = 2;
    auto vecs_pre = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs_pre), std::size(vecs_pre)));
    auto constexpr n_expected_readable = std::size(str1) + std::size(str2);
    auto constexpr n_expected_writable = 0;
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(n_expected_writable, bfy_buffer_get_space_len(&buf));

    // remove nothing
    EXPECT_TRUE(bfy_buffer_drain(&buf, 0));

    // confirm that nothing changed
    auto vecs_post = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs_post), std::size(vecs_post)));
    EXPECT_EQ(vecs_pre, vecs_post);
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(n_expected_writable, bfy_buffer_get_space_len(&buf));

    // cleanup
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, drain_empty_buffer) {
    // setup: build an empty buffer
    auto buf = bfy_buffer_init();
    EXPECT_EQ(0, buffer_count_blocks(&buf));
    EXPECT_EQ(0, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(0, bfy_buffer_get_space_len(&buf));

    // drain something
    EXPECT_TRUE(bfy_buffer_drain(&buf, bfy_buffer_drain(&buf, 128)));

    // confirm that nothing changed
    EXPECT_EQ(0, buffer_count_blocks(&buf));
    EXPECT_EQ(0, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(0, bfy_buffer_get_space_len(&buf));

    // cleanup
    bfy_buffer_destruct(&buf);
}
TEST(Buffer, drain_too_much) {
    // setup: build a buffer with two blocks
    auto buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    bfy_buffer_add_readonly(&buf, std::data(str2), std::size(str2));
    auto constexpr n_expected_vecs = 2;
    auto vecs_pre = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs_pre), std::size(vecs_pre)));
    auto constexpr n_expected_readable = std::size(str1) + std::size(str2);
    auto constexpr n_expected_writable = 0;
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(n_expected_writable, bfy_buffer_get_space_len(&buf));

    // drain more than the buffer holds
    EXPECT_TRUE(bfy_buffer_drain(&buf, n_expected_readable * 2));

    // confirm that the buffer is empty
    EXPECT_EQ(0, buffer_count_blocks(&buf));
    EXPECT_EQ(0, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(0, bfy_buffer_get_space_len(&buf));

    // cleanup
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, remove_string) {
    BufferWithReadonlyStrings local;

    // remove it to a string
    size_t len;
    auto* str = bfy_buffer_remove_string(&local.buf, &len);
    EXPECT_EQ(0, bfy_buffer_get_content_len(&local.buf));

    // confirm that the string holds what we expected and is zero-terminated
    EXPECT_EQ(std::size(local.allstrs), len);
    EXPECT_EQ(std::size(local.allstrs), strlen(str));
    EXPECT_EQ(local.allstrs, str);

    // cleanup
    free(str);
}

TEST(Buffer, remove_string_with_empty_buf) {
    // setup: build an empty buffer
    auto buf = bfy_buffer_init();
    std::string const expected_str;
    EXPECT_EQ(std::size(expected_str), bfy_buffer_get_content_len(&buf));

    // remove it to a string
    size_t len;
    auto* str = bfy_buffer_remove_string(&buf, &len);
    EXPECT_EQ(0, bfy_buffer_get_content_len(&buf));

    // confirm it's an empty string
    EXPECT_EQ(std::size(expected_str), len);
    EXPECT_EQ(std::size(expected_str), strlen(str));
    EXPECT_EQ(expected_str, str);

    // cleanup
    free(str);
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, copyout_some) {
    auto local = BufferWithReadonlyStrings {};
    auto const n_readable = bfy_buffer_get_content_len(&local.buf);
    auto const n_writable = bfy_buffer_get_space_len(&local.buf);
    auto const n_blocks = buffer_count_blocks(&local.buf);

    // copy out some of it
    auto array = std::array<char, 128>{};
    auto constexpr n_expected = std::size(local.str1) + 1;
    auto const n_got = bfy_buffer_copyout(&local.buf, std::data(array), n_expected);

    // confirm we got what we expected
    EXPECT_EQ(n_expected, n_got);
    auto const allstrs = local.allstrs;
    EXPECT_TRUE(std::equal(std::data(array), std::data(array)+n_got, std::data(allstrs)));

    // confirm that buffer is unchanged
    EXPECT_EQ(n_readable, bfy_buffer_get_content_len(&local.buf));
    EXPECT_EQ(n_writable, bfy_buffer_get_space_len(&local.buf));
    EXPECT_EQ(n_blocks, buffer_count_blocks(&local.buf));
}

TEST(Buffer, copyout_all) {
    auto local = BufferWithReadonlyStrings {};
    auto const n_readable = bfy_buffer_get_content_len(&local.buf);
    auto const n_writable = bfy_buffer_get_space_len(&local.buf);
    auto const n_blocks = buffer_count_blocks(&local.buf);

    // copy out some of it
    auto array = std::array<char, 128>{};
    auto const n_expected = std::size(local.allstrs);
    auto const n_got = bfy_buffer_copyout(&local.buf, std::data(array), SIZE_MAX);

    // confirm we got what we expected
    EXPECT_EQ(n_expected, n_got);
    auto const allstrs = local.allstrs;
    EXPECT_TRUE(std::equal(std::data(array), std::data(array)+n_got, std::data(allstrs)));

    // confirm that buffer is unchanged
    EXPECT_EQ(n_readable, bfy_buffer_get_content_len(&local.buf));
    EXPECT_EQ(n_writable, bfy_buffer_get_space_len(&local.buf));
    EXPECT_EQ(n_blocks, buffer_count_blocks(&local.buf));
}

TEST(Buffer, copyout_none) {
    auto local = BufferWithReadonlyStrings {};
    auto const n_readable = bfy_buffer_get_content_len(&local.buf);
    auto const n_writable = bfy_buffer_get_space_len(&local.buf);
    auto const n_blocks = buffer_count_blocks(&local.buf);

    // copy out some of it
    auto array = std::array<char, 64>{};
    auto constexpr n_expected = 0;
    auto const n_got = bfy_buffer_copyout(&local.buf, std::data(array), n_expected);

    // confirm we got what we expected
    EXPECT_EQ(n_expected, n_got);

    // confirm that buffer is unchanged
    EXPECT_EQ(n_readable, bfy_buffer_get_content_len(&local.buf));
    EXPECT_EQ(n_writable, bfy_buffer_get_space_len(&local.buf));
    EXPECT_EQ(n_blocks, buffer_count_blocks(&local.buf));
}

/// hton, ntoh functions

TEST(Buffer, endian_16) {
    BufferWithLocalArray<64> local;
    auto const in = uint16_t { 1 };
    auto const expected = htobe16(in);
    auto out = std::remove_cv_t<decltype(in)> {};
    EXPECT_TRUE(bfy_buffer_add_hton_u16(&local.buf, in));
    EXPECT_TRUE(std::equal(std::data(local.array), std::data(local.array)+sizeof(expected), reinterpret_cast<char const*>(&expected)));
    EXPECT_TRUE(bfy_buffer_remove_ntoh_u16(&local.buf, &out));
    EXPECT_EQ(in, out);
}

TEST(Buffer, endian_32) {
    BufferWithLocalArray<64> local;
    auto const in = uint32_t { 1 };
    auto const expected = htobe32(in);
    auto out = std::remove_cv_t<decltype(in)> {};
    EXPECT_TRUE(bfy_buffer_add_hton_u32(&local.buf, in));
    EXPECT_TRUE(std::equal(std::data(local.array), std::data(local.array)+sizeof(expected), reinterpret_cast<char const*>(&expected)));
    EXPECT_TRUE(bfy_buffer_remove_ntoh_u32(&local.buf, &out));
    EXPECT_EQ(in, out);
}

TEST(Buffer, endian_64) {
    BufferWithLocalArray<64> local;
    auto const in = uint64_t { 1 };
    auto const expected = htobe64(in);
    auto out = std::remove_cv_t<decltype(in)> {};
    EXPECT_TRUE(bfy_buffer_add_hton_u64(&local.buf, in));
    EXPECT_TRUE(std::equal(std::data(local.array), std::data(local.array)+sizeof(expected), reinterpret_cast<char const*>(&expected)));
    EXPECT_TRUE(bfy_buffer_remove_ntoh_u64(&local.buf, &out));
    EXPECT_EQ(in, out);
}

TEST(Buffer, add_buffer) {
    auto a = BufferWithReadonlyStrings {};
    auto b = BufferWithReadonlyStrings {};
    auto const n_expected_vecs = buffer_count_blocks(&a.buf) + buffer_count_blocks(&b.buf);
    auto const expected_size = std::size(a.allstrs) + std::size(b.allstrs);

    auto buf = bfy_buffer_init();
    EXPECT_TRUE(bfy_buffer_add_buffer(&buf, &a.buf));
    EXPECT_TRUE(bfy_buffer_add_buffer(&buf, &b.buf));
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, NULL, 0));
    EXPECT_EQ(expected_size, bfy_buffer_get_content_len(&buf));

    auto const str = buffer_remove_string(&buf);
    EXPECT_EQ(expected_size, std::size(str));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add_empty_buffer) {
    auto a = BufferWithReadonlyStrings {};
    auto buf = bfy_buffer_init();

    auto const pre_blocks_a = buffer_get_blocks(&a.buf);
    auto const pre_blocks_b = buffer_get_blocks(&buf);
    EXPECT_TRUE(bfy_buffer_add_buffer(&a.buf, &buf));
    EXPECT_EQ(pre_blocks_a, buffer_get_blocks(&a.buf));
    EXPECT_EQ(pre_blocks_b, buffer_get_blocks(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, remove_empty_buffer) {
    auto a = BufferWithReadonlyStrings {};
    auto buf = bfy_buffer_init();

    auto const pre_blocks_a = buffer_get_blocks(&a.buf);
    auto const pre_blocks_b = buffer_get_blocks(&buf);

    EXPECT_TRUE(bfy_buffer_remove_buffer(&a.buf, &buf, 0));
    EXPECT_EQ(pre_blocks_a, buffer_get_blocks(&a.buf));
    EXPECT_EQ(pre_blocks_b, buffer_get_blocks(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, remove_buffer_on_block_boundary) {
    auto a = BufferWithReadonlyStrings {};
    auto b = BufferWithReadonlyStrings {};

    auto const pre_blocks_a = buffer_get_blocks(&a.buf);
    auto const pre_blocks_b = buffer_get_blocks(&b.buf);

    // remove part of the buffer on what we know is a block boundary
    auto const n_remove = pre_blocks_a.front().iov_len;
    EXPECT_TRUE(bfy_buffer_remove_buffer(&a.buf, &b.buf, n_remove));

    // confirm that the block was just moved over verbatim
    auto expected_blocks_a = std::vector<bfy_iovec> { pre_blocks_a };
    auto expected_blocks_b = std::vector<bfy_iovec> { pre_blocks_b };
    expected_blocks_b.insert(std::end(expected_blocks_b), expected_blocks_a.front());
    expected_blocks_a.erase(std::begin(expected_blocks_a));
    EXPECT_EQ(expected_blocks_a, buffer_get_blocks(&a.buf));
    EXPECT_EQ(expected_blocks_b, buffer_get_blocks(&b.buf));
}

TEST(Buffer, remove_part_of_first_block) {
    auto a = BufferWithReadonlyStrings {};
    auto buf = bfy_buffer_init();

    auto const pre_contents_a = buffer_copyout(&a.buf);
    auto const pre_contents_b = buffer_copyout(&buf);
    auto const pre_blocks_a = buffer_get_blocks(&a.buf);
    auto const pre_blocks_b = buffer_get_blocks(&buf);

    // remove half of the first buffer so that we know we're
    // forcing a block to be split in half
    auto const n_remove = pre_blocks_a.front().iov_len / 2;
    EXPECT_TRUE(bfy_buffer_remove_buffer(&a.buf, &buf, n_remove));

    // confirm that each buffer's contents are what we expect
    auto expected_contents_a = std::vector<char> { pre_contents_a };
    auto expected_contents_b = std::vector<char> { pre_contents_b };
    expected_contents_b.insert(std::end(expected_contents_b), std::begin(pre_contents_a), std::begin(pre_contents_a)+n_remove);
    expected_contents_a.erase(std::begin(expected_contents_a), std::begin(expected_contents_a)+n_remove);
    EXPECT_EQ(expected_contents_a, buffer_copyout(&a.buf));
    EXPECT_EQ(expected_contents_b, buffer_copyout(&buf));

    // confirm that the target buffer got a single new block
    EXPECT_EQ(std::size(pre_blocks_a), buffer_count_blocks(&a.buf));
    EXPECT_EQ(std::size(pre_blocks_b) + 1, buffer_count_blocks(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, remove_nothing_from_empty_buf) {
    // setup: two empty buffers
    auto a = bfy_buffer_init();
    auto b = bfy_buffer_init();

    // take a snapshot of the precondition state
    auto const pre_contents_a = buffer_copyout(&a);
    auto const pre_contents_b = buffer_copyout(&b);
    auto const pre_blocks_a = buffer_get_blocks(&a);
    auto const pre_blocks_b = buffer_get_blocks(&b);

    // remove nothing
    EXPECT_TRUE(bfy_buffer_remove_buffer(&a, &b, 0));

    // confirm that nothing changed
    EXPECT_EQ(pre_contents_a, buffer_copyout(&a));
    EXPECT_EQ(pre_contents_b, buffer_copyout(&b));
    EXPECT_EQ(pre_blocks_a, buffer_get_blocks(&a));
    EXPECT_EQ(pre_blocks_b, buffer_get_blocks(&b));

    // cleanup
    bfy_buffer_destruct(&b);
    bfy_buffer_destruct(&a);
}

TEST(Buffer, peek_space_with_free_space) {
    auto array = std::array<char, 64> {};
    auto buf = bfy_buffer_init_unmanaged(std::data(array), std::size(array));
    EXPECT_EQ(std::size(array), bfy_buffer_get_space_len(&buf));

    auto const io = bfy_buffer_peek_space(&buf);
    EXPECT_EQ(std::data(array), io.iov_base);
    EXPECT_EQ(std::size(array), io.iov_len);

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, peek_space_with_readonly) {
    auto array = std::array<char, 64> {};
    auto buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(array), std::size(array));
    EXPECT_EQ(1, buffer_count_blocks(&buf));

    auto const io = bfy_buffer_peek_space(&buf);
    EXPECT_EQ(std::end(array), io.iov_base);
    EXPECT_EQ(0, io.iov_len);

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, reserve_space) {
    auto buf = bfy_buffer_init();

    // confirm that reserve_space() returns data
    auto const n_wanted = 4096;
    auto const io = bfy_buffer_reserve_space(&buf, n_wanted);
    EXPECT_NE(nullptr, io.iov_base);
    EXPECT_LE(n_wanted, io.iov_len);

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, commit_space) {
    // setup pt 1: create a buffer
    auto buf = bfy_buffer_init();
    EXPECT_EQ(0, bfy_buffer_get_content_len(&buf));

    // setup pt 2: reserve space and write into it
    auto constexpr str = std::string_view { "Lorem ipsum dolor sit amet" };
    auto const io = bfy_buffer_reserve_space(&buf, std::size(str));
    auto const precommit_space = bfy_buffer_get_space_len(&buf);
    EXPECT_NE(nullptr, io.iov_base);
    EXPECT_LE(std::size(str), io.iov_len);
    memcpy(io.iov_base, std::data(str), std::size(str));

    // confirm that the space can be committed
    EXPECT_TRUE(bfy_buffer_commit_space(&buf, std::size(str)));

    // confirm that the committed space is now readable content
    EXPECT_EQ(std::size(str), bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(precommit_space - std::size(str), bfy_buffer_get_space_len(&buf));
    EXPECT_EQ(str, buffer_remove_string(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, reset) {
    auto constexpr n_bytes = 64;
    auto array = std::array<char, n_bytes> {};
    auto buf = bfy_buffer_init_unmanaged(std::data(array), std::size(array));

    auto constexpr str = std::string_view { "Lorem ipsum dolor sit amet" };
    EXPECT_TRUE(bfy_buffer_add(&buf, std::data(str), std::size(str)));
    EXPECT_EQ(std::size(str), bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(n_bytes - std::size(str), bfy_buffer_get_space_len(&buf));
    bfy_buffer_reset(&buf);
    EXPECT_EQ(0, bfy_buffer_get_content_len(&buf));
    EXPECT_EQ(n_bytes, bfy_buffer_get_space_len(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add_reference) {
    auto cb = [](void* data, size_t len, void* vdata) {
        auto* iop = reinterpret_cast<bfy_iovec*>(vdata);
        iop->iov_base = data;
        iop->iov_len = len;
    };

    // setup: add the reference to the buffer
    auto buf = bfy_buffer_init();
    auto constexpr str_in = std::string_view { "Lorem ipsum dolor sit amet" };
    auto io = bfy_iovec {};
    EXPECT_TRUE(bfy_buffer_add_reference(&buf, std::data(str_in), std::size(str_in), cb, &io));

    // read remove the buffer's contents into a string
    auto const str = buffer_remove_string(&buf);
    EXPECT_EQ(str_in, str);

    // confirm that the callback was invoked
    EXPECT_EQ(std::data(str_in), io.iov_base);
    EXPECT_EQ(std::size(str_in), io.iov_len);

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add_reference_callback_reached_in_buffer_dtor) {
    auto cb = [](void* data, size_t len, void* vdata) {
        auto* iop = reinterpret_cast<bfy_iovec*>(vdata);
        iop->iov_base = data;
        iop->iov_len = len;
    };

    // setup: add the reference to the buffer
    auto buf = bfy_buffer_init();
    auto constexpr str = std::string_view { "Lorem ipsum dolor sit amet" };
    auto io = bfy_iovec {};
    EXPECT_TRUE(bfy_buffer_add_reference(&buf, std::data(str), std::size(str), cb, &io));

    // confirm that the callback was invoked in the destructor
    bfy_buffer_destruct(&buf);
    EXPECT_EQ(std::data(str), io.iov_base);
    EXPECT_EQ(std::size(str), io.iov_len);
}

TEST(Buffer, add_reference_callback_reached_after_ownership_changed) {
    auto cb = [](void* data, size_t len, void* vdata) {
        auto* iop = reinterpret_cast<bfy_iovec*>(vdata);
        iop->iov_base = data;
        iop->iov_len = len;
    };

    // setup: add the reference to one buffer
    auto src = bfy_buffer_init();
    auto constexpr str = std::string_view { "Lorem ipsum dolor sit amet" };
    auto io = bfy_iovec {};
    EXPECT_TRUE(bfy_buffer_add_reference(&src, std::data(str), std::size(str), cb, &io));
    EXPECT_EQ(std::size(str), bfy_buffer_get_content_len(&src));

    // transfer the contents to another buffer
    auto tgt = bfy_buffer_init();
    fprintf(stderr, "calling bfy_buffer_add_buffer\n");
    bfy_buffer_add_buffer(&tgt, &src);
    fprintf(stderr, "called bfy_buffer_add_buffer\n");
    EXPECT_EQ(0, bfy_buffer_get_content_len(&src));
    EXPECT_EQ(std::size(str), bfy_buffer_get_content_len(&tgt));

    // confirm that transfer did not invoke the unref function
    EXPECT_EQ(0, io.iov_len);
    EXPECT_EQ(nullptr, io.iov_base);

    // confirm that draining the contents from tgt invokes the callback
    bfy_buffer_drain(&tgt, SIZE_MAX);
    EXPECT_EQ(0, bfy_buffer_get_content_len(&tgt));
    EXPECT_EQ(std::size(str), io.iov_len);
    EXPECT_EQ(std::data(str), io.iov_base);

    bfy_buffer_destruct(&tgt);
    bfy_buffer_destruct(&src);
}

#if 0
TEST(Buffer, clear) {
    BFY_HEAP_BUFFER(buf)

    std::string const in = "1234567890";
    bfy_buffer_add(&buf, in.data(), in.length());
    EXPECT_EQ(in.length(), bfy_buffer_get_content_len(&buf));

    bfy_buffer_clear(&buf);
    EXPECT_EQ(0, bfy_buffer_get_content_len(&buf));

    bfy_buffer_destruct(&buf);
}
#endif
