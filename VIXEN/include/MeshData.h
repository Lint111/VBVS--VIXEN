#pragma once
#include "Headers.h"


struct VertexWithColor {
    float x, y, z, w; // position
    float r, g, b, a; // color format: RGBA
};

static const VertexWithColor squareData[] = {
	{ -0.5f,  0.5f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0 },
	{  0.5f,  0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0 },
	{  0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0 },
	{ -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0 },
};

uint16_t squareIndices[] = { 0,3,1, 3,2,1 }; // 6 indices