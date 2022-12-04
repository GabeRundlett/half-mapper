#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring> //Mainly for memset (in bsp.cpp)
#include <vector>
#include <cmath>
#include <map>
#include <algorithm>
#include <assert.h>

#include <daxa/daxa.hpp>
#include <daxa/utils/task_list.hpp>
using namespace daxa::types;
#include "../shared/shared.inl"

struct VERTEX {
    float x, y, z;
    void fixHand() {
        float swapY = y;
        y = z;
        z = swapY;
        x = -x;
    }
    VERTEX(float _x, float _y, float _z) {
        x = _x;
        y = _y;
        z = _z;
    }
    VERTEX() {
        x = 0;
        y = 0;
        z = 0;
    }
};
