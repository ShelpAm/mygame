#include <boost/test/unit_test.hpp>
#include "core/math.hpp"
#include <unordered_set>

BOOST_AUTO_TEST_SUITE(math_tests)

BOOST_AUTO_TEST_CASE(vec2f_default_construction) {
    Vec2f v;
    BOOST_TEST(v.x == 0.f);
    BOOST_TEST(v.y == 0.f);
}

BOOST_AUTO_TEST_CASE(vec2f_addition) {
    Vec2f a{1.f, 2.f};
    Vec2f b{3.f, 4.f};
    Vec2f r = a + b;
    BOOST_TEST(r.x == 4.f);
    BOOST_TEST(r.y == 6.f);
}

BOOST_AUTO_TEST_CASE(vec2f_subtraction) {
    Vec2f a{5.f, 3.f};
    Vec2f b{2.f, 1.f};
    Vec2f r = a - b;
    BOOST_TEST(r.x == 3.f);
    BOOST_TEST(r.y == 2.f);
}

BOOST_AUTO_TEST_CASE(vec2f_scalar_multiply) {
    Vec2f v{2.f, 3.f};
    Vec2f r = v * 3.f;
    BOOST_TEST(r.x == 6.f);
    BOOST_TEST(r.y == 9.f);
}

BOOST_AUTO_TEST_CASE(vec2f_add_assign) {
    Vec2f v{1.f, 2.f};
    v += Vec2f{3.f, 4.f};
    BOOST_TEST(v.x == 4.f);
    BOOST_TEST(v.y == 6.f);
}

BOOST_AUTO_TEST_CASE(vec2f_sub_assign) {
    Vec2f v{5.f, 3.f};
    v -= Vec2f{2.f, 1.f};
    BOOST_TEST(v.x == 3.f);
    BOOST_TEST(v.y == 2.f);
}

BOOST_AUTO_TEST_CASE(vec2f_length) {
    Vec2f v{3.f, 4.f};
    BOOST_TEST(v.length() == 5.f, boost::test_tools::tolerance(0.001f));
}

BOOST_AUTO_TEST_CASE(vec2f_length_zero) {
    Vec2f v{0.f, 0.f};
    BOOST_TEST(v.length() == 0.f);
}

BOOST_AUTO_TEST_CASE(vec2i_default_construction) {
    Vec2i v;
    BOOST_TEST(v.x == 0);
    BOOST_TEST(v.y == 0);
}

BOOST_AUTO_TEST_CASE(vec2i_equality) {
    Vec2i a{1, 2};
    Vec2i b{1, 2};
    Vec2i c{3, 4};
    BOOST_TEST(a == b);
    BOOST_TEST(!(a == c));
    BOOST_TEST(a != c);
}

BOOST_AUTO_TEST_CASE(vec2i_less_than) {
    Vec2i a{1, 2};
    Vec2i b{1, 3};
    Vec2i c{2, 0};
    BOOST_TEST(a < b);
    BOOST_TEST(a < c);
}

BOOST_AUTO_TEST_CASE(vec2i_hash) {
    std::hash<Vec2i> hasher;
    Vec2i a{1, 2};
    Vec2i b{1, 2};
    Vec2i c{3, 4};
    BOOST_TEST(hasher(a) == hasher(b));
    BOOST_TEST(hasher(a) != hasher(c));
}

BOOST_AUTO_TEST_CASE(vec2i_in_unordered_set) {
    std::unordered_set<Vec2i> s;
    s.insert({1, 2});
    s.insert({3, 4});
    BOOST_TEST(s.contains({1, 2}));
    BOOST_TEST(s.contains({3, 4}));
    BOOST_TEST(!s.contains({5, 6}));
}

BOOST_AUTO_TEST_SUITE_END()
