#pragma once

#include "gx_platformadapter.h"

class LVGLPlatform : public gx::PlatformAdapter {
public:
  LVGLPlatform();
  ~LVGLPlatform() override;

  void processNative(gx::CoreApplication *app, int filter) override;
};
