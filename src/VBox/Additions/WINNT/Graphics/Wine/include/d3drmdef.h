/*
 * Copyright 2007 Vijay Kiran Kamuju
 * Copyright 2007 David ADAM
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * Sun LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Sun elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */

#ifndef __D3DRMDEFS_H__
#define __D3DRMDEFS_H__

#include <stddef.h>
#include <d3dtypes.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef D3DVALUE D3DRMMATRIX4D[4][4];

typedef struct _D3DRMQUATERNION {
    D3DVALUE s;
    D3DVECTOR v;
} D3DRMQUATERNION, *LPD3DRMQUATERNION;

typedef enum _D3DRMLIGHTTYPE {
    D3DRMLIGHT_AMBIENT,
    D3DRMLIGHT_POINT,
    D3DRMLIGHT_SPOT,
    D3DRMLIGHT_DIRECTIONAL,
    D3DRMLIGHT_PARALLELPOINT
} D3DRMLIGHTTYPE, *LPD3DRMLIGHTTYPE;

typedef struct _D3DRMPALETTEENTRY {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char flags;
} D3DRMPALETTEENTRY, *LPD3DRMPALETTEENTRY;

typedef struct _D3DRMIMAGE {
    int width;
    int height;
    int aspectx;
    int aspecty;
    int depth;
    int rgb;
    int bytes_per_line;
    void* buffer1;
    void* buffer2;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    unsigned long alpha_mask;
    int palette_size;
    D3DRMPALETTEENTRY* palette;
} D3DRMIMAGE, *LPD3DRMIMAGE;

typedef enum _D3DRMWRAPTYPE {
    D3DRMWRAP_FLAT,
    D3DRMWRAP_CYLINDER,
    D3DRMWRAP_SPHERE,
    D3DRMWRAP_CHROME,
    D3DRMWRAP_SHEET,
    D3DRMWRAP_BOX
} D3DRMWRAPTYPE, *LPD3DRMWRAPTYPE;

typedef DWORD D3DRMLOADOPTIONS;

typedef enum _D3DRMUSERVISUALREASON {
    D3DRMUSERVISUAL_CANSEE,
    D3DRMUSERVISUAL_RENDER
} D3DRMUSERVISUALREASON, *LPD3DRMUSERVISUALREASON;

void WINAPI D3DRMMatrixFromQuaternion(D3DRMMATRIX4D, LPD3DRMQUATERNION);

LPD3DRMQUATERNION WINAPI D3DRMQuaternionFromRotation(LPD3DRMQUATERNION ,LPD3DVECTOR,D3DVALUE);
LPD3DRMQUATERNION WINAPI D3DRMQuaternionMultiply(LPD3DRMQUATERNION, LPD3DRMQUATERNION, LPD3DRMQUATERNION);
LPD3DRMQUATERNION WINAPI D3DRMQuaternionSlerp(LPD3DRMQUATERNION, LPD3DRMQUATERNION, LPD3DRMQUATERNION, D3DVALUE);

LPD3DVECTOR WINAPI D3DRMVectorAdd(LPD3DVECTOR, LPD3DVECTOR, LPD3DVECTOR);
LPD3DVECTOR WINAPI D3DRMVectorCrossProduct(LPD3DVECTOR, LPD3DVECTOR, LPD3DVECTOR);
D3DVALUE WINAPI D3DRMVectorDotProduct(LPD3DVECTOR, LPD3DVECTOR);
LPD3DVECTOR WINAPI D3DRMVectorNormalize(LPD3DVECTOR);

#define D3DRMVectorNormalise D3DRMVectorNormalize

D3DVALUE WINAPI D3DRMVectorModulus(LPD3DVECTOR);
LPD3DVECTOR WINAPI D3DRMVectorRandom(LPD3DVECTOR);
LPD3DVECTOR WINAPI D3DRMVectorRotate(LPD3DVECTOR, LPD3DVECTOR, LPD3DVECTOR, D3DVALUE);
LPD3DVECTOR WINAPI D3DRMVectorReflect(LPD3DVECTOR, LPD3DVECTOR, LPD3DVECTOR);
LPD3DVECTOR WINAPI D3DRMVectorScale(LPD3DVECTOR, LPD3DVECTOR, D3DVALUE);
LPD3DVECTOR WINAPI D3DRMVectorSubtract(LPD3DVECTOR, LPD3DVECTOR, LPD3DVECTOR);

D3DCOLOR WINAPI D3DRMCreateColorRGB(D3DVALUE, D3DVALUE, D3DVALUE);
D3DCOLOR WINAPI D3DRMCreateColorRGBA(D3DVALUE, D3DVALUE, D3DVALUE, D3DVALUE);
D3DVALUE WINAPI D3DRMColorGetAlpha(D3DCOLOR);
D3DVALUE WINAPI D3DRMColorGetBlue(D3DCOLOR);
D3DVALUE WINAPI D3DRMColorGetGreen(D3DCOLOR);
D3DVALUE WINAPI D3DRMColorGetRed(D3DCOLOR);

#if defined(__cplusplus)
}
#endif

#endif
