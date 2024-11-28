#pragma once
#include "Texture.hpp"
#include "../defines.hpp"

struct SPreloadedAsset {
    CTexture texture;
    bool     ready = false;
};