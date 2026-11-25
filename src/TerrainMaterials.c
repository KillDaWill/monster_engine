#define GL_GLEXT_PROTOTYPES
#include "TerrainMaterials.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MATERIAL_SIZE = 512, FIRST_TEXTURE_UNIT = 0 };

void TerrainMaterials_Load(TerrainMaterials *materials) {
    if (materials->attempted) return;
    materials->attempted = true;

    const char *paths[] = {"assets/terrain/albedo.raw", "assets/terrain/normal.raw", "assets/terrain/masks.raw"};
    const GLint internalFormat[] = {GL_SRGB8_ALPHA8, GL_RG8, GL_RG8};
    const GLenum format[] = {GL_RGBA, GL_RG, GL_RG};
    const int layers[] = {4, 5, 4};
    const int channels[] = {4, 2, 2};
    GLint previous = 0, unpackBuffer = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &previous);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);

    GLfloat anisotropy = 1.0f;
    GLint extensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
    for (GLint i = 0; i < extensions; ++i) {
        const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (extension && (strcmp(extension, "GL_EXT_texture_filter_anisotropic") == 0 ||
                          strcmp(extension, "GL_ARB_texture_filter_anisotropic") == 0)) {
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
            if (anisotropy > 4.0f) anisotropy = 4.0f;
            break;
        }
    }

    bool ok = true;
    glGenTextures(3, materials->textures);
    for (int i = 0; i < 3 && ok; ++i) {
        size_t bytes = (size_t)MATERIAL_SIZE * MATERIAL_SIZE * layers[i] * channels[i];
        FILE *file = fopen(paths[i], "rb");
        unsigned char *data = file ? malloc(bytes) : NULL;
        ok = data && fread(data, 1, bytes, file) == bytes && fgetc(file) == EOF;
        if (file) fclose(file);
        if (ok) {
            glBindTexture(GL_TEXTURE_2D_ARRAY, materials->textures[i]);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internalFormat[i], MATERIAL_SIZE, MATERIAL_SIZE,
                         layers[i], 0, format[i], GL_UNSIGNED_BYTE, data);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            if (anisotropy > 1.0f) glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy);
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            GLint mipWidth = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 9, GL_TEXTURE_WIDTH, &mipWidth);
            ok = mipWidth == 1;
            for (int side = MATERIAL_SIZE; side > 0; side /= 2)
                materials->vramBytes += (size_t)side * side * layers[i] * channels[i];
        }
        if (!ok) fprintf(stderr, "[TerrainMaterials] %s: textura no disponible, usando material simplificado.\n", paths[i]);
        free(data);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)previous);
    glPopClientAttrib();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpackBuffer);
    if (!ok) {
        TerrainMaterials_Free(materials);
        materials->attempted = true;
    }
}

void TerrainMaterials_Free(TerrainMaterials *materials) {
    if (materials->textures[0]) glDeleteTextures(3, materials->textures);
    *materials = (TerrainMaterials){0};
}

TerrainMaterialBindings TerrainMaterials_GetBindings(unsigned int program) {
    return (TerrainMaterialBindings){
        .enabled = glGetUniformLocation(program, "uHasMaterials"),
        .sampler = {
            glGetUniformLocation(program, "uMaterialAlbedo"),
            glGetUniformLocation(program, "uMaterialNormal"),
            glGetUniformLocation(program, "uMaterialMasks"),
        },
        .encodeSRGB = glGetUniformLocation(program, "uEncodeSRGB"),
    };
}

TerrainMaterialState TerrainMaterials_Bind(const TerrainMaterials *materials,
                                           const TerrainMaterialBindings *bindings) {
    TerrainMaterialState state = {0};
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeUnit);
    glUniform1i(bindings->enabled, materials && materials->textures[0]);
    bool encode = true;
    if (glIsEnabled(GL_FRAMEBUFFER_SRGB)) {
        GLint fbo = 0, buffer = GL_NONE, encoding = GL_LINEAR;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_DRAW_BUFFER0, &buffer);
        if (!fbo) {
            if (buffer == GL_BACK || buffer == GL_LEFT) buffer = GL_BACK_LEFT;
            if (buffer == GL_FRONT) buffer = GL_FRONT_LEFT;
        }
        if (buffer != GL_NONE)
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, (GLenum)buffer,
                                                    GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
        encode = encoding != GL_SRGB;
    }
    glUniform1i(bindings->encodeSRGB, encode);
    for (int i = 0; i < 3; ++i) {
        if (bindings->sampler[i] < 0) continue;
        state.touched |= 1u << i;
        glActiveTexture(GL_TEXTURE0 + FIRST_TEXTURE_UNIT + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &state.texture[i]);
        glBindTexture(GL_TEXTURE_2D_ARRAY, materials ? materials->textures[i] : 0);
        glUniform1i(bindings->sampler[i], FIRST_TEXTURE_UNIT + i);
    }
    return state;
}

void TerrainMaterials_Restore(const TerrainMaterialState *state) {
    for (int i = 0; i < 3; ++i) {
        if (!(state->touched & (1u << i))) continue;
        glActiveTexture(GL_TEXTURE0 + FIRST_TEXTURE_UNIT + i);
        glBindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)state->texture[i]);
    }
    glActiveTexture((GLenum)state->activeUnit);
}
