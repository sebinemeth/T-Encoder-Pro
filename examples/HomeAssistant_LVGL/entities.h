/*
 * @Description: Entity definitions for Home Assistant Controller
 * @Author: WARP
 * @Date: 2024-10-06
 * @License: GPL 3.0
 */
#pragma once

struct HAEntity {
    String name;
    String entity_id;
    bool state;
    bool lastState;
};

// Define your entities here - simply add/remove/modify as needed
const int ENTITY_COUNT = 2;

HAEntity entities[ENTITY_COUNT] = {
    {String("LilyGo Test"), String("input_boolean.lilygo_test"), false, false},
    {String("LilyGo Test 2"), String("input_boolean.lilygo_test_2"), false, false}
};
