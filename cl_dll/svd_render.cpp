//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "com_model.h"
#include "studio.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "dlight.h"
#include "triangleapi.h"

#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <math.h>

#include "studio_util.h"
#include "r_studioint.h"

#include "StudioModelRenderer.h"
#include "GameStudioModelRenderer.h"

#include "pmtrace.h"
#include "r_efx.h"
#include "event_api.h"
#include "event_args.h"
#include "in_defs.h"
#include "pm_defs.h"
#include "lightlist.h"
#include "svdformat.h"
#include "svd_render.h"

#define GLEW_STATIC 1
#include "GL/glew.h"

// Quake definitions
#define	SURF_PLANEBACK		2
#define	SURF_DRAWSKY		4
#define SURF_DRAWSPRITE		8
#define SURF_DRAWTURB		0x10
#define SURF_DRAWTILED		0x20
#define SURF_DRAWBACKGROUND	0x40
#define SURF_UNDERWATER		0x80
#define SURF_DONTWARP		0x100
#define BACKFACE_EPSILON	0.01

// 0-2 are axial planes
#define	PLANE_X			0
#define	PLANE_Y			1
#define	PLANE_Z			2

#define MAX_RENDER_ENTITIES 16384

// Globals used by shadow rendering
model_t*	g_pWorld;
int			g_visFrame = 0;
int			g_frameCount = 0;

Vector		g_viewOrigin;
Vector		g_viewAngles;

bool		g_bFBOSupported = false;
bool		g_useMSAA = true;
int			g_msaaSetting = 4; // Same as Steam HL

// Latest free texture
GLuint		g_freeTextureIndex = 131072;

// renderbuffer objects for fbo with stencil buffer
GLuint		g_colorRBO = 0;
GLuint		g_depthStencilRBO = 0;
GLuint		g_stencilFBO = 0;

// intermediate renderbuffer
GLuint		g_intermediateColorRBO = 0;
GLuint		g_intermediateDepthRBO = 0;
GLuint		g_intermediateFBO = 0;

// Framebuffer binding coming from Steam HL
GLint		g_steamHLBoundFBO = 0;

// Pointer to default_fov
cvar_t*		g_pCvarDefaultFOV = NULL;

// msurface_t struct size
int			g_msurfaceStructSize = 0;

// The renderer object, created on the stack.
extern CGameStudioModelRenderer g_StudioRenderer;

#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

#define DEG2RAD( a ) ( a * M_PI ) / 180.0F

// Frustum planes
mplane_t g_frustumPlanes[5];

// Global engine <-> studio model rendering code interface
extern engine_studio_api_t IEngineStudio;

/*
====================
Mod_PointInLeaf

====================
*/
void RotatePointAroundVector( Vector& vDest, const Vector& vDir, const Vector& vPoint, float flDegrees )
{
	float q[3];
	float q3;
	float t[3];
	float t3;
	float hrad;
	float s;

	hrad = DEG2RAD(flDegrees) / 2;
	s = sin(hrad);
	VectorScale(vDir, s, q);
	q3 = cos(hrad);

	CrossProduct(q, vPoint, t);
	VectorMA(t, q3, vPoint, t);
	t3 = DotProduct(q, vPoint);

	CrossProduct(q, t, vDest);
	VectorMA(vDest, t3, q, vDest);
	VectorMA(vDest, q3, t, vDest);
}

/*
==================
SignbitsForPlane

==================
*/
int SignbitsForPlane( mplane_t *pOut )
{
	int	nBits = 0;

	for(int j = 0; j < 3; j++)
	{
		if(pOut->normal[j] < 0)
			nBits |= 1<<j;
	}

	return nBits;
}

