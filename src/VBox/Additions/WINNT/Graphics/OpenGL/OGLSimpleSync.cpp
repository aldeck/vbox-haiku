/** @file
 *
 * VirtualBox Windows NT/2000/XP guest OpenGL ICD
 *
 * Simple buffered OpenGL functions
 *
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#include "VBoxOGL.h"


/* OpenGL functions */
void APIENTRY glFinish (void)
{
    /** @todo if no back buffer, then draw to screen */
    VBOX_OGL_GEN_SYNC_OP(Finish);
}

void APIENTRY glFlush (void)
{
    /** @todo if no back buffer, then draw to screen */
    VBOX_OGL_GEN_SYNC_OP(Flush);
}

GLuint APIENTRY glGenLists (GLsizei range)
{
    VBOX_OGL_GEN_SYNC_OP1_RET(GLuint, GenLists, range);
    return retval;
}

GLboolean APIENTRY glIsEnabled (GLenum cap)
{
    /** @todo cache? */
    VBOX_OGL_GEN_SYNC_OP1_RET(GLboolean, IsEnabled, cap);
    return retval;
}

GLboolean APIENTRY glIsList (GLuint list)
{
    VBOX_OGL_GEN_SYNC_OP1_RET(GLboolean, IsList, list);
    return retval;
}

GLboolean APIENTRY glIsTexture (GLuint texture)
{
    VBOX_OGL_GEN_SYNC_OP1_RET(GLboolean, IsTexture, texture);
    return retval;
}

static uint32_t vboxglGetNrElements(GLenum pname)
{
    switch(pname)
    {
    case GL_ACCUM_ALPHA_BITS:
    case GL_ACCUM_BLUE_BITS:
    case GL_ACCUM_GREEN_BITS:
    case GL_ACCUM_RED_BITS:
    case GL_ALPHA_BIAS:
    case GL_ALPHA_BITS:
    case GL_ALPHA_SCALE:
    case GL_ALPHA_TEST:
    case GL_ALPHA_TEST_FUNC:
    case GL_ALPHA_TEST_REF:
    case GL_ATTRIB_STACK_DEPTH:
    case GL_AUTO_NORMAL:
    case GL_AUX_BUFFERS:
    case GL_BLEND:
    case GL_BLEND_DST:
//    case GL_BLEND_EQUATION_EXT:
    case GL_BLEND_SRC:
    case GL_BLUE_BIAS:
    case GL_BLUE_BITS:
    case GL_BLUE_SCALE:
    case GL_CLIENT_ATTRIB_STACK_DEPTH:
//    case GL_CLIP_PLANEi:
    case GL_COLOR_ARRAY:
    case GL_COLOR_ARRAY_SIZE:
    case GL_COLOR_ARRAY_STRIDE:
    case GL_COLOR_ARRAY_TYPE:
    case GL_COLOR_LOGIC_OP:
    case GL_COLOR_MATERIAL:
    case GL_COLOR_MATERIAL_FACE:
    case GL_COLOR_MATERIAL_PARAMETER:
    case GL_CULL_FACE:
    case GL_CULL_FACE_MODE:
    case GL_CURRENT_INDEX:
    case GL_CURRENT_RASTER_DISTANCE:
    case GL_CURRENT_RASTER_INDEX:
    case GL_CURRENT_RASTER_POSITION_VALID:
    case GL_DEPTH_BIAS:
    case GL_DEPTH_BITS:
    case GL_DEPTH_CLEAR_VALUE:
    case GL_DEPTH_FUNC:
    case GL_DEPTH_SCALE:
    case GL_DEPTH_TEST:
    case GL_DEPTH_WRITEMASK:
    case GL_DITHER:
    case GL_DOUBLEBUFFER:
    case GL_DRAW_BUFFER:
    case GL_EDGE_FLAG:
    case GL_EDGE_FLAG_ARRAY:
    case GL_EDGE_FLAG_ARRAY_STRIDE:
    case GL_FOG:
    case GL_FOG_DENSITY:
    case GL_FOG_END:
    case GL_FOG_HINT:
    case GL_FOG_INDEX:
    case GL_FOG_MODE:
    case GL_FOG_START:
    case GL_FRONT_FACE:
    case GL_GREEN_BIAS:
    case GL_GREEN_BITS:
    case GL_GREEN_SCALE:
    case GL_INDEX_ARRAY:
    case GL_INDEX_ARRAY_STRIDE:
    case GL_INDEX_ARRAY_TYPE:
    case GL_INDEX_BITS:
    case GL_INDEX_CLEAR_VALUE:
    case GL_INDEX_LOGIC_OP:
    case GL_INDEX_MODE:
    case GL_INDEX_OFFSET:
    case GL_INDEX_SHIFT:
    case GL_INDEX_WRITEMASK:
//    case GL_LIGHTi:
    case GL_LIGHTING:
    case GL_LIGHT_MODEL_LOCAL_VIEWER:
    case GL_LIGHT_MODEL_TWO_SIDE:
    case GL_LINE_SMOOTH:
    case GL_LINE_SMOOTH_HINT:
    case GL_LINE_STIPPLE:
    case GL_LINE_STIPPLE_PATTERN:
    case GL_LINE_STIPPLE_REPEAT:
    case GL_LINE_WIDTH:
    case GL_LINE_WIDTH_GRANULARITY  :
    case GL_LIST_BASE:
    case GL_LIST_INDEX:
    case GL_LIST_MODE:
    case GL_LOGIC_OP_MODE:
    case GL_MAP1_COLOR_4:
    case GL_MAP1_GRID_SEGMENTS:
    case GL_MAP1_INDEX:
    case GL_MAP1_NORMAL:
    case GL_MAP1_TEXTURE_COORD_1:
    case GL_MAP1_TEXTURE_COORD_2:
    case GL_MAP1_TEXTURE_COORD_3:
    case GL_MAP1_TEXTURE_COORD_4:
    case GL_MAP1_VERTEX_3:
    case GL_MAP1_VERTEX_4:
    case GL_MAP2_COLOR_4:
    case GL_MAP2_INDEX:
    case GL_MAP2_NORMAL:
    case GL_MAP2_TEXTURE_COORD_1:
    case GL_MAP2_TEXTURE_COORD_2:
    case GL_MAP2_TEXTURE_COORD_3:
    case GL_MAP2_TEXTURE_COORD_4:
    case GL_MAP2_VERTEX_3:
    case GL_MAP2_VERTEX_4:
    case GL_MAP_COLOR:
    case GL_MAP_STENCIL:
    case GL_MATRIX_MODE:
    case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH:
    case GL_MAX_ATTRIB_STACK_DEPTH:
    case GL_MAX_CLIP_PLANES:
    case GL_MAX_EVAL_ORDER:
    case GL_MAX_LIGHTS:
    case GL_MAX_LIST_NESTING:
    case GL_MAX_MODELVIEW_STACK_DEPTH:
    case GL_MAX_NAME_STACK_DEPTH:
    case GL_MAX_PIXEL_MAP_TABLE:
    case GL_MAX_PROJECTION_STACK_DEPTH:
    case GL_MAX_TEXTURE_SIZE:
    case GL_MAX_TEXTURE_STACK_DEPTH :
    case GL_MODELVIEW_STACK_DEPTH:
    case GL_NAME_STACK_DEPTH:
    case GL_NORMAL_ARRAY:
    case GL_NORMAL_ARRAY_STRIDE:
    case GL_NORMAL_ARRAY_TYPE:
    case GL_NORMALIZE:
    case GL_PACK_ALIGNMENT:
    case GL_PACK_LSB_FIRST:
    case GL_PACK_ROW_LENGTH:
    case GL_PACK_SKIP_PIXELS:
    case GL_PACK_SKIP_ROWS:
    case GL_PACK_SWAP_BYTES:
    case GL_PERSPECTIVE_CORRECTION_HINT:
    case GL_PIXEL_MAP_A_TO_A_SIZE:
    case GL_PIXEL_MAP_B_TO_B_SIZE:
    case GL_PIXEL_MAP_G_TO_G_SIZE:
    case GL_PIXEL_MAP_I_TO_A_SIZE:
    case GL_PIXEL_MAP_I_TO_B_SIZE:
    case GL_PIXEL_MAP_I_TO_G_SIZE:
    case GL_PIXEL_MAP_I_TO_I_SIZE:
    case GL_PIXEL_MAP_I_TO_R_SIZE:
    case GL_PIXEL_MAP_R_TO_R_SIZE:
    case GL_PIXEL_MAP_S_TO_S_SIZE:
    case GL_POINT_SIZE:
    case GL_POINT_SIZE_GRANULARITY	  :
    case GL_POINT_SMOOTH:
    case GL_POINT_SMOOTH_HINT	  :
    case GL_POLYGON_OFFSET_FACTOR:
    case GL_POLYGON_OFFSET_UNITS :
    case GL_POLYGON_OFFSET_FILL:
    case GL_POLYGON_OFFSET_LINE:
    case GL_POLYGON_OFFSET_POINT:
    case GL_POLYGON_SMOOTH:
    case GL_POLYGON_SMOOTH_HINT:
    case GL_POLYGON_STIPPLE:
    case GL_PROJECTION_STACK_DEPTH:
    case GL_READ_BUFFER:
    case GL_RED_BIAS:
    case GL_RED_BITS:
    case GL_RED_SCALE:
    case GL_RENDER_MODE:
    case GL_RGBA_MODE:
    case GL_SCISSOR_TEST:
    case GL_SHADE_MODEL:
    case GL_STENCIL_BITS:
    case GL_STENCIL_CLEAR_VALUE:
    case GL_STENCIL_FAIL:
    case GL_STENCIL_FUNC:
    case GL_STENCIL_PASS_DEPTH_FAIL:
    case GL_STENCIL_PASS_DEPTH_PASS:
    case GL_STENCIL_REF:
    case GL_STENCIL_TEST:
    case GL_STENCIL_VALUE_MASK:
    case GL_STENCIL_WRITEMASK:
    case GL_STEREO:
    case GL_SUBPIXEL_BITS:
    case GL_TEXTURE_1D:
//    case GL_TEXTURE_1D_BINDING:
    case GL_TEXTURE_2D	:
//    case GL_TEXTURE_2D_BINDING:
    case GL_TEXTURE_COORD_ARRAY:
    case GL_TEXTURE_COORD_ARRAY_SIZE:
    case GL_TEXTURE_COORD_ARRAY_STRIDE:
    case GL_TEXTURE_COORD_ARRAY_TYPE:
    case GL_TEXTURE_GEN_Q:
    case GL_TEXTURE_GEN_R:
    case GL_TEXTURE_GEN_S:
    case GL_TEXTURE_GEN_T:
    case GL_TEXTURE_STACK_DEPTH:
    case GL_UNPACK_ALIGNMENT:
    case GL_UNPACK_LSB_FIRST:
    case GL_UNPACK_ROW_LENGTH:
    case GL_UNPACK_SKIP_PIXELS:
    case GL_UNPACK_SKIP_ROWS:
    case GL_UNPACK_SWAP_BYTES:
    case GL_VERTEX_ARRAY:
    case GL_VERTEX_ARRAY_SIZE:
    case GL_VERTEX_ARRAY_STRIDE:
    case GL_VERTEX_ARRAY_TYPE:
    case GL_ZOOM_X:
    case GL_ZOOM_Y:
        return 1;

    case GL_ACCUM_CLEAR_VALUE:
//    case GL_BLEND_COLOR_EXT:
    case GL_COLOR_CLEAR_VALUE:
    case GL_COLOR_WRITEMASK:
    case GL_CURRENT_COLOR:
    case GL_CURRENT_RASTER_COLOR:
    case GL_CURRENT_RASTER_POSITION:
    case GL_CURRENT_RASTER_TEXTURE_COORDS:
    case GL_CURRENT_TEXTURE_COORDS:
    case GL_FOG_COLOR:
    case GL_LIGHT_MODEL_AMBIENT:
    case GL_MAP2_GRID_DOMAIN:
    case GL_VIEWPORT:
    case GL_SCISSOR_BOX:
        return 4;

    case GL_DEPTH_RANGE:
    case GL_LINE_WIDTH_RANGE:
    case GL_MAP1_GRID_DOMAIN:
    case GL_MAP2_GRID_SEGMENTS:
    case GL_MAX_VIEWPORT_DIMS:
    case GL_POINT_SIZE_RANGE:
    case GL_POLYGON_MODE:
        return 2;

    case GL_CURRENT_NORMAL:
        return 3;

    case GL_MODELVIEW_MATRIX:
    case GL_PROJECTION_MATRIX:
    case GL_TEXTURE_MATRIX:
        return 16;

    default:
        AssertMsgFailed(("%s Unknown element %x\n", __FUNCTION__, pname));
        return 0;
    }
}

