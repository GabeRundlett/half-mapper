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
#include <daxa/utils/math_operators.hpp>
using namespace daxa::types;
using namespace daxa::math_operators;
#include "../shared/shared.inl"

#define COUNT_DRAWS 0
#define EXPORT_ASSETS 0
#define EXPORT_IMAGES 1
#define EXPORT_MESHES 1

#if COUNT_DRAWS
extern usize draw_count;
#endif

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