/*
==================
BoxOnPlaneSide

==================
*/
int BoxOnPlaneSide( const Vector& emins, const Vector& emaxs, mplane_t *p )
{
	float	dist1, dist2;
	int		sides;

	switch(p->signbits)
	{
		case 0:
			dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
			dist2 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
			break;
		case 1:
			dist1 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
			dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
			break;
		case 2:
			dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
			dist2 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
			break;
		case 3:
			dist1 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
			dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
			break;
		case 4:
			dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
			dist2 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
			break;
		case 5:
			dist1 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
			dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
			break;
		case 6:
			dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
			dist2 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
			break;
		case 7:
			dist1 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
			dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
			break;
		default:
			dist1 = dist2 = 0;
			break;
	}

	sides = 0;
	if(dist1 >= p->dist)
		sides = 1;
	if(dist2 < p->dist)
		sides |= 2;

	return sides;
}

/*
====================
R_SetFrustum

====================
*/
void R_SetFrustum( const Vector& vAngles, const Vector& vOrigin, float flFOV, float flFarDist )
{
	Vector vVpn, vUp, vRight;
	gEngfuncs.pfnAngleVectors(vAngles, vVpn, vRight, vUp);

	RotatePointAroundVector(g_frustumPlanes[0].normal, vUp, vVpn, -(90-flFOV / 2));
	RotatePointAroundVector(g_frustumPlanes[1].normal, vUp, vVpn, 90-flFOV / 2);
	RotatePointAroundVector(g_frustumPlanes[2].normal, vRight, vVpn, 90-flFOV / 2);
	RotatePointAroundVector(g_frustumPlanes[3].normal, vRight, vVpn, -(90 - flFOV / 2));

	for(int i = 0; i < 4; i++)
	{
		g_frustumPlanes[i].type = PLANE_ANYZ;
		g_frustumPlanes[i].dist = DotProduct(vOrigin, g_frustumPlanes[i].normal);
		g_frustumPlanes[i].signbits = SignbitsForPlane(&g_frustumPlanes[i]);
	}
}

/*
=====================
R_CullBox

=====================
*/
bool R_CullBox( const Vector& vMins, const Vector& vMaxs )
{	
	for(int i = 0; i < 4; i++)
	{
		if(BoxOnPlaneSide(vMins, vMaxs, &g_frustumPlanes[i]) == 2)
			return true;
	}

	return false;
}

/*
====================
Mod_PointInLeaf

====================
*/
mleaf_t *Mod_PointInLeaf (Vector p, model_t *model) // quake's func
{
	mnode_t *node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		mplane_t *plane = node->plane;
		float d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}

	return NULL;	// never reached
}

/*
==================
R_IsExtensionSupported

==================
*/
bool R_IsExtensionSupported( const char *ext )
{
	const char * extensions = (const char *)glGetString ( GL_EXTENSIONS );
	const char * start = extensions;
	const char * ptr;

	while ( ( ptr = strstr ( start, ext ) ) != NULL )
	{
		// we've found, ensure name is exactly ext
		const char * end = ptr + strlen ( ext );
		if ( isspace ( *end ) || *end == '\0' )
			return true;

		start = end;
	}
	return false;
}

/*
====================
SVD_CreateIntermediateFBO

====================
*/
void SVD_CreateIntermediateFBO( void )
{
	if(!g_bFBOSupported)
		return;

	SCREENINFO	scrinfo;
	scrinfo.iSize = sizeof(scrinfo);
	GetScreenInfo(&scrinfo);

	// Set up main rendering target
	glGenRenderbuffers(1, &g_intermediateColorRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, g_intermediateColorRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA, scrinfo.iWidth, scrinfo.iHeight);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenRenderbuffers(1, &g_intermediateDepthRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, g_intermediateDepthRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, scrinfo.iWidth, scrinfo.iHeight);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenFramebuffers(1, &g_intermediateFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, g_intermediateFBO);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_intermediateColorRBO);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_intermediateDepthRBO);

	GLenum eStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(eStatus != GL_FRAMEBUFFER_COMPLETE)
	{
		gEngfuncs.Con_Printf("%s - FBO creation failed. Code returned: %d.\n", __FUNCTION__, (int)glGetError());
		g_bFBOSupported = false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/*
====================
SVD_CreateStencilFBO

====================
*/
void SVD_CreateStencilFBO( void )
{
	if(!g_bFBOSupported)
		return;

	SCREENINFO	scrinfo;
	scrinfo.iSize = sizeof(scrinfo);
	GetScreenInfo(&scrinfo);

	// Set up main rendering target
	glGenRenderbuffers(1, &g_colorRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, g_colorRBO);
	if(g_useMSAA && g_msaaSetting > 0)
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, g_msaaSetting, GL_RGBA, scrinfo.iWidth, scrinfo.iHeight);
	else
		glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA, scrinfo.iWidth, scrinfo.iHeight);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenRenderbuffers(1, &g_depthStencilRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, g_depthStencilRBO);
	if(g_useMSAA && g_msaaSetting > 0)
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, g_msaaSetting, GL_DEPTH24_STENCIL8, scrinfo.iWidth, scrinfo.iHeight);
	else
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, scrinfo.iWidth, scrinfo.iHeight);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenFramebuffers(1, &g_stencilFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, g_stencilFBO);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_colorRBO);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_depthStencilRBO);

	GLenum eStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(eStatus != GL_FRAMEBUFFER_COMPLETE)
	{
		gEngfuncs.Con_Printf("%s - FBO creation failed. Code returned: %d.\n", __FUNCTION__, (int)glGetError());
		g_bFBOSupported = false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/*
====================
SVD_VidInit

====================
*/
void SVD_VidInit( void )
{
	SVD_Clear();
}

/*
====================
SVD_Init

====================
*/
void SVD_Init( void )
{
	if(IEngineStudio.IsHardware() != 1)
	{
		g_bFBOSupported = false;
		return;
	}

	if (glewInit() != GLEW_OK)
	{
		gEngfuncs.Con_Printf("GLEW failed to launch for some reason.\n");
		return;
	}

	if(!R_IsExtensionSupported("EXT_framebuffer_object") && !R_IsExtensionSupported("ARB_framebuffer_object"))
	{
		gEngfuncs.Con_Printf("Your hardware does not support framebuffer objects. Stencil shadows will remain disabled.\n");
		g_bFBOSupported = false;
		return;
	}

	if(glGenRenderbuffers && glBindRenderbuffer && glRenderbufferStorage && glFramebufferRenderbuffer
		&& glFramebufferTexture2D && glCheckFramebufferStatus && glBindFramebuffer && glGenFramebuffers
		&& glDeleteRenderbuffers && glDeleteFramebuffers && glGetFramebufferAttachmentParameteriv
		&& glGetRenderbufferParameteriv && glBlitFramebuffer)
	{
		// Functions loaded fine
		g_bFBOSupported = true;
	}
	else
	{
		g_bFBOSupported = false;
		return;
	}

	// Create the stencil FBO
	SVD_CreateStencilFBO();

	// Create intermediate FBO
	if(g_bFBOSupported && g_useMSAA && g_msaaSetting > 0)
		SVD_CreateIntermediateFBO();
}

/*
====================
SVD_Shutdown

====================
*/
void SVD_Shutdown( void )
{
	SVD_Clear();

	if(!g_bFBOSupported)
		return;

	if(g_colorRBO)
	{
		glDeleteRenderbuffers(1, &g_colorRBO);
		g_colorRBO = 0;
	}

	if(g_depthStencilRBO)
	{
		glDeleteRenderbuffers(1, &g_depthStencilRBO);
		g_depthStencilRBO = 0;
	}

	if(g_stencilFBO)
	{
		glDeleteFramebuffers(1, &g_stencilFBO);
		g_stencilFBO = 0;
	}


	if(g_intermediateColorRBO)
	{
		glDeleteRenderbuffers(1, &g_intermediateColorRBO);
		g_intermediateColorRBO = 0;
	}

	if(g_intermediateDepthRBO)
	{
		glDeleteRenderbuffers(1, &g_intermediateDepthRBO);
		g_intermediateDepthRBO = 0;
	}

	if(g_intermediateFBO)
	{
		glDeleteFramebuffers(1, &g_intermediateFBO);
		g_intermediateFBO = 0;
	}
}

/*
====================
SVD_DrawBrushModel

====================
*/
void SVD_DrawBrushModel ( cl_entity_t *pentity )
{
	model_t *pmodel = pentity->model;

	Vector vlocalview;
	Vector vmins, vmaxs;

	// set model-local view origin
	VectorCopy(g_viewOrigin, vlocalview);

	if (pentity->angles[0] || pentity->angles[1] || pentity->angles[2])
	{
		for (int i = 0; i < 3; i++)
		{
			vmins[i] = pentity->origin[i] - pmodel->radius;
			vmaxs[i] = pentity->origin[i] + pmodel->radius;
		}
	}
	else
	{
		VectorAdd (pentity->origin, pmodel->mins, vmins);
		VectorAdd (pentity->origin, pmodel->maxs, vmaxs);
	}

	if (R_CullBox(vmins, vmaxs))
		return;

	VectorSubtract (vlocalview, pentity->origin, vlocalview);

	if(pentity->angles[0] || pentity->angles[1] || pentity->angles[2])
	{
		Vector	vtemp, vforward, vright, vup;
		VectorCopy(vlocalview, vtemp);
		AngleVectors(pentity->angles, vforward, vright, vup);
		vlocalview[0] = DotProduct(vtemp, vforward);
		vlocalview[1] = -DotProduct(vtemp, vright); 
		vlocalview[2] = DotProduct(vtemp, vup);
	}

	if(pentity->curstate.origin[0] || pentity->curstate.origin[1] || pentity->curstate.origin[2]
		|| pentity->curstate.angles[0] || pentity->curstate.angles[1] || pentity->curstate.angles[2])
	{
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();

		glTranslatef(pentity->curstate.origin[0],  pentity->curstate.origin[1],  pentity->curstate.origin[2]);

		glRotatef(pentity->curstate.angles[1],  0, 0, 1);
		glRotatef(pentity->curstate.angles[0],  0, 1, 0);
		glRotatef(pentity->curstate.angles[2],  1, 0, 0);
	}

	byte* pfirstsurfbyteptr = reinterpret_cast<byte*>(g_pWorld->surfaces);
	for (int i = 0; i < pmodel->nummodelsurfaces; i++)
	{
		msurface_t	*psurface = reinterpret_cast<msurface_t*>(pfirstsurfbyteptr + g_msurfaceStructSize*(pmodel->firstmodelsurface+i));
		mplane_t *pplane = psurface->plane;

		float fldot = DotProduct(vlocalview, pplane->normal) - pplane->dist;

		if (((psurface->flags & SURF_PLANEBACK) && (fldot < -BACKFACE_EPSILON)) 
			|| (!(psurface->flags & SURF_PLANEBACK) && (fldot > BACKFACE_EPSILON)))
		{
			if (psurface->flags & SURF_DRAWSKY)
				continue;

			if (psurface->flags & SURF_DRAWTURB)
				continue;

			glpoly_t *p = psurface->polys;
			float *v = p->verts[0];
			
			glBegin (GL_POLYGON);			
			for (int j = 0; j < p->numverts; j++, v+= VERTEXSIZE)
			{
				glTexCoord2f (v[3], v[4]);
				glVertex3fv (v);
			}
			glEnd ();
		}
	}

	if(pentity->curstate.origin[0] || pentity->curstate.origin[1] || pentity->curstate.origin[2]
		|| pentity->curstate.angles[0] || pentity->curstate.angles[1] || pentity->curstate.angles[2])
	{
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
	}
}

/*
====================
SVD_RecursiveDrawWorld

====================
*/
void SVD_RecursiveDrawWorld ( mnode_t *node )
{
	if(!g_msurfaceStructSize)
		g_msurfaceStructSize = R_DetermineSurfaceStructSize();

	if (node->contents == CONTENTS_SOLID)
		return;

	if (node->visframe != g_visFrame)
		return;
	
	if (node->contents < 0)
		return;		// faces already marked by engine

	// recurse down the children, Order doesn't matter
	SVD_RecursiveDrawWorld (node->children[0]);
	SVD_RecursiveDrawWorld (node->children[1]);

	// draw stuff
	int c = node->numsurfaces;
	if (node->numsurfaces > 0)
	{
		byte* pfirstsurfbyteptr = reinterpret_cast<byte*>(g_pWorld->surfaces);
		for(int i = 0; i < node->numsurfaces; i++)
		{
			msurface_t	*surf = reinterpret_cast<msurface_t*>(pfirstsurfbyteptr + g_msurfaceStructSize*(node->firstsurface+i));
			if (surf->visframe != g_frameCount)
				continue;

			if (surf->flags & (SURF_DRAWSKY|SURF_DRAWTURB|SURF_UNDERWATER))
				continue;

			glpoly_t *p = surf->polys;
			float *v = p->verts[0];
			
			glBegin (GL_POLYGON);			
			for (int j = 0; j < p->numverts; j++, v+= VERTEXSIZE)
			{
				glTexCoord2f (v[3], v[4]);
				glVertex3fv (v);
			}
			glEnd ();
		}
	}
}

/*
====================
SVD_CalcRefDef

====================
*/
void SVD_CalcRefDef ( ref_params_t* pparams )
{
	if(IEngineStudio.IsHardware() != 1)
		return;

	SVD_CheckInit();

	g_viewOrigin = pparams->vieworg;
	g_viewAngles = pparams->viewangles;

	if(g_StudioRenderer.m_pCvarDrawStencilShadows->value < 1)
		return;

	if(g_bFBOSupported)
	{
		// Get previous FBO binding, as Steam HL's MSAA might be enabled
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_steamHLBoundFBO);

		// Bind the FBO to use
		glBindFramebuffer(GL_FRAMEBUFFER, g_stencilFBO);
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
		glViewport(0, 0, ScreenWidth, ScreenHeight);
	}

	// Might be using hacky DLL
	glClear( GL_STENCIL_BUFFER_BIT );
}

