#include "gx_platform_lvgl.h"

using namespace gx;

LVGLPlatform::LVGLPlatform() : PlatformAdapter(nullptr) {
  
}

LVGLPlatform::~LVGLPlatform() {}

void LVGLPlatform::processNative(CoreApplication *app, int filter) {
  PlatformAdapter::processNative(app, filter);
}