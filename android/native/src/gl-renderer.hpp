#pragma once

#include <GLES2/gl2.h>

#include <cstdint>

#include "frontend.hpp"

namespace aresdroid {

//Draws the newest emulator frame into the current GLES2 surface (GLSurfaceView's thread).
class GLRenderer {
public:
  enum class Scale : int { Fit = 0, Integer = 1, Stretch = 2 };

  void surfaceCreated();
  void surfaceChanged(int width, int height);
  void draw(Session& session);

  Scale scale = Scale::Fit;
  bool smooth = true;

private:
  GLuint compile(GLenum type, const char* source);

  GLuint program = 0;
  GLuint texture = 0;
  GLint positionAttribute = -1;
  GLint texcoordAttribute = -1;
  GLint samplerUniform = -1;
  int surfaceWidth = 0;
  int surfaceHeight = 0;
  uint32_t textureWidth = 0;
  uint32_t textureHeight = 0;
  VideoFrame frame;
  uint64_t serial = 0;
};

}