/*
====================
SVD_DrawTransparentTriangles

====================
*/
void SVD_DrawTransparentTriangles ( void )
{
	if(g_StudioRenderer.m_pCvarDrawStencilShadows->value < 1)
		return;

	if(IEngineStudio.IsHardware() != 1)
		return;

	glPushAttrib(GL_TEXTURE_BIT);

	// buz: workaround half-life's bug, when multitexturing left enabled after
	// rendering brush entities
	glActiveTexture( GL_TEXTURE1_ARB );
	glDisable(GL_TEXTURE_2D);
	glActiveTexture( GL_TEXTURE0_ARB );

	glDepthMask(GL_FALSE);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(GL_ZERO, GL_ZERO, GL_ZERO, 0.5);
	glDepthFunc(GL_EQUAL);

	glStencilFunc(GL_NOTEQUAL, 0, ~0);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glEnable(GL_STENCIL_TEST);

	// get current visframe number
	g_pWorld = IEngineStudio.GetModelByIndex(1);
	mleaf_t *pleaf = Mod_PointInLeaf ( g_viewOrigin, g_pWorld );
	g_visFrame = pleaf->visframe;

	// get current frame number
	g_frameCount = g_StudioRenderer.m_nFrameCount;

	// draw world
	SVD_RecursiveDrawWorld( g_pWorld->nodes );
	
#if 0 // Unfortunately there is a bug with some brushmodels, so this is not supported until I find a fix
	// Now draw brushmodels
	R_SetFrustum(g_viewAngles, g_viewOrigin, gHUD.m_iFOV, 16384);

	// Get local player
	cl_entity_t* plocalplayer = gEngfuncs.GetLocalPlayer();

	for(int i = 1; i < MAX_RENDER_ENTITIES; i++)
	{
		cl_entity_t* pentity = gEngfuncs.GetEntityByIndex(i);
		if(!pentity)
			break;

		if(!pentity->model || pentity->model->type != mod_brush)
			continue;

		if(pentity->curstate.messagenum != plocalplayer->curstate.messagenum)
			continue;

		if(pentity->curstate.rendermode != kRenderNormal)
			continue;

		SVD_DrawBrushModel(pentity);
	}
#endif
	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_STENCIL_TEST);
	glDepthFunc(GL_LEQUAL);

	glPopAttrib();

	SVD_PerformFBOBlit();
}

