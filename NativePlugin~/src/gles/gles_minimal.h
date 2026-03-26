// Minimal OpenGL ES 3.1+ type definitions for MDI plugin.
// Only the types and constants needed for indirect draw calls.
// Avoids requiring the full GLES/EGL SDK.
#pragma once

#include <stdint.h>

#ifdef _WIN32
#define GL_APIENTRY __stdcall
#else
#define GL_APIENTRY
#endif

// Basic GL types
typedef unsigned int   GLenum;
typedef unsigned int   GLuint;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLboolean;
typedef char           GLchar;
typedef intptr_t       GLintptr;
typedef ptrdiff_t      GLsizeiptr;

// Constants
#define GL_TRIANGLES                  0x0004
#define GL_TRIANGLE_STRIP             0x0005
#define GL_LINES                      0x0001
#define GL_LINE_STRIP                 0x0003
#define GL_POINTS                     0x0000

#define GL_UNSIGNED_SHORT             0x1403
#define GL_UNSIGNED_INT               0x1405

#define GL_DRAW_INDIRECT_BUFFER       0x8F3F
#define GL_EXTENSIONS                 0x1F03

#define GL_FALSE                      0
#define GL_TRUE                       1

// Function pointer types

// glDrawElementsIndirect — OpenGL ES 3.1 core
typedef void (GL_APIENTRY *PFNGLDRAWELEMENTSINDIRECTPROC)(
    GLenum mode, GLenum type, const void* indirect);

// glMultiDrawElementsIndirectEXT — GL_EXT_multi_draw_indirect
typedef void (GL_APIENTRY *PFNGLMULTIDRAWELEMENTSINDIRECTEXTPROC)(
    GLenum mode, GLenum type, const void* indirect,
    GLsizei drawcount, GLsizei stride);

// glBindBuffer
typedef void (GL_APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);

// glGetString
typedef const GLchar* (GL_APIENTRY *PFNGLGETSTRINGPROC)(GLenum name);

// Platform-specific proc address resolver
#ifdef _WIN32
// On Windows (ANGLE/desktop GLES emulation), use wglGetProcAddress or loaded from DLL
typedef void* (*PFN_GetProcAddress)(const char* name);
#else
// On Android, use eglGetProcAddress
typedef void (*(*PFN_eglGetProcAddress)(const char* name))();
#endif