void APIENTRY glGetBooleanv (GLenum pname, GLboolean *params)
{
    uint32_t n = vboxglGetNrElements(pname);

    if (!n)
    {
        AssertMsgFailed(("glGetBooleanv: Invalid enum %x\n", pname));
        glSetError(GL_INVALID_ENUM);
        return;
    }

    VBOX_OGL_GEN_SYNC_OP2_PASS_PTR(GetBooleanv, pname, n*sizeof(GLboolean), params);
    return;
}

void APIENTRY glGetDoublev (GLenum pname, GLdouble *params)
{
    uint32_t n = vboxglGetNrElements(pname);

    if (!n)
    {
        AssertMsgFailed(("glGetDoublev: Invalid enum %x\n", pname));
        glSetError(GL_INVALID_ENUM);
        return;
    }

    VBOX_OGL_GEN_SYNC_OP2_PASS_PTR(GetDoublev, pname, n*sizeof(GLdouble), params);
    return;
}

void APIENTRY glGetFloatv (GLenum pname, GLfloat *params)
{
    uint32_t n = vboxglGetNrElements(pname);

    if (!n)
    {
        AssertMsgFailed(("glGetFloatv: Invalid enum %x\n", pname));
        glSetError(GL_INVALID_ENUM);
        return;
    }

    VBOX_OGL_GEN_SYNC_OP2_PASS_PTR(GetFloatv, pname, n*sizeof(GLfloat), params);
    return;
}

void APIENTRY glGetIntegerv (GLenum pname, GLint *params)
{
    uint32_t n = vboxglGetNrElements(pname);

    if (!n)
    {
        AssertMsgFailed(("glGetIntegerv: Invalid enum %x\n", pname));
        glSetError(GL_INVALID_ENUM);
        return;
    }

    VBOX_OGL_GEN_SYNC_OP2_PASS_PTR(GetIntegerv, pname, n*sizeof(GLint), params);
    return;
}


void APIENTRY glGetMapdv (GLenum target, GLenum query, GLdouble *v)
{
    AssertFailed(); /** @todo */
    return;
}

void APIENTRY glGetMapfv (GLenum target, GLenum query, GLfloat *v)
{
    AssertFailed(); /** @todo */
    return;
}

void APIENTRY glGetMapiv (GLenum target, GLenum query, GLint *v)
{
    AssertFailed(); /** @todo */
    return;
}

void APIENTRY glGetPointerv (GLenum pname, GLvoid* *params)
{
    AssertFailed(); /** @todo */
    return;
}


