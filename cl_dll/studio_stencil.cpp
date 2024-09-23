//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

// studio_model.cpp
// routines for setting up to draw 3DStudio models

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "com_model.h"
#include "studio.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "dlight.h"
#include "triangleapi.h"
#include "lightlist.h"

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

#define GLEW_STATIC 1
#include "GL/glew.h"

// msurface_t struct size
extern int g_msurfaceStructSize;

// Global engine <-> studio model rendering code interface
extern engine_studio_api_t IEngineStudio;

void VectorRotate(const Vector& in1, float in2[3][4], Vector& out)
{
	out[0] = DotProduct(in1, in2[0]);
	out[1] = DotProduct(in1, in2[1]);
	out[2] = DotProduct(in1, in2[2]);
}

__forceinline float Q_rsqrt(float number)
{
	long i;
	float x2, y;
	const float threehalfs = 1.5F;

	x2 = number * 0.5F;
	y = number;
	i = *(long*)&y;                       // evil floating point bit level hacking
	i = 0x5f3759df - (i >> 1);               // what the fuck?
	y = *(float*)&i;
	y = y * (threehalfs - (x2 * y * y));   // 1st iteration

	return y;
}

void VectorNormalizeFast(float* v)
{
	float ilength = DotProduct(v, v);
	float sqroot = Q_rsqrt(ilength);
	VectorScale(v, sqroot, v);
}

//=============================================
// @brief
//
//=============================================
const msurface_t* Mod_SurfaceAtPoint( const model_t* pmodel, const mnode_t* pnode, const Vector& start, const Vector& end )
{
	if(pnode->contents < 0)
		return nullptr;

	mplane_t* pplane = pnode->plane;
	float front = DotProduct(start, pplane->normal) - pplane->dist;
	float back = DotProduct(end, pplane->normal) - pplane->dist;

	bool s = (front < 0.0f) ? true : false;
	bool t = (back < 0.0f) ? true : false;

	if(t == s)
		return Mod_SurfaceAtPoint(pmodel, pnode->children[s], start, end);

	Vector mid, point;
	float frac = front / (front - back);
	VectorSubtract(end, start, point);
	VectorMA(start, frac, point, mid);

	const msurface_t* psurface = Mod_SurfaceAtPoint(pmodel, pnode->children[s], start, mid);
	if(psurface)
		return psurface;

	byte* pfirstsurfaceptr = reinterpret_cast<byte*>(pmodel->surfaces);
	for(int i = 0; i < pnode->numsurfaces; i++)
	{
		msurface_t* psurface = reinterpret_cast<msurface_t*>(pfirstsurfaceptr + g_msurfaceStructSize*(pnode->firstsurface+i));
		mtexinfo_t *ptexinfo = psurface->texinfo;

		int ds = (int)(DotProduct(mid, ptexinfo->vecs[0]) + ptexinfo->vecs[0][3]);
		int dt = (int)(DotProduct(mid, ptexinfo->vecs[1]) + ptexinfo->vecs[1][3]);

		if(ds >= psurface->texturemins[0] && dt >= psurface->texturemins[1])
		{
			if((ds - psurface->texturemins[0]) <= psurface->extents[0] &&
				(dt - psurface->texturemins[1]) <= psurface->extents[1])
				return psurface;
		}
	}

	return Mod_SurfaceAtPoint(pmodel, pnode->children[s ^ 1], mid, end);
}

/*
====================
StudioGetMinsMaxs

====================
*/
void CStudioModelRenderer::StudioGetMinsMaxs ( Vector& outMins, Vector& outMaxs )
{
	if (m_pCurrentEntity->curstate.sequence >=  m_pStudioHeader->numseq) 
		m_pCurrentEntity->curstate.sequence = 0;

	// Build full bounding box
	mstudioseqdesc_t *pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + m_pCurrentEntity->curstate.sequence;

	Vector vTemp;
	static Vector vBounds[8];

	for (int i = 0; i < 8; i++)
	{
		if ( i & 1 ) vTemp[0] = pseqdesc->bbmin[0];
		else vTemp[0] = pseqdesc->bbmax[0];
		if ( i & 2 ) vTemp[1] = pseqdesc->bbmin[1];
		else vTemp[1] = pseqdesc->bbmax[1];
		if ( i & 4 ) vTemp[2] = pseqdesc->bbmin[2];
		else vTemp[2] = pseqdesc->bbmax[2];
		VectorCopy( vTemp, vBounds[i] );
	}

	float rotationMatrix[3][4];
	m_pCurrentEntity->angles[PITCH] = -m_pCurrentEntity->angles[PITCH];
	AngleMatrix(m_pCurrentEntity->angles, rotationMatrix);
	m_pCurrentEntity->angles[PITCH] = -m_pCurrentEntity->angles[PITCH];

	for (int i = 0; i < 8; i++ )
	{
		VectorCopy(vBounds[i], vTemp);
		VectorRotate(vTemp, rotationMatrix, vBounds[i]);
	}

	// Set the bounding box
	outMins = Vector(9999, 9999, 9999);
	outMaxs = Vector(-9999, -9999, -9999);
	for(int i = 0; i < 8; i++)
	{
		// Mins
		if(vBounds[i][0] < outMins[0]) outMins[0] = vBounds[i][0];
		if(vBounds[i][1] < outMins[1]) outMins[1] = vBounds[i][1];
		if(vBounds[i][2] < outMins[2]) outMins[2] = vBounds[i][2];

		// Maxs
		if(vBounds[i][0] > outMaxs[0]) outMaxs[0] = vBounds[i][0];
		if(vBounds[i][1] > outMaxs[1]) outMaxs[1] = vBounds[i][1];
		if(vBounds[i][2] > outMaxs[2]) outMaxs[2] = vBounds[i][2];
	}

	VectorAdd(outMins, m_pCurrentEntity->origin, outMins);
	VectorAdd(outMaxs, m_pCurrentEntity->origin, outMaxs);
}

/*
====================
StudioEntityLight

====================
*/
void CStudioModelRenderer::StudioGetLightSources()
{
	Vector mins, maxs;
	StudioGetMinsMaxs(mins, maxs);

	// Get elight list
	gLightList.GetLightList(m_pCurrentEntity->origin, mins, maxs, m_pEntityLights, &m_iNumEntityLights);

	// Reset this anyway
	m_iClosestLight = -1;

	if(!m_iNumEntityLights)
		return;

	Vector transOrigin;
	float flClosestDist = -1;

	// Find closest light origin
	for(unsigned int i = 0; i < m_iNumEntityLights; i++)
	{
		elight_t* plight = m_pEntityLights[i];

		if(!plight->temporary)
		{
			Vector& origin = m_pCurrentEntity->origin;
			if(plight->origin[0] > maxs[0] || plight->origin[1] > maxs[1] || plight->origin[2] > maxs[2]
				|| plight->origin[0] < mins[0] || plight->origin[1] < mins[1] || plight->origin[2] < mins[2])
			{
				float flDist = (plight->origin - origin).Length();
				if(flClosestDist == -1 || flClosestDist > flDist)
				{
					flClosestDist = flDist;
					m_iClosestLight = i;
				}
			}
		}
	}
}

