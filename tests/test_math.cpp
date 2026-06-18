#include "core/math.hpp"
#include <boost/test/unit_test.hpp>
#include <unordered_set>

BOOST_AUTO_TEST_SUITE(math_tests)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

BOOST_AUTO_TEST_CASE(vec2f_default_construction)
{
    Vec2f v;
    BOOST_TEST(v.x == 0.F);
    BOOST_TEST(v.y == 0.F);
}

BOOST_AUTO_TEST_CASE(vec2f_addition)
{
    Vec2f a(1.F, 2.F);
    Vec2f b(3.F, 4.F);
    Vec2f r = a + b;
    BOOST_TEST(r.x == 4.F);
    BOOST_TEST(r.y == 6.F);
}

BOOST_AUTO_TEST_CASE(vec2f_subtraction)
{
    Vec2f a(5.F, 3.F);
    Vec2f b(2.F, 1.F);
    Vec2f r = a - b;
    BOOST_TEST(r.x == 3.F);
    BOOST_TEST(r.y == 2.F);
}

BOOST_AUTO_TEST_CASE(vec2f_scalar_multiply)
{
    Vec2f v(2.F, 3.F);
    Vec2f r = v * 3.F;
    BOOST_TEST(r.x == 6.F);
    BOOST_TEST(r.y == 9.F);
}

BOOST_AUTO_TEST_CASE(vec2f_add_assign)
{
    Vec2f v(1.F, 2.F);
    v += Vec2f(3.F, 4.F);
    BOOST_TEST(v.x == 4.F);
    BOOST_TEST(v.y == 6.F);
}

BOOST_AUTO_TEST_CASE(vec2f_sub_assign)
{
    Vec2f v(5.F, 3.F);
    v -= Vec2f(2.F, 1.F);
    BOOST_TEST(v.x == 3.F);
    BOOST_TEST(v.y == 2.F);
}

BOOST_AUTO_TEST_CASE(vec2f_length)
{
    Vec2f v(3.F, 4.F);
    BOOST_TEST(v.length() == 5.F, boost::test_tools::tolerance(0.001F));
}

BOOST_AUTO_TEST_CASE(vec2f_length_zero)
{
    Vec2f v(0.F, 0.F);
    BOOST_TEST(v.length() == 0.F);
}

BOOST_AUTO_TEST_CASE(vec2i_default_construction)
{
    Vec2i v;
    BOOST_TEST(v.x == 0);
    BOOST_TEST(v.y == 0);
}

BOOST_AUTO_TEST_CASE(vec2i_equality)
{
    Vec2i a(1, 2);
    Vec2i b(1, 2);
    Vec2i c(3, 4);
    BOOST_TEST(a == b);
    BOOST_TEST(!(a == c));
    BOOST_TEST(a != c);
}

BOOST_AUTO_TEST_CASE(vec2i_less_than)
{
    Vec2i a(1, 2);
    Vec2i b(1, 3);
    Vec2i c(2, 0);
    BOOST_TEST(a < b);
    BOOST_TEST(a < c);
}

BOOST_AUTO_TEST_CASE(vec2i_hash)
{
    std::hash<Vec2i> hasher;
    Vec2i a(1, 2);
    Vec2i b(1, 2);
    Vec2i c(3, 4);
    BOOST_TEST(hasher(a) == hasher(b));
    BOOST_TEST(hasher(a) != hasher(c));
}

BOOST_AUTO_TEST_CASE(vec2i_in_unordered_set)
{
    std::unordered_set<Vec2i> s;
    s.insert(Vec2i(1, 2));
    s.insert(Vec2i(3, 4));
    BOOST_TEST(s.contains(Vec2i(1, 2)));
    BOOST_TEST(s.contains(Vec2i(3, 4)));
    BOOST_TEST(!s.contains(Vec2i(5, 6)));
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

BOOST_AUTO_TEST_SUITE_END()
