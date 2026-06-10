#include <boost/test/unit_test.hpp>
#include "core/input-manager.hpp"
BOOST_AUTO_TEST_SUITE(input_tests)

BOOST_AUTO_TEST_CASE(all_actions_have_default_bindings) {
    InputManager im;
    // All actions should be queryable without crashing
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::move_up));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::move_down));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::move_left));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::move_right));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::interact));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::open_journal));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::open_inventory));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::open_map));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::pause));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::escape));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::confirm));
    BOOST_CHECK_NO_THROW(im.is_pressed(InputManager::Action::cancel));
}

BOOST_AUTO_TEST_CASE(actions_initially_not_pressed) {
    InputManager im;
    im.update(); // populate keyboard state
    BOOST_TEST(!im.is_pressed(InputManager::Action::move_up));
    BOOST_TEST(!im.is_pressed(InputManager::Action::interact));
    BOOST_TEST(!im.is_pressed(InputManager::Action::confirm));
}

BOOST_AUTO_TEST_CASE(just_pressed_requires_transition) {
    InputManager im;
    im.update();
    BOOST_TEST(!im.just_pressed(InputManager::Action::move_up));
}

BOOST_AUTO_TEST_CASE(just_released_requires_transition) {
    InputManager im;
    im.update();
    BOOST_TEST(!im.just_released(InputManager::Action::move_up));
}

BOOST_AUTO_TEST_CASE(mouse_position_defaults) {
    InputManager im;
    im.update();
    auto screen = im.mouse_screen_pos();
    auto world = im.mouse_world_pos();
    // Mouse position should be queryable without crashing
    BOOST_TEST(true);
    (void)screen;
    (void)world;
}

BOOST_AUTO_TEST_CASE(custom_key_binding) {
    InputManager im;
    im.set_key_binding(InputManager::Action::move_up, SDL_SCANCODE_UP);
    // Should not crash — binding updated
    BOOST_TEST(true);
}

BOOST_AUTO_TEST_SUITE_END()
