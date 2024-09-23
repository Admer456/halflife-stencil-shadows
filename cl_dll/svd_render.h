//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#ifndef SVD_RENDER_H
#define SVD_RENDER_H

#include "ref_params.h"

extern void SVD_Init();
extern void SVD_VidInit();
extern void SVD_Frame();
extern void SVD_Shutdown();
extern void SVD_CreateStencilFBO();

extern void SVD_CalcRefDef( ref_params_t* pparams );
extern void SVD_DrawTransparentTriangles();
extern void SVD_PerformFBOBlit();
extern int R_DetermineSurfaceStructSize();
#endif