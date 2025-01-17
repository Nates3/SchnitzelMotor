#include "gl_renderer.h"
#include "schnitzel_lib.h"
#include "render_interface.h"

// To Load PNG Files
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// To Load TTF Files
#include <ft2build.h>
#include FT_FREETYPE_H

// #############################################################################
//                           OpenGL Structs
// #############################################################################
struct GLContext
{
  char* texturePath;
  int programID;
  int transformSBOID;
  int screenSizeID;
  int projectionID;
  int textureID;
  int fontAtlasID;
  long long textureTimestamp;
};

// #############################################################################
//                           OpenGL Globals
// #############################################################################
static GLContext glContext;

// #############################################################################
//                           Render Interface Implementations
// #############################################################################
void load_font(char* filePath, int fontSize)
{
  FT_Library fontLibrary;
  FT_Init_FreeType(&fontLibrary);

  FT_Face fontFace;
  FT_New_Face(fontLibrary, filePath, 0, &fontFace);
  FT_Set_Pixel_Sizes(fontFace, 0, fontSize);

  int padding = 2;
  int row = 0;
  int col = padding;

  const int textureWidth = 512;
  char textureBuffer[textureWidth  * textureWidth];
  for (FT_ULong glyphIdx = 32; glyphIdx < 127; ++glyphIdx)
  {
    FT_UInt glyphIndex = FT_Get_Char_Index(fontFace, glyphIdx);
    FT_Load_Glyph(fontFace, glyphIndex, FT_LOAD_DEFAULT);
    FT_Render_Glyph(fontFace->glyph, FT_RENDER_MODE_NORMAL);

    if (col + fontFace->glyph->bitmap.width + padding >= 512)
    {
      col = padding;
      row += fontSize;
    }

    for (unsigned int y = 0; y < fontFace->glyph->bitmap.rows; ++y)
    {
      for (unsigned int x = 0; x < fontFace->glyph->bitmap.width; ++x)
      {
        textureBuffer[(row + y) * textureWidth + col + x] =
            fontFace->glyph->bitmap.buffer[y * fontFace->glyph->bitmap.width + x];
      }
    }

    Glyph* glyph = &renderData->glyphs[glyphIdx];
    glyph->textureCoords = 
    {
      (float)col, 
      (float)row
    };
    glyph->size = 
    { 
      (float)fontFace->glyph->bitmap.width, 
      (float)fontFace->glyph->bitmap.rows 
    };
    glyph->advance = 
    {
      (float)(fontFace->glyph->advance.x >> 6), 
      (float)(fontFace->glyph->advance.y >> 6)
    };
    glyph->offset =
    {
      (float)fontFace->glyph->bitmap_left,
      (float)fontFace->glyph->bitmap_top,
    };

    col += fontFace->glyph->bitmap.width + padding;
  }

  FT_Done_Face(fontFace);
  FT_Done_FreeType(fontLibrary);

  // Upload OpenGL Texture
  {
    glGenTextures(1, (GLuint*)&glContext.fontAtlasID);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glContext.fontAtlasID);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, textureWidth, textureWidth, 0, 
                 GL_RED, GL_UNSIGNED_BYTE, (char*)textureBuffer);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
}

// #############################################################################
//                           OpenGL Functions
// #############################################################################
static void APIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity,
                                         GLsizei length, const GLchar* message, const void* user)
{
  if(severity == GL_DEBUG_SEVERITY_LOW || 
     severity == GL_DEBUG_SEVERITY_MEDIUM ||
     severity == GL_DEBUG_SEVERITY_HIGH)
  {
    SM_ASSERT(0, "OpenGL Error: %s", message);
  }
  else
  {
    SM_TRACE((char*)message);
  }
}

bool gl_init(BumpAllocator* transientStorage)
{
  load_gl_functions();

  glDebugMessageCallback(&gl_debug_callback, 0);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glEnable(GL_DEBUG_OUTPUT);

  int vertShaderID = glCreateShader(GL_VERTEX_SHADER);
  int fragShaderID = glCreateShader(GL_FRAGMENT_SHADER);

  int fileSize = 0;
  char* shaderHeader = read_file("src/shader_header.h", &fileSize, transientStorage);
  char* vertShaderSource = read_file("assets/shaders/quad.vert", &fileSize, transientStorage);
  char* fragShaderSource = read_file("assets/shaders/quad.frag", &fileSize, transientStorage);

  if(!shaderHeader)
  {
    SM_ASSERT(0, "Failed to load Shader Header: %s", "src/shader_header.h");
    return false;
  }

  if(!vertShaderSource)
  {
    SM_ASSERT(0, "Failed to load Shader: %s", "assets/shaders/quad.vert");
    return false;
  }

  if(!fragShaderSource)
  {
    SM_ASSERT(0, "Failed to load Shader: %s", "assets/shaders/quad.frag");
    return false;
  }

  // Vertex Shader 
  {
    char* shaderSources[] =
    {
      "#version 430 core\n",
      shaderHeader,
      vertShaderSource
    };

    glShaderSource(vertShaderID, ArraySize(shaderSources), shaderSources, 0);
    glCompileShader(vertShaderID);

    // Validate if the Shader works
    {
      int success;
      char shaderLog[1024] = {};
      glGetShaderiv(vertShaderID, GL_COMPILE_STATUS, &success);
      if(!success)
      {
        glGetShaderInfoLog(vertShaderID, 1024, 0, shaderLog);
        SM_ASSERT(0, "Failed to compile shader: %s", shaderLog);
      }
    }
  }

  // Fragment Shader
  {
    char* shaderSources[] =
    {
      "#version 430 core\r\n",
      shaderHeader,
      fragShaderSource
    };

    glShaderSource(fragShaderID, ArraySize(shaderSources), shaderSources, 0);
    glCompileShader(fragShaderID);
    // Validate if the Shader works
    {
      int success;
      char shaderLog[1024] = {};
      glGetShaderiv(fragShaderID, GL_COMPILE_STATUS, &success);
      if(!success)
      {
        glGetShaderInfoLog(fragShaderID, 1024, 0, shaderLog);
        SM_ASSERT(0, "Failed to compile shader: %s", shaderLog);
      }
    }
  }

  glContext.programID = glCreateProgram();
  glAttachShader(glContext.programID, vertShaderID);
  glAttachShader(glContext.programID, fragShaderID);
  glLinkProgram(glContext.programID);

  // This is preemtively, because they are still bound
  // They are already marked for deletion tho
  glDetachShader(glContext.programID, vertShaderID);
  glDetachShader(glContext.programID, fragShaderID);
  glDeleteShader(vertShaderID);
  glDeleteShader(fragShaderID);

  // This needs to be bound, otherwise OpenGL doesn't draw anything
  // We won't use it tho!
  int VAO = 0;
  glGenVertexArrays(1, (GLuint*)&VAO);
  glBindVertexArray(VAO);

  // Transform Storage Buffer
  {
    glGenBuffers(1, (GLuint*)&glContext.transformSBOID);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, glContext.transformSBOID);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Transform) * MAX_TRANSFORMS,
                 renderData->transforms.elements, GL_DYNAMIC_DRAW);
  }

  // Uniforms
  {
    glContext.screenSizeID = glGetUniformLocation(glContext.programID, "screenSize");
    glContext.projectionID = glGetUniformLocation(glContext.programID, "orthoProjection");
  }

  // Load our first Texture
  {
    int width, height, nChannels;
    glContext.texturePath = "assets/textures/Texture_Atlas_01.png";
    char* data = (char*)stbi_load(glContext.texturePath, 
                                  &width, &height, &nChannels, 4);
    int textureSizeInBytes = 4 * width * height;

    glContext.textureTimestamp = get_timestamp(glContext.texturePath);

    if(!data)
    {
      SM_ASSERT(0, "Failed to load Texture!");
      return false;
    }

    glContext.textureTimestamp = get_timestamp(glContext.texturePath);

    glGenTextures(1, (GLuint*)&glContext.textureID);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, glContext.textureID);

    // set the texture wrapping/filtering options (on the currently bound texture object)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    // This setting only matters when using the GLSL texture() function
    // When you use texelFetch() this setting has no effect,
    // because texelFetch is designed for this purpose
    // See: https://interactiveimmersive.io/blog/glsl/glsl-data-tricks/
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
  }

  // Load Font Atlas
  {
    load_font("assets/fonts/AtariClassic-gry3.ttf", 8);
  }

  glUseProgram(glContext.programID);

  // sRGB output (even if input texture is non-sRGB -> don't rely on texture used)
  // Your font is not using sRGB, for example (not that it matters there, because no actual color is sampled from it)
  // But this could prevent some future bug when you start mixing different types of textures
  // Of course, you still need to correctly set the image file source format when using glTexImage2D()
  glEnable(GL_FRAMEBUFFER_SRGB);
  glDisable(0x809D); // disable multisampling

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_GREATER);

  return true;
}

void gl_render()
{
  // Hot reload texure
  long long textureTimestamp = get_timestamp(glContext.texturePath);
  if(textureTimestamp > glContext.textureTimestamp)
  {
    int width, height, nChannels;
    char* data = (char*)stbi_load(glContext.texturePath, 
                                  &width, &height, &nChannels, 4);
    if(data)
    {
      int textureSizeInBytes = 4 * width * height;
    

      glContext.textureTimestamp = textureTimestamp;
      glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
      stbi_image_free(data);
    }
  }

  // Linux Colors, kinda
  glClearColor(renderData->clearColor.r, 
               renderData->clearColor.g, 
               renderData->clearColor.b, 
               renderData->clearColor.a);
  glClearDepth(0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glViewport(0, 0, input->screenSize.x, input->screenSize.y);

  // Copy screenSize to the GPU
  glUniform2fv(glContext.screenSizeID, 1, &input->screenSize.x);

  // Calculate projection matrix for 2D
  {
    OrthographicCamera2D camera = renderData->camera;
    float zoom = camera.zoom? camera.zoom : 1.0f;
    Vec2 dimensions = camera.dimensions * 1.0f / zoom;
    Vec2 position = camera.position * (float)max((int)(1.0f / zoom), 1);
    Mat4 projectionMatrix = 
      orthographic_projection(position.x - dimensions.x / 2.0f, 
                              position.x + dimensions.x / 2.0f, 
                              position.y - dimensions.y / 2.0f, 
                              position.y + dimensions.y / 2.0f);

    glUniformMatrix4fv(glContext.projectionID, 1, GL_FALSE, 
                      &projectionMatrix.ax);
  }

  // Opaque Objects
  {
    // Copy transforms to the GPU
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(Transform) * MAX_TRANSFORMS,
                    renderData->transforms.elements);
    // Reset for next Frame

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, renderData->transforms.count);
    renderData->transforms.clear();
  }

  // Transparent Objects
  {
    glEnable(GL_BLEND);      
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Copy transparent transforms to the GPU
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(Transform) * MAX_TRANSFORMS,
                    renderData->transparent_transforms.elements);
    // Reset for next Frame
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, renderData->transparent_transforms.count);
    glDisable(GL_BLEND);
    renderData->transparent_transforms.clear();
  }
}















