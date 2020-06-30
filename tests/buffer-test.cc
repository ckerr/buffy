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
#include <numeric>
#include <string_view>
#include <cstring>  // memcmp()

#include "libbuffy/buffer.h"

#include "gtest/gtest.h"

bool operator== (bfy_iovec const& a, struct bfy_iovec const& b) {
  return a.iov_base == b.iov_base && a.iov_len == b.iov_len;
}

TEST(Buffer, init_and_destruct) {
    // test that you can instantiate and destruct a buffer on the stack
    struct bfy_buffer buf = bfy_buffer_init();
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, new_and_free) {
    // test that you can create and destroy a buffer on the heap
    struct bfy_buffer* buf = bfy_buffer_new();
    EXPECT_NE(nullptr, buf);
    EXPECT_EQ(0, bfy_buffer_get_readable_size(buf));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(buf));
    bfy_buffer_free(buf);
}

TEST(Buffer, init_unmanaged) {
    // create a buffer with a stack-managed chunk of memory to use
    auto array = std::array<char, 32>{};
    struct bfy_buffer buf = bfy_buffer_init_unmanaged(std::data(array), std::size(array));
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(std::size(array), bfy_buffer_get_writable_size(&buf));

    // Write something to the buffer and peek it.
    // That written data should be readable and located in `array`.
    auto constexpr str = std::string_view("Hello There!");
    auto rv = bfy_buffer_add(&buf, std::data(str), std::size(str));
    EXPECT_TRUE(rv);
    auto constexpr n_expected_vecs = 1;
    auto vecs = std::array<bfy_iovec, n_expected_vecs>{};
    rv = bfy_buffer_peek_all(&buf, std::data(vecs), std::size(vecs));
    EXPECT_EQ(n_expected_vecs, rv);
    auto const expected_vecs = std::array<bfy_iovec, n_expected_vecs>{ { std::data(array), std::size(str) } };
    EXPECT_EQ(expected_vecs, vecs);
    EXPECT_EQ(std::size(array) - std::size(str), bfy_buffer_get_writable_size(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add_readonly) {
    struct bfy_buffer buf = bfy_buffer_init();

    // add a read-only string.
    // it should show up as readable in the buffer.
    auto constexpr str1 = std::string_view{"Hello, "};
    auto rv = bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    EXPECT_TRUE(rv);
    auto expected_readable_size = std::size(str1);
    EXPECT_EQ(expected_readable_size, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));

    // add another, to test that buffer knows the end block is readonly
    // and will allocate another block.
    auto constexpr str2 = std::string_view{"World!"};
    rv = bfy_buffer_add_readonly(&buf, std::data(str2), std::size(str2));
    EXPECT_TRUE(rv);
    expected_readable_size += std::size(str2);
    EXPECT_EQ(expected_readable_size, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, peek) {
    struct bfy_buffer buf = bfy_buffer_init();

    auto constexpr pt1 = std::string_view("Hello");
    auto constexpr pt2 = std::string_view("World");
    auto rv = bfy_buffer_add_readonly(&buf, std::data(pt1), std::size(pt1));
    EXPECT_TRUE(rv);
    rv = bfy_buffer_add_readonly(&buf, std::data(pt2), std::size(pt2));
    EXPECT_TRUE(rv);

    EXPECT_EQ(1, bfy_buffer_peek(&buf, std::size(pt1), nullptr, 0));
    EXPECT_EQ(2, bfy_buffer_peek(&buf, std::size(pt1)+1, nullptr, 0));
    EXPECT_EQ(2, bfy_buffer_peek(&buf, std::size(pt1)+std::size(pt2), nullptr, 0));
    EXPECT_EQ(2, bfy_buffer_peek(&buf, std::size(pt1)+std::size(pt2)+1, nullptr, 0));
    EXPECT_EQ(2, bfy_buffer_peek_all(&buf, nullptr, 0));

    const bfy_iovec JunkVec = {
       .iov_base = reinterpret_cast<void*>(0xBADF00D),
       .iov_len = 666
    };

    // test that a single-vec peek works
    auto vecs = std::array<struct bfy_iovec, 4>{};
    std::fill(std::begin(vecs), std::end(vecs), JunkVec);
    auto n_vecs = bfy_buffer_peek(&buf, std::size(pt1), std::data(vecs), std::size(vecs));
    EXPECT_EQ(1, n_vecs);
    EXPECT_EQ(std::data(pt1), vecs[0].iov_base);
    EXPECT_EQ(std::size(pt1), vecs[0].iov_len);
    EXPECT_EQ(JunkVec, vecs[1]);

    // test that a single-vec peek with a null iovec works
    rv = bfy_buffer_peek(&buf, std::size(pt1), nullptr, 0);
    EXPECT_EQ(1, rv);

    // test that a multivec peek works
    std::fill(std::begin(vecs), std::end(vecs), JunkVec);
    n_vecs = bfy_buffer_peek(&buf, std::size(pt1)+1, std::data(vecs), std::size(vecs));
    EXPECT_EQ(2, n_vecs);
    EXPECT_EQ(std::data(pt1), vecs[0].iov_base);
    EXPECT_EQ(std::size(pt1), vecs[0].iov_len);
    EXPECT_EQ(std::data(pt2), vecs[1].iov_base);
    EXPECT_EQ(1, vecs[1].iov_len);
    EXPECT_EQ(JunkVec, vecs[2]);

    // test that a multivec peek with a null iovec works
    rv = bfy_buffer_peek(&buf, std::size(pt1), nullptr, 0);
    EXPECT_TRUE(rv);

    // test that the number extents needed is returned
    // even if it's greater than the number of extents passed in
    std::fill(std::begin(vecs), std::end(vecs), JunkVec);
    n_vecs = bfy_buffer_peek(&buf, std::size(pt1)+std::size(pt2), std::data(vecs), 1);
    EXPECT_EQ(2, n_vecs);
    EXPECT_EQ(std::data(pt1), vecs[0].iov_base);
    EXPECT_EQ(std::size(pt1), vecs[0].iov_len);
    EXPECT_EQ(JunkVec, vecs[2]);

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add) {
    struct bfy_buffer buf = bfy_buffer_init();

    char constexpr ch = 'y';
    auto const rv = bfy_buffer_add_ch(&buf, ch);
    EXPECT_TRUE(rv);
    EXPECT_EQ(1, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(1, bfy_buffer_peek_all(&buf, nullptr, 0));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, add_after_readonly) {
    auto pt1 = std::array<char, 6>{ 'H', 'e', 'l', 'l', 'o', ' ' };
    auto pt2 = std::array<char, 5>{ 'T', 'h', 'e', 'r', 'e' };

    // add the first part as read-only
    bfy_buffer buf = bfy_buffer_init();
    auto rv = bfy_buffer_add_readonly(&buf, std::data(pt1), std::size(pt1));
    EXPECT_TRUE(rv);

    // add the second part as writable
    rv = bfy_buffer_add(&buf, std::data(pt2), std::size(pt2));
    EXPECT_TRUE(rv);
    EXPECT_EQ(std::size(pt1) + std::size(pt2), bfy_buffer_get_readable_size(&buf));
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
    // create a buffer with a stack-based playground
    auto array = std::array<char, 64>{};
    bfy_buffer buf = bfy_buffer_init_unmanaged(std::data(array), std::size(array));
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(sizeof(array), bfy_buffer_get_writable_size(&buf));

    // printf into the buffer
    auto const rv = bfy_buffer_add_printf(&buf, "%s, %s!", "Hello", "World");
    EXPECT_TRUE(rv);
    EXPECT_EQ(1, bfy_buffer_peek_all(&buf, nullptr, 0));

    // confirm that the string was written into the memory used by the buffer
    auto constexpr expected = std::string_view { "Hello, World!" };
    EXPECT_EQ(expected.length(), bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(sizeof(array) - expected.length(), bfy_buffer_get_writable_size(&buf));
    EXPECT_EQ(expected, std::data(array));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, make_contiguous_when_only_one_block) {
    auto array = std::array<char, 64>{};
    bfy_buffer buf = bfy_buffer_init_unmanaged(std::data(array), std::size(array));

    auto constexpr str1 = std::string_view { "General" };
    bfy_buffer_add(&buf, std::data(str1), std::size(str1));

    // confirm adding str1 fit inside the existing block
    auto constexpr n_expected_vecs = 1;
    auto vecs1 = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs1), std::size(vecs1)));
    EXPECT_EQ(std::data(array), vecs1[0].iov_base);
    EXPECT_EQ(std::size(str1), vecs1[0].iov_len);

    // confirm that making contiguous changes nothing
    // because it was already a single block
    auto* rv = bfy_buffer_make_all_contiguous(&buf);
    auto vecs2 = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs2), std::size(vecs2)));
    EXPECT_EQ(vecs1[0].iov_base, vecs2[0].iov_base);

    // confirm that make_contiguous() returned the right value
    EXPECT_EQ(vecs1[0].iov_base, rv);

    bfy_buffer_destruct(&buf);
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
    bfy_buffer buf = bfy_buffer_init();
    auto const action = [&buf](auto const& str){bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));};
    std::for_each(begin, end, action);
    auto const n_readable = bfy_buffer_get_readable_size(&buf);
    auto const acc = [](auto& acc, auto const& str){return acc + std::size(str);};
    auto const n_readable_expected = std::accumulate(begin, end, size_t{}, acc);
    EXPECT_EQ(n_blocks_in, bfy_buffer_peek_all(&buf, nullptr, 0));
    EXPECT_EQ(n_readable_expected, n_readable);

    // confirm that nothing happens when you request a block
    // that is already contiguous
    auto constexpr n_contiguous = std::size(strs.front());
    for (int i=0; i < n_contiguous; ++i) {
        auto const* rv = bfy_buffer_make_contiguous(&buf, n_contiguous);
        EXPECT_EQ(rv, std::data(strs.front()));
        EXPECT_EQ(n_blocks_in, bfy_buffer_peek_all(&buf, nullptr, 0));
        EXPECT_EQ(n_readable_expected, bfy_buffer_get_readable_size(&buf));
    }

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, make_contiguous_when_readonly_blocks) {
    auto constexpr n_blocks_in = 2;
    auto constexpr begin = std::begin(strs);
    auto constexpr end = begin + n_blocks_in;

    // setup: build a block with some read-only blocks
    bfy_buffer buf = bfy_buffer_init();
    auto const action = [&buf](auto const& str){bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));};
    std::for_each(begin, end, action);
    auto const n_readable = bfy_buffer_get_readable_size(&buf);
    auto const n_readable_expected = std::accumulate(begin, end, size_t{}, [](auto& acc, auto const& str){return acc + std::size(str);});
    EXPECT_EQ(n_readable_expected, n_readable);
    EXPECT_EQ(n_blocks_in, bfy_buffer_peek_all(&buf, nullptr, 0));

    // confirm that make_contiguous put 'em in one block
    auto const* rv = bfy_buffer_make_all_contiguous(&buf);
    EXPECT_EQ(1, bfy_buffer_peek_all(&buf, nullptr, 0));
    EXPECT_EQ(n_readable_expected, bfy_buffer_get_readable_size(&buf));
    std::for_each(begin, end, [this, rv](auto const& str){ EXPECT_NE(std::data(str), rv); });

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, make_contiguous_when_aligned_with_block) {
    auto constexpr n_blocks_in = 3;
    auto constexpr begin = std::begin(strs);
    auto constexpr end = begin + n_blocks_in;

    // setup: build a block with three read-only blocks
    bfy_buffer buf = bfy_buffer_init();
    auto const action = [&buf](auto const& str){bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));};
    std::for_each(begin, end, action);
    auto const n_readable = bfy_buffer_get_readable_size(&buf);
    auto const acc = [](auto& acc, auto const& str){return acc + std::size(str);};
    auto const n_readable_expected = std::accumulate(begin, end, size_t{}, acc);
    EXPECT_EQ(n_readable_expected, n_readable);
    EXPECT_EQ(n_blocks_in, bfy_buffer_peek_all(&buf, nullptr, 0));

    // try to make the first two blocks contiguous
    auto constexpr n_blocks_contiguous = n_blocks_in - 1;
    auto const n_contiguous = std::accumulate(begin, begin+n_blocks_contiguous, size_t{}, acc);
    bfy_buffer_make_contiguous(&buf, n_contiguous);
    auto vecs = std::array<bfy_iovec, n_blocks_contiguous>{};
    auto const n_vecs = bfy_buffer_peek_all(&buf, std::data(vecs), n_blocks_contiguous);
    EXPECT_EQ(n_blocks_contiguous, n_vecs);
    EXPECT_EQ(n_contiguous, vecs[0].iov_len);
    EXPECT_NE(std::data(str1), vecs[0].iov_base);
    EXPECT_EQ(n_readable_expected - n_contiguous, vecs[1].iov_len);
    EXPECT_EQ(std::data(str3), vecs[1].iov_base);
    EXPECT_EQ(n_readable_expected, bfy_buffer_get_readable_size(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, make_contiguous_when_not_aligned_with_block) {
    auto constexpr n_blocks_in = 3;
    auto const begin = std::begin(strs);
    auto const end = begin + n_blocks_in;

    // setup: build a block with three read-only blocks
    bfy_buffer buf = bfy_buffer_init();
    auto const action = [&buf](auto const& str){bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));};
    std::for_each(begin, end, action);
    auto const n_readable = bfy_buffer_get_readable_size(&buf);
    auto const acc = [](auto& acc, auto const& str){return acc + std::size(str);};
    auto const n_readable_expected = std::accumulate(begin, end, size_t{}, acc);
    EXPECT_EQ(n_readable_expected, n_readable);
    EXPECT_EQ(n_blocks_in, bfy_buffer_peek_all(&buf, nullptr, 0));

    // try to make 2-and-some blocks contiguous
    auto const n_contiguous = n_readable_expected - 1;
    auto constexpr n_expected_vecs = 2;
    auto const expected_lengths = std::array<size_t, n_expected_vecs> { n_contiguous, n_readable_expected - n_contiguous };
    bfy_buffer_make_contiguous(&buf, n_contiguous);
    auto vecs = std::array<bfy_iovec, n_expected_vecs>{};
    auto const rv = bfy_buffer_peek_all(&buf, std::data(vecs), n_expected_vecs);
    EXPECT_EQ(n_expected_vecs, rv);
    for (int i=0; i<n_expected_vecs; ++i) {
      EXPECT_EQ(expected_lengths[i], vecs[i].iov_len);
    }

    EXPECT_EQ(n_readable, bfy_buffer_get_readable_size(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, ensure_writable_when_already_available) {
    auto memory = std::array<char, 128>{};
    bfy_buffer buf = bfy_buffer_init_unmanaged(std::data(memory), std::size(memory));

    for (size_t i=0; i < std::size(memory); ++i) {
        auto const rv = bfy_buffer_ensure_writable_size(&buf, i);
        EXPECT_TRUE(rv);
        EXPECT_EQ(std::size(memory), bfy_buffer_get_writable_size(&buf));
        EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    }

    bfy_buffer_destruct(&buf);
}

TEST(buffer, ensure_writable_when_not_enough_available) {
    auto memory = std::array<char, 128>{};
    bfy_buffer buf = bfy_buffer_init_unmanaged(std::data(memory), std::size(memory));

    auto const n_wanted = std::size(memory) * 2;
    auto const rv = bfy_buffer_ensure_writable_size(&buf, n_wanted);
    EXPECT_TRUE(rv);
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    EXPECT_LE(n_wanted, bfy_buffer_get_writable_size(&buf));

    bfy_buffer_destruct(&buf);

}

TEST(Buffer, ensure_writable_when_readonly) {
    bfy_buffer buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));
    EXPECT_EQ(std::size(str1), bfy_buffer_get_readable_size(&buf));

    auto const n_available = 10;  // arbitrary
    auto const rv = bfy_buffer_ensure_writable_size(&buf, n_available);
    EXPECT_TRUE(rv);
    EXPECT_EQ(std::size(str1), bfy_buffer_get_readable_size(&buf));
    EXPECT_LE(n_available, bfy_buffer_get_writable_size(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, drain_on_block_boundary) {
    // setup: build a buffer with two blocks
    bfy_buffer buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    bfy_buffer_add_readonly(&buf, std::data(str2), std::size(str2));
    EXPECT_EQ(2, bfy_buffer_peek_all(&buf, nullptr, 0));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));
    EXPECT_EQ(std::size(str1) + std::size(str2), bfy_buffer_get_readable_size(&buf));

    // drain the first block -- the second one should remain
    EXPECT_TRUE(bfy_buffer_drain(&buf, std::size(str1)));
    auto constexpr n_expected_vecs = 1;
    auto vecs = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs), std::size(vecs)));
    EXPECT_EQ(std::data(str2), vecs.front().iov_base);
    EXPECT_EQ(std::size(str2), vecs.front().iov_len);
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));
    EXPECT_EQ(std::size(str2), bfy_buffer_get_readable_size(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, drain_part_of_first_block) {
    // setup: build a buffer with two blocks
    bfy_buffer buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    bfy_buffer_add_readonly(&buf, std::data(str2), std::size(str2));
    EXPECT_EQ(2, bfy_buffer_peek_all(&buf, nullptr, 0));
    auto expected_readable_size = std::size(str1) + std::size(str2);
    auto constexpr expected_writable_size = 0;
    EXPECT_EQ(expected_readable_size, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(expected_writable_size, bfy_buffer_get_writable_size(&buf));

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
    EXPECT_EQ(expected_readable_size, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(expected_writable_size, bfy_buffer_get_writable_size(&buf));

    bfy_buffer_destruct(&buf);
}

TEST(Buffer, drain_zero) {
    // setup: build a buffer with two blocks
    bfy_buffer buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    bfy_buffer_add_readonly(&buf, std::data(str2), std::size(str2));
    auto constexpr n_expected_vecs = 2;
    auto vecs_pre = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs_pre), std::size(vecs_pre)));
    auto constexpr n_expected_readable = std::size(str1) + std::size(str2);
    auto constexpr n_expected_writable = 0;
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(n_expected_writable, bfy_buffer_get_writable_size(&buf));

    // remove nothing
    EXPECT_TRUE(bfy_buffer_drain(&buf, 0));

    // confirm that nothing changed
    auto vecs_post = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs_post), std::size(vecs_post)));
    EXPECT_EQ(vecs_pre, vecs_post);
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(n_expected_writable, bfy_buffer_get_writable_size(&buf));

    // cleanup
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, drain_empty_buffer) {
    // setup: build an empty buffer
    bfy_buffer buf = bfy_buffer_init();
    EXPECT_EQ(0, bfy_buffer_peek_all(&buf, nullptr, 0));
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));

    // drain something
    EXPECT_TRUE(bfy_buffer_drain(&buf, bfy_buffer_drain(&buf, 128)));

    // confirm that nothing changed
    EXPECT_EQ(0, bfy_buffer_peek_all(&buf, nullptr, 0));
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));

    // cleanup
    bfy_buffer_destruct(&buf);
}
TEST(Buffer, drain_too_much) {
    // setup: build a buffer with two blocks
    bfy_buffer buf = bfy_buffer_init();
    bfy_buffer_add_readonly(&buf, std::data(str1), std::size(str1));
    bfy_buffer_add_readonly(&buf, std::data(str2), std::size(str2));
    auto constexpr n_expected_vecs = 2;
    auto vecs_pre = std::array<bfy_iovec, n_expected_vecs>{};
    EXPECT_EQ(n_expected_vecs, bfy_buffer_peek_all(&buf, std::data(vecs_pre), std::size(vecs_pre)));
    auto constexpr n_expected_readable = std::size(str1) + std::size(str2);
    auto constexpr n_expected_writable = 0;
    EXPECT_EQ(n_expected_readable, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(n_expected_writable, bfy_buffer_get_writable_size(&buf));

    // drain more than the buffer holds
    EXPECT_TRUE(bfy_buffer_drain(&buf, n_expected_readable * 2));

    // confirm that the buffer is empty
    EXPECT_EQ(0, bfy_buffer_peek_all(&buf, nullptr, 0));
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(0, bfy_buffer_get_writable_size(&buf));

    // cleanup
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, remove_string) {
    // setup: build a buffer with multiple blocks
    bfy_buffer buf = bfy_buffer_init();
    std::string expected_str;
    for (auto const& str : strs) {
      bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));
      expected_str += str;
    }
    EXPECT_EQ(std::size(expected_str), bfy_buffer_get_readable_size(&buf));

    // remove it to a string
    size_t len;
    auto* str = bfy_buffer_remove_string(&buf, &len);
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));

    // confirm that the string holds what we expected and is zero-terminated
    EXPECT_EQ(std::size(expected_str), len);
    EXPECT_EQ(std::size(expected_str), strlen(str));
    EXPECT_EQ(expected_str, str);

    // cleanup
    free(str);
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, remove_string_with_empty_buf) {
    // setup: build an empty buffer
    bfy_buffer buf = bfy_buffer_init();
    std::string const expected_str;
    EXPECT_EQ(std::size(expected_str), bfy_buffer_get_readable_size(&buf));

    // remove it to a string
    size_t len;
    auto* str = bfy_buffer_remove_string(&buf, &len);
    EXPECT_EQ(0, bfy_buffer_get_readable_size(&buf));

    // confirm it's an empty string
    EXPECT_EQ(std::size(expected_str), len);
    EXPECT_EQ(std::size(expected_str), strlen(str));
    EXPECT_EQ(expected_str, str);

    // cleanup
    free(str);
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, copyout_some) {
    bfy_buffer buf = bfy_buffer_init();
    std::string expected_out;
    for (auto const& str : strs) {
        bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));
        expected_out += str;
    }
    auto const n_readable = bfy_buffer_get_readable_size(&buf);
    auto const n_writable = bfy_buffer_get_writable_size(&buf);
    auto const n_blocks = bfy_buffer_peek_all(&buf, nullptr, 0);

    // copy out some of it
    auto array = std::array<char, 64>{};
    auto constexpr n_expected = std::size(str1) + 1;
    auto const n_got = bfy_buffer_copyout(&buf, std::data(array), n_expected);

    // confirm we got what we expected
    EXPECT_EQ(n_expected, n_got);
    EXPECT_FALSE(memcmp(std::data(expected_out.substr(0, n_got)), std::data(array), n_expected));

    // confirm that buffer is unchanged
    EXPECT_EQ(n_readable, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(n_writable, bfy_buffer_get_writable_size(&buf));
    EXPECT_EQ(n_blocks, bfy_buffer_peek_all(&buf, nullptr, 0));

    // cleanup
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, copyout_all) {
    bfy_buffer buf = bfy_buffer_init();
    std::string expected_out;
    for (auto const& str : strs) {
        bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));
        expected_out += str;
    }
    auto const n_readable = bfy_buffer_get_readable_size(&buf);
    auto const n_writable = bfy_buffer_get_writable_size(&buf);
    auto const n_blocks = bfy_buffer_peek_all(&buf, nullptr, 0);

    // copy out some of it
    auto array = std::array<char, 64>{};
    auto const n_expected = std::size(expected_out);
    auto const n_got = bfy_buffer_copyout(&buf, std::data(array), SIZE_MAX);

    // confirm we got what we expected
    EXPECT_EQ(n_expected, n_got);
    EXPECT_FALSE(memcmp(std::data(expected_out), std::data(array), n_got));

    // confirm that buffer is unchanged
    EXPECT_EQ(n_readable, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(n_writable, bfy_buffer_get_writable_size(&buf));
    EXPECT_EQ(n_blocks, bfy_buffer_peek_all(&buf, nullptr, 0));

    // cleanup
    bfy_buffer_destruct(&buf);
}