/*
====================
SVD_PerformFBOBlit

====================
*/
void SVD_PerformFBOBlit ( void )
{
	// If supported, use FBO
	if(!g_bFBOSupported)
		return;

	// Blit from main FBO to main renderbuffer or intermediate
	glBindFramebuffer(GL_READ_FRAMEBUFFER, g_stencilFBO);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	if(g_useMSAA && g_msaaSetting > 0)
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_intermediateFBO);
	else if(g_steamHLBoundFBO)
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_steamHLBoundFBO);
	else
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	glBlitFramebuffer(0, 0, ScreenWidth, ScreenHeight, 0, 0, ScreenWidth, ScreenHeight, GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	if(g_useMSAA && g_msaaSetting > 0)
	{
		// Blit from intermediate to target
		glBindFramebuffer(GL_READ_FRAMEBUFFER, g_intermediateFBO);

		if(g_steamHLBoundFBO)
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_steamHLBoundFBO);
		else
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

		glBlitFramebuffer(0, 0, ScreenWidth, ScreenHeight, 0, 0, ScreenWidth, ScreenHeight, GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	if(g_steamHLBoundFBO)
		glBindFramebuffer(GL_FRAMEBUFFER, g_steamHLBoundFBO);
	else
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/*
====================
R_DetermineSurfaceStructSize

====================
*/
int R_DetermineSurfaceStructSize( void )
{
	model_t* pworld = IEngineStudio.GetModelByIndex(1);
	assert(pworld);

	mplane_t* pplanes = pworld->planes;
	msurface_t* psurfaces = pworld->surfaces;

	// Try to find second texinfo ptr
	byte* psecondsurfbytedata = reinterpret_cast<byte*>(&psurfaces[1]);

	// Size of msurface_t with that stupid displaylist junk
	static const int MAXOFS = 108;

	int byteoffs = 0;
	while(byteoffs <= MAXOFS)
	{
		mplane_t **pplaneptr = reinterpret_cast<mplane_t**>(psecondsurfbytedata+byteoffs);
		
		int i = 0;
		for(; i < pworld->numplanes; i++)
		{
			if(&pplanes[i] == *pplaneptr)
				break;
		}

		if(i != pworld->numplanes)
		{
			break;
		}

		byteoffs++;
	}

	if(byteoffs >= MAXOFS)
	{
		gEngfuncs.Con_Printf("%s - Failed to determine msurface_t struct size.\n");
		return sizeof(msurface_t);
	}
	else
	{
		mplane_t** pfirstsurftexinfoptr = &psurfaces[0].plane;
		byte* psecondptr = reinterpret_cast<byte*>(psecondsurfbytedata) + byteoffs;
		byte* ptr = reinterpret_cast<byte*>(pfirstsurftexinfoptr);
		return ((unsigned int)psecondptr - (unsigned int)ptr);
	}
}