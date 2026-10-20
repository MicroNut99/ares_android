#include "gl-renderer.hpp"

#include <android/log.h>

#include <algorithm>
#include <cmath>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "ares", __VA_ARGS__)

namespace aresdroid {

static const char* vertexShader = R"(
attribute vec2 position;
attribute vec2 texcoord;
varying vec2 uv;
void main() {
  uv = texcoord;
  gl_Position = vec4(position, 0.0, 1.0);
}
)";

//the core's pixels are 0xAARRGGBB words; uploaded as RGBA bytes they arrive as B,G,R,A
static const char* fragmentShader = R"(
precision mediump float;
varying vec2 uv;
uniform sampler2D source;
void main() {
  gl_FragColor = vec4(texture2D(source, uv).bgr, 1.0);
}
)";

GLuint GLRenderer::compile(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if(!ok) {
    char text[512] = {};
    glGetShaderInfoLog(shader, sizeof(text), nullptr, text);
    LOG("shader error: %s", text);
  }
  return shader;
}

void GLRenderer::surfaceCreated() {
  //a new EGL context: everything from the old one is gone
  program = glCreateProgram();
  glAttachShader(program, compile(GL_VERTEX_SHADER, vertexShader));
  glAttachShader(program, compile(GL_FRAGMENT_SHADER, fragmentShader));
  glLinkProgram(program);
  positionAttribute = glGetAttribLocation(program, "position");
  texcoordAttribute = glGetAttribLocation(program, "texcoord");
  samplerUniform = glGetUniformLocation(program, "source");

  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  textureWidth = textureHeight = 0;
  serial = 0;
}

void GLRenderer::surfaceChanged(int width, int height) {
  surfaceWidth = width;
  surfaceHeight = height;
}

void GLRenderer::draw(Session& session) {
  glViewport(0, 0, surfaceWidth, surfaceHeight);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glBindTexture(GL_TEXTURE_2D, texture);
  if(session.copyFrame(frame, serial)) {
    serial = frame.serial;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if(frame.width != textureWidth || frame.height != textureHeight) {
      textureWidth = frame.width;
      textureHeight = frame.height;
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.pixels.data());
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, textureWidth, textureHeight, GL_RGBA, GL_UNSIGNED_BYTE, frame.pixels.data());
    }
  }
  if(!textureWidth || !surfaceWidth || !surfaceHeight) return;

  GLint filter = smooth ? GL_LINEAR : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

  //the picture's size in surface pixels
  double aspect = frame.aspect > 0.1 ? frame.aspect : 4.0 / 3.0;
  double outW = surfaceWidth, outH = surfaceHeight;
  if(scale != Scale::Stretch) {
    if(outW / outH > aspect) outW = outH * aspect;
    else outH = outW / aspect;
    if(scale == Scale::Integer) {
      //whole multiples of the console's line count (224 / 240, or 448 / 480 interlaced)
      double lines = (double)textureHeight;
      double factor = std::floor(outH / lines);
      if(factor >= 1.0) {
        outH = lines * factor;
        outW = outH * aspect;
      }
    }
  }
  float x = (float)(outW / surfaceWidth);
  float y = (float)(outH / surfaceHeight);
  //top-aligned in portrait leaves room for the touch controls below the picture
  float top = surfaceHeight > surfaceWidth ? 1.0f : y;
  float bottom = top - 2.0f * y;

  const GLfloat vertices[] = {
    -x, bottom,  0.0f, 1.0f,
     x, bottom,  1.0f, 1.0f,
    -x, top,     0.0f, 0.0f,
     x, top,     1.0f, 0.0f,
  };

  glUseProgram(program);
  glUniform1i(samplerUniform, 0);
  glActiveTexture(GL_TEXTURE0);
  glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices);
  glVertexAttribPointer(texcoordAttribute, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices + 2);
  glEnableVertexAttribArray(positionAttribute);
  glEnableVertexAttribArray(texcoordAttribute);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

}