/*
====================
StudioSetupShadows

====================
*/
void CStudioModelRenderer::StudioSetupShadows()
{
	if( IEngineStudio.IsHardware() != 1 )
		return;	
	
	// Determine the shading angle
	if(m_iClosestLight == -1)
	{
		Vector mins, maxs;
		StudioGetMinsMaxs(mins, maxs);

		Vector skyVec;
		skyVec[0] = m_pSkylightDirX->value;
		skyVec[1] = m_pSkylightDirY->value;
		skyVec[2] = m_pSkylightDirZ->value;
		skyVec = skyVec.Normalize();

		Vector center = mins *0.5 + maxs * 0.5;
		Vector end = center - skyVec * 8192;

		model_t* pworld = IEngineStudio.GetModelByIndex(1);
		const msurface_t* phitsurf = Mod_SurfaceAtPoint(pworld, pworld->nodes, center, end);
		
		Vector shadeVector;
		if(phitsurf && phitsurf->flags & SURF_DRAWSKY)
		{
			// Hit by sky
			shadeVector = skyVec;
			VectorInverse(shadeVector);
		}
		else
		{
			
			shadeVector[0] = 0.3;
			shadeVector[1] = 0.5;
			shadeVector[2] = 1;
			shadeVector = shadeVector.Normalize();
		}

		m_vShadowLightVector = shadeVector;
		m_shadowLightType = SL_TYPE_LIGHTVECTOR;
	}
	else
	{
		elight_t* plight = m_pEntityLights[m_iClosestLight];
		m_vShadowLightOrigin = plight->origin;
		m_shadowLightType = SL_TYPE_POINTLIGHT;
	}
}

/*
====================
StudioSetupModelSVD

====================
*/
void CStudioModelRenderer::StudioSetupModelSVD( int bodypart )
{
	if (bodypart > m_pSVDHeader->numbodyparts)
		bodypart = 0;

	svdbodypart_t* pbodypart = (svdbodypart_t *)((byte *)m_pSVDHeader + m_pSVDHeader->bodypartindex) + bodypart;

	int index = m_pCurrentEntity->curstate.body / pbodypart->base;
	index = index % pbodypart->numsubmodels;

	m_pSVDSubModel = (svdsubmodel_t *)((byte *)m_pSVDHeader + pbodypart->submodelindex) + index;
}


/*
====================
StudioShouldDrawShadow

====================
*/
bool CStudioModelRenderer::StudioShouldDrawShadow()
{
	if(m_pCvarDrawStencilShadows->value < 1 )
		return false;

	if( IEngineStudio.IsHardware() != 1 )
		return false;

	if( !m_pRenderModel->visdata )
		return false;

	if(m_pCurrentEntity->curstate.renderfx == kRenderFxNoShadow)
		return false;

	// Fucking butt-ugly hack to make the shadows less annoying
	pmtrace_t tr;
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	gEngfuncs.pEventAPI->EV_PlayerTrace( m_vRenderOrigin, m_pCurrentEntity->origin+Vector(0, 0, 1), PM_WORLD_ONLY, -1, &tr);

	if(tr.fraction != 1.0)
		return false;

	return true;
}

/*
====================
StudioDrawShadow

====================
*/
void CStudioModelRenderer::StudioDrawShadow ()
{
	glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);

	// Disabable these to avoid slowdown bug
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	glClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glClientActiveTexture(GL_TEXTURE2);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glClientActiveTexture(GL_TEXTURE3);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glClientActiveTexture(GL_TEXTURE0);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer( 3, GL_FLOAT, sizeof(Vector), m_vertexTransform );
	glEnableClientState(GL_VERTEX_ARRAY);

	// Set SVD header
	m_pSVDHeader = (svdheader_t*)m_pRenderModel->visdata;

	glDepthMask(GL_FALSE);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // disable writes to color buffer

	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS, 0, ~0);

	if(m_bTwoSideSupported)
	{
		glDisable(GL_CULL_FACE);
		glEnable(GL_STENCIL_TEST_TWO_SIDE_EXT);
	}

	for (int i = 0; i < m_pStudioHeader->numbodyparts; i++)
	{
		StudioSetupModelSVD( i );
		StudioDrawShadowVolume( );
	}

	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDisable(GL_STENCIL_TEST);

	if(m_bTwoSideSupported)
	{
		glEnable(GL_CULL_FACE);
		glDisable(GL_STENCIL_TEST_TWO_SIDE_EXT);
	}

	glDisableClientState(GL_VERTEX_ARRAY);

	glPopClientAttrib();
}