TEST(Buffer, copyout_none) {
    bfy_buffer buf = bfy_buffer_init();
    std::string expected_out;
    for (auto const& str : strs) {
        bfy_buffer_add_readonly(&buf, std::data(str), std::size(str));
        expected_out += str;
    }
    auto const n_readable = bfy_buffer_get_readable_size(&buf);
    auto const n_writable = bfy_buffer_get_writable_size(&buf);
    auto const n_blocks = bfy_buffer_peek_all(&buf, nullptr, 0);

    // copy out some of it
    auto array = std::array<char, 64>{};
    auto constexpr n_expected = 0;
    auto const n_got = bfy_buffer_copyout(&buf, std::data(array), n_expected);

    // confirm we got what we expected
    EXPECT_EQ(n_expected, n_got);

    // confirm that buffer is unchanged
    EXPECT_EQ(n_readable, bfy_buffer_get_readable_size(&buf));
    EXPECT_EQ(n_writable, bfy_buffer_get_writable_size(&buf));
    EXPECT_EQ(n_blocks, bfy_buffer_peek_all(&buf, nullptr, 0));

    // cleanup
    bfy_buffer_destruct(&buf);
}

#if 0
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
    char* out = nullptr;
    size_t outlen = 0;
    rv = bfy_buffer_take_string(&buf, &out, &outlen);
    EXPECT_EQ(-1, rv);
    EXPECT_EQ(ENOMEM, errno);
    EXPECT_EQ(Limit, outlen);
    EXPECT_TRUE(!memcmp(out, (first_half+second_half).data(), Limit));
    EXPECT_EQ(array, out);
    EXPECT_EQ(Fill, array[Limit]);
}
#endif
