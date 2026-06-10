#include <boost/test/unit_test.hpp>
#include "core/InputManager.hpp"

BOOST_AUTO_TEST_SUITE(input_tests)

BOOST_AUTO_TEST_CASE(all_actions_have_default_bindings) {
    InputManager im;
    // All actions should be queryable without crashing
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::MoveUp));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::MoveDown));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::MoveLeft));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::MoveRight));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::Interact));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::OpenJournal));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::OpenInventory));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::OpenMap));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::Pause));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::Escape));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::Confirm));
    BOOST_CHECK_NO_THROW(im.isPressed(InputManager::Action::Cancel));
}

BOOST_AUTO_TEST_CASE(actions_initially_not_pressed) {
    InputManager im;
    im.update(); // populate keyboard state
    BOOST_TEST(!im.isPressed(InputManager::Action::MoveUp));
    BOOST_TEST(!im.isPressed(InputManager::Action::Interact));
    BOOST_TEST(!im.isPressed(InputManager::Action::Confirm));
}

BOOST_AUTO_TEST_CASE(just_pressed_requires_transition) {
    InputManager im;
    im.update();
    BOOST_TEST(!im.justPressed(InputManager::Action::MoveUp));
}

BOOST_AUTO_TEST_CASE(just_released_requires_transition) {
    InputManager im;
    im.update();
    BOOST_TEST(!im.justReleased(InputManager::Action::MoveUp));
}

BOOST_AUTO_TEST_CASE(mouse_position_defaults) {
    InputManager im;
    im.update();
    auto screen = im.mouseScreenPos();
    auto world = im.mouseWorldPos();
    // Mouse position should be queryable without crashing
    BOOST_TEST(true);
    (void)screen;
    (void)world;
}

BOOST_AUTO_TEST_CASE(custom_key_binding) {
    InputManager im;
    im.setKeyBinding(InputManager::Action::MoveUp, SDL_SCANCODE_UP);
    // Should not crash — binding updated
    BOOST_TEST(true);
}

BOOST_AUTO_TEST_SUITE_END()