/*
====================
StudioDrawShadowVolume

====================
*/
void CStudioModelRenderer::StudioDrawShadowVolume()
{
	float plane[4];
	Vector lightdir;
	Vector *pv1, *pv2, *pv3;

	if (!m_pSVDSubModel->numfaces)
		return;

	Vector *psvdverts = (Vector *)((byte *)m_pSVDHeader + m_pSVDSubModel->vertexindex);
	byte *pvertbone = ((byte *)m_pSVDHeader + m_pSVDSubModel->vertinfoindex);

	// Extrusion distance
	float extrudeDistance = m_pCvarShadowVolumeExtrudeDistance->value;

	// Calculate vertex coords
	if(m_shadowLightType == SL_TYPE_POINTLIGHT)
	{
		// For point light sources
		for (int i = 0, j = 0; i < m_pSVDSubModel->numverts; i++, j+=2)
		{
			VectorTransform(psvdverts[i], (*m_pbonetransform)[pvertbone[i]], m_vertexTransform[j]);

			VectorSubtract(m_vertexTransform[j], m_vShadowLightOrigin, lightdir);
			VectorNormalizeFast(lightdir);

			VectorMA(m_vertexTransform[j], extrudeDistance, lightdir, m_vertexTransform[j+1]);
		}
	}
	else
	{
		for (int i = 0, j = 0; i < m_pSVDSubModel->numverts; i++, j+=2)
		{
			VectorTransform(psvdverts[i], (*m_pbonetransform)[pvertbone[i]], m_vertexTransform[j]);
			VectorMA(m_vertexTransform[j], extrudeDistance, -m_vShadowLightVector, m_vertexTransform[j+1]);
		}
	}

	// Process the faces
	int numIndexes = 0;
	svdface_t* pfaces = (svdface_t*)((byte *)m_pSVDHeader + m_pSVDSubModel->faceindex);

	if(m_shadowLightType == SL_TYPE_POINTLIGHT)
	{
		// For point light sources
		for (int i = 0; i < m_pSVDSubModel->numfaces; i++)
		{
			pv1 = &m_vertexTransform[pfaces[i].vertex0];
			pv2 = &m_vertexTransform[pfaces[i].vertex1];
			pv3 = &m_vertexTransform[pfaces[i].vertex2];
		
			plane[0] = pv1->y * (pv2->z - pv3->z) + pv2->y * (pv3->z - pv1->z) + pv3->y * (pv1->z - pv2->z);
			plane[1] = pv1->z * (pv2->x - pv3->x) + pv2->z * (pv3->x - pv1->x) + pv3->z * (pv1->x - pv2->x);
			plane[2] = pv1->x * (pv2->y - pv3->y) + pv2->x * (pv3->y - pv1->y) + pv3->x * (pv1->y - pv2->y);
			plane[3] = -( pv1->x * (pv2->y * pv3->z - pv3->y * pv2->z) + pv2->x * ( pv3->y * pv1->z - pv1->y * pv3->z ) + pv3->x * ( pv1->y * pv2->z - pv2->y * pv1->z ) );

			m_trianglesFacingLight[i] = (DotProduct(plane, m_vShadowLightOrigin) + plane[3]) > 0;
			if (m_trianglesFacingLight[i])
			{
				m_shadowVolumeIndexes[numIndexes] = pfaces[i].vertex0;
				m_shadowVolumeIndexes[numIndexes + 1] = pfaces[i].vertex2;
				m_shadowVolumeIndexes[numIndexes + 2] = pfaces[i].vertex1;
											   
				m_shadowVolumeIndexes[numIndexes + 3] = pfaces[i].vertex0 + 1;
				m_shadowVolumeIndexes[numIndexes + 4] = pfaces[i].vertex1 + 1;
				m_shadowVolumeIndexes[numIndexes + 5] = pfaces[i].vertex2 + 1;

				numIndexes += 6;
			}
		}
	}
	else
	{
		// For a light vector
		for (int i = 0; i < m_pSVDSubModel->numfaces; i++)
		{
			pv1 = &m_vertexTransform[pfaces[i].vertex0];
			pv2 = &m_vertexTransform[pfaces[i].vertex1];
			pv3 = &m_vertexTransform[pfaces[i].vertex2];
			
			// Calculate normal of the face
			plane[0] = pv1->y * (pv2->z - pv3->z) + pv2->y * (pv3->z - pv1->z) + pv3->y * (pv1->z - pv2->z);
			plane[1] = pv1->z * (pv2->x - pv3->x) + pv2->z * (pv3->x - pv1->x) + pv3->z * (pv1->x - pv2->x);
			plane[2] = pv1->x * (pv2->y - pv3->y) + pv2->x * (pv3->y - pv1->y) + pv3->x * (pv1->y - pv2->y);

			m_trianglesFacingLight[i] = DotProduct(plane, m_vShadowLightVector) > 0;
			if (m_trianglesFacingLight[i])
			{
				m_shadowVolumeIndexes[numIndexes] = pfaces[i].vertex0;
				m_shadowVolumeIndexes[numIndexes + 1] = pfaces[i].vertex2;
				m_shadowVolumeIndexes[numIndexes + 2] = pfaces[i].vertex1;
											   
				m_shadowVolumeIndexes[numIndexes + 3] = pfaces[i].vertex0 + 1;
				m_shadowVolumeIndexes[numIndexes + 4] = pfaces[i].vertex1 + 1;
				m_shadowVolumeIndexes[numIndexes + 5] = pfaces[i].vertex2 + 1;

				numIndexes += 6;
			}
		}
	}

	// Process the edges
	svdedge_t* pedges = (svdedge_t*)((byte *)m_pSVDHeader + m_pSVDSubModel->edgeindex);
	for (int i = 0; i < m_pSVDSubModel->numedges; i++)
	{
		if (m_trianglesFacingLight[pedges[i].face0])
		{
			if ((pedges[i].face1 != -1) && m_trianglesFacingLight[pedges[i].face1])
				continue;

			m_shadowVolumeIndexes[numIndexes] = pedges[i].vertex0;
			m_shadowVolumeIndexes[numIndexes + 1] = pedges[i].vertex1;
		}
		else
		{
			if ((pedges[i].face1 == -1) || !m_trianglesFacingLight[pedges[i].face1])
				continue;

			m_shadowVolumeIndexes[numIndexes] = pedges[i].vertex1;
			m_shadowVolumeIndexes[numIndexes + 1] = pedges[i].vertex0;
		}

		m_shadowVolumeIndexes[numIndexes + 2] = m_shadowVolumeIndexes[numIndexes] + 1;
		m_shadowVolumeIndexes[numIndexes + 3] = m_shadowVolumeIndexes[numIndexes + 2];
		m_shadowVolumeIndexes[numIndexes + 4] = m_shadowVolumeIndexes[numIndexes + 1];
		m_shadowVolumeIndexes[numIndexes + 5] = m_shadowVolumeIndexes[numIndexes + 1] + 1;
		numIndexes += 6;
	}

	if(m_bTwoSideSupported)
	{
		glActiveStencilFaceEXT(GL_BACK);
		glStencilOp(GL_KEEP, GL_INCR_WRAP_EXT, GL_KEEP);
		glStencilMask(~0);

		glActiveStencilFaceEXT(GL_FRONT);
		glStencilOp(GL_KEEP, GL_DECR_WRAP_EXT, GL_KEEP);
		glStencilMask(~0);

		glDrawElements(GL_TRIANGLES, numIndexes, GL_UNSIGNED_SHORT, m_shadowVolumeIndexes);
	}
	else
	{
		// draw back faces incrementing stencil values when z fails
		glStencilOp(GL_KEEP, GL_INCR, GL_KEEP);
		glCullFace(GL_BACK);
		glDrawElements(GL_TRIANGLES, numIndexes, GL_UNSIGNED_SHORT, m_shadowVolumeIndexes);

		// draw front faces decrementing stencil values when z fails
		glStencilOp(GL_KEEP, GL_DECR, GL_KEEP);
		glCullFace(GL_FRONT);
		glDrawElements(GL_TRIANGLES, numIndexes, GL_UNSIGNED_SHORT, m_shadowVolumeIndexes);
	}
}
