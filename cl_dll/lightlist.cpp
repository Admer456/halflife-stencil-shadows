/***+
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// Ammo.cpp
//
// implementation of CHudAmmo class
//

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "pm_shared.h"

#include <string.h>
#include <stdio.h>
#include "lightlist.h"

#include "pmtrace.h"
#include "r_efx.h"
#include "event_api.h"
#include "event_args.h"
#include "in_defs.h"
#include "pm_defs.h"

#include "lightlist.h"
#include "com_model.h"
#include "r_studioint.h"
#include "svd_render.h"

#define GLEW_STATIC 1
#include "GL/glew.h"

// Class declaration
CLightList gLightList;

// msurface_t struct size
extern int g_msurfaceStructSize;

extern engine_studio_api_t IEngineStudio;

//===========================================
// Con_Printf
//
//===========================================
char* COM_ReadLine( char* pstr, char* pstrOut )
{
	int length = 0;
	while(*pstr && *pstr != '\n' && *pstr != '\r')
	{
		pstrOut[length] = *pstr;
		length++; pstr++;
	}

	pstrOut[length] = '\0';

	while(*pstr && (*pstr == '\n' || *pstr == '\r'))
		pstr++;

	if(!(*pstr))
		return NULL;
	else
		return pstr;
}

//===========================================
//
//
//===========================================
void COM_ToLowerCase( char* pstr )
{
	char* _pstr = pstr;
	while(*_pstr)
	{
		*_pstr = tolower(*_pstr);
		_pstr++;
	}
}

//===========================================
//
//
//===========================================
void __CmdFunc_MakeLight( void )
{
	Vector lightColor;
	int entIndex = atoi(gEngfuncs.Cmd_Argv(1));
	lightColor[0] = atof(gEngfuncs.Cmd_Argv(2))/255.0f;
	lightColor[1] = atof(gEngfuncs.Cmd_Argv(3))/255.0f;
	lightColor[2] = atof(gEngfuncs.Cmd_Argv(4))/255.0f;
	float radius = atof(gEngfuncs.Cmd_Argv(5));

	gLightList.AddLight(entIndex, gHUD.m_vecOrigin, lightColor, radius, false);
}

//===========================================
//
//
//===========================================
int __MsgFunc_LightSource( const char *pszName, int iSize, void *pBuf )
{
	return gLightList.MsgFunc_LightSource( pszName, iSize, pBuf );
}

//===========================================
//
//
//===========================================
void CLightList::Init( void )
{
	// Create debug fn
	gEngfuncs.pfnAddCommand("make_light", __CmdFunc_MakeLight);

	// Register client message
	HOOK_MESSAGE(LightSource);

	m_pCvarDebugELights = CVAR_CREATE( "r_debug_lights", "0", FCVAR_CLIENTDLL );
}

/*
====================
VidInit

====================
*/
void CLightList::VidInit( void )
{
	// Clear entity lights array
	memset(m_pEntityLights, 0, sizeof(m_pEntityLights));
	m_iNumEntityLights = 0;

	// Clear tempentity lights array
	memset(m_pTempEntityLights, 0, sizeof(m_pTempEntityLights));
	m_iNumTempEntityLights = 0;

	// Get pointer to first elight
	m_pGoldSrcELights = gEngfuncs.pEfxAPI->CL_AllocElight(0);
	m_pGoldSrcDLights = gEngfuncs.pEfxAPI->CL_AllocDlight(0);

	// Reset this
	m_readLightsRad = false;
}

/*
====================
AddEntityLight

====================
*/
void CLightList::AddLight( int entindex, const Vector& origin, const Vector& color, float radius, bool isTemporary )
{
	elight_t* plight = NULL;
	for(int i = 0; i < m_iNumEntityLights; i++)
	{
		if(m_pEntityLights[i].entindex == entindex)
		{
			plight = &m_pEntityLights[i];
			break;
		}
	}

	if(!plight)
	{
		if(!isTemporary)
		{
			if(m_iNumEntityLights == MAX_ENTITY_LIGHTS)
				return;

			plight = &m_pEntityLights[m_iNumEntityLights];
			m_iNumEntityLights++;
		}
		else
		{
			if(m_iNumTempEntityLights == MAX_GOLDSRC_ELIGHTS)
				return;

			plight = &m_pTempEntityLights[m_iNumTempEntityLights];
			m_iNumTempEntityLights++;
		}
	}

	plight->origin = origin;
	plight->color = color;
	plight->radius = radius;
	plight->entindex = entindex;
	plight->temporary = isTemporary; // we don't want te_elight to shadow

	for(int i = 0; i < 3; i++)
	{
		plight->mins[i] = plight->origin[i] - plight->radius;
		plight->maxs[i] = plight->origin[i] + plight->radius;
	}
}

/*
====================
AddEntityLight

====================
*/
void CLightList::RemoveLight( int entindex )
{
	for(int i = 0; i < m_iNumEntityLights; i++)
	{
		if(m_pEntityLights[i].entindex == entindex)
		{
			for(int j = i; j < m_iNumEntityLights-1; j++)
				m_pEntityLights[j] = m_pEntityLights[j+1];

			m_iNumEntityLights--;
			return;
		}
	}
}

/*
====================
GetLightList

====================
*/
void CLightList::GetLightList( Vector& origin, const Vector& mins, const Vector& maxs, elight_t** lightArray, unsigned int* numLights )
{
	// Set this to zero
	*numLights = NULL;

	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );

	for(int i = 0; i < m_iNumEntityLights; i++)
	{
		if((*numLights) == MAX_MODEL_ENTITY_LIGHTS)
			return;

		elight_t* plight = &m_pEntityLights[i];

		if(CheckBBox(plight, mins, maxs))
			continue;

		static pmtrace_t traceResult;
		gEngfuncs.pEventAPI->EV_PlayerTrace( (float *)origin, (float *)plight->origin, PM_WORLD_ONLY, -1, &traceResult );
		if(traceResult.fraction != 1.0 || traceResult.allsolid || traceResult.startsolid)
			continue;

		lightArray[*numLights] = plight;
		(*numLights)++;
	}

	for(int i = 0; i < m_iNumTempEntityLights; i++)
	{
		if((*numLights) == MAX_MODEL_ENTITY_LIGHTS)
			return;

		elight_t* plight = &m_pTempEntityLights[i];

		if(CheckBBox(plight, mins, maxs))
			continue;

		static pmtrace_t traceResult;
		gEngfuncs.pEventAPI->EV_PlayerTrace( (float *)origin, (float *)plight->origin, PM_WORLD_ONLY, -1, &traceResult );
		if(traceResult.fraction != 1.0 || traceResult.allsolid || traceResult.startsolid)
			continue;

		lightArray[*numLights] = plight;
		(*numLights)++;
	}
}

/*
====================
CheckBBox

====================
*/
bool CLightList::CheckBBox( elight_t* plight, const Vector& vmins, const Vector& vmaxs )
{
	if (vmins[0] > plight->maxs[0]) 
		return true;

	if (vmins[1] > plight->maxs[1]) 
		return true;

	if (vmins[2] > plight->maxs[2]) 
		return true;

	if (vmaxs[0] < plight->mins[0]) 
		return true;

	if (vmaxs[1] < plight->mins[1]) 
		return true;

	if (vmaxs[2] < plight->mins[2]) 
		return true;

	return false;
}

/*
====================
CalcRefDef

====================
*/
void CLightList::CalcRefDef( void )
{
	// Reset to zero
	m_iNumTempEntityLights = 0;

	float fltime = gEngfuncs.GetClientTime();

	dlight_t* pdlight = m_pGoldSrcELights;
	for(int i = 0; i < MAX_GOLDSRC_ELIGHTS; i++, pdlight++)
	{
		if ( pdlight == nullptr )
		{
			break;
		}

		if (!pdlight->radius || pdlight->die < fltime)
		{
			continue;
		}

		Vector lightColor;
		lightColor.x = (float)pdlight->color.r/255.0f;
		lightColor.y = (float)pdlight->color.g/255.0f;
		lightColor.z = (float)pdlight->color.b/255.0f;

		AddLight( pdlight->key, pdlight->origin, lightColor, pdlight->radius*8, true );
	}

	pdlight = m_pGoldSrcDLights;
	for(int i = 0; i < MAX_GOLDSRC_DLIGHTS; i++, pdlight++)
	{
		if (pdlight == nullptr)
		{
			break;
		}

		if(!pdlight->radius || pdlight->die < fltime)
			continue;

		Vector lightColor;
		lightColor.x = (float)pdlight->color.r/255.0f;
		lightColor.y = (float)pdlight->color.g/255.0f;
		lightColor.z = (float)pdlight->color.b/255.0f;

		AddLight( pdlight->key, pdlight->origin, lightColor, pdlight->radius, true );
	}

	if(!m_readLightsRad)
	{
		ReadLightsRadFile();
		m_readLightsRad = true;
	}
}

/*
====================
DrawNormal

====================
*/
void CLightList::DrawNormal( void )
{
	if(m_pCvarDebugELights->value < 1)
		return;

	// Push texture state
	glPushAttrib(GL_TEXTURE_BIT);

	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);

	glActiveTexture(GL_TEXTURE2);
	glDisable(GL_TEXTURE_2D);

	glActiveTexture(GL_TEXTURE3);
	glDisable(GL_TEXTURE_2D);

	// Set the active texture unit
	glActiveTexture(GL_TEXTURE0);
	glDisable(GL_TEXTURE_2D);

	// Draw elight positions
	if(m_pCvarDebugELights->value >= 2)
		glDisable(GL_DEPTH_TEST);
	
	glPointSize(8.0);
	glBegin(GL_POINTS);
	for(int i = 0; i < m_iNumEntityLights; i++)
	{
		elight_t& el = m_pEntityLights[i];

		glColor4f(el.color.x, el.color.y, el.color.z, 1.0);
		glVertex3fv(el.origin);
	}
	glEnd();

	if(m_pCvarDebugELights->value >= 2)
		glEnable(GL_DEPTH_TEST);

	glPopAttrib();
}

/*
====================
MsgFunc_LightSource

====================
*/
int CLightList::MsgFunc_LightSource( const char *pszName, int iSize, void *pBuf )
{
	BEGIN_READ(pBuf, iSize);

	int entindex = READ_SHORT();
	bool active = READ_BYTE() ? true : false;

	if(!active)
	{
		RemoveLight(entindex);
		return 1;
	}

	Vector origin;
	for(int i = 0; i < 3; i++)
		origin[i] = READ_COORD();

	Vector color;
	for(int i = 0; i < 3; i++)
		color[i] = (float)READ_BYTE()/255.0f;

	float radius = READ_COORD()*9.5;
	AddLight(entindex, origin, color, radius, false);

	return 1;
}

/*
====================
ReadLightsRadFile

====================
*/
void CLightList::ReadLightsRadFile( void )
{
	m_numTexLights = 0;

	int length = 0;
	char* pFile = (char*)gEngfuncs.COM_LoadFile("lights.rad", 5, &length);
	if(!pFile)
		return;

	char szLine[1024];
	char szToken[128];

	char* pstr = pFile;
	while(pstr)
	{
		pstr = COM_ReadLine(pstr, szLine);
		if(!pstr)
			break;

		if(strlen(szLine) <= 0)
			continue;

		if(!strncmp(szLine, "//", 2))
			continue;

		char textureName[64];
		int colorR = 0;
		int colorG = 0;
		int colorB = 0;
		int strength = 0;

		// Get light texture name
		char* plstr = gEngfuncs.COM_ParseFile(szLine, textureName);
		if(!plstr)
		{
			gEngfuncs.Con_Printf("Incomplete entry in lights.rad for '%s'.\n", textureName);
			continue;
		}

		// Get color elements
		plstr = gEngfuncs.COM_ParseFile(plstr, szToken);
		if(!plstr)
		{
			gEngfuncs.Con_Printf("Incomplete entry in lights.rad for '%s'.\n", textureName);
			continue;
		}

		colorR = atoi(szToken);
		if(colorR < 0)
			colorR = 0;
		else if(colorR > 255)
			colorR = 255;

		plstr = gEngfuncs.COM_ParseFile(plstr, szToken);
		if(!plstr)
		{
			gEngfuncs.Con_Printf("Incomplete entry in lights.rad for '%s'.\n", textureName);
			continue;
		}

		colorG = atoi(szToken);
		if(colorG < 0)
			colorG = 0;
		else if(colorG > 255)
			colorG = 255;

		plstr = gEngfuncs.COM_ParseFile(plstr, szToken);
		if(!plstr)
		{
			gEngfuncs.Con_Printf("Incomplete entry in lights.rad for '%s'.\n", textureName);
			continue;
		}

		colorB = atoi(szToken);
		if(colorB < 0)
			colorB = 0;
		else if(colorB > 255)
			colorB = 255;

		// Get strength
		plstr = gEngfuncs.COM_ParseFile(plstr, szToken);

		strength = atoi(szToken);
		if(strength < 0)
			strength = 0;

		if(m_numTexLights >= MAX_TEXLIGHTS)
		{
			gEngfuncs.Con_Printf("Exceeded MAX_TEXLIGHTS\n");
			break;
		}

		texlight_t* pnew = &m_texLights[m_numTexLights];
		m_numTexLights++;

		strcpy(pnew->texname, textureName);
		COM_ToLowerCase(pnew->texname);
		pnew->colorr = colorR;
		pnew->colorg = colorG;
		pnew->colorb = colorB;
		pnew->strength = strength;
	}

	gEngfuncs.COM_FreeFile(pFile);

	// Go through the world and find surfaces tied to this
	const model_t* pWorld = IEngineStudio.GetModelByIndex(1);
	if(!pWorld)
		return;

	char texName[64];

	// Array of light-emitting surfaces
	std::vector<lightsurface_t> lightSurfacesVector;

	if(!g_msurfaceStructSize)
		g_msurfaceStructSize = R_DetermineSurfaceStructSize();

	// Parse through all surfaces and build lit surface array
	byte* pfirstsurfaceptr = reinterpret_cast<byte*>(pWorld->surfaces);
	for(int i = 0; i < pWorld->numsurfaces; i++)
	{
		msurface_t* psurface = reinterpret_cast<msurface_t*>(pfirstsurfaceptr + g_msurfaceStructSize*i);

		// See if surface binds to a texlight
		mtexinfo_t* ptexinfo = psurface->texinfo;
		if(!ptexinfo->texture)
			continue;

		strcpy(texName, ptexinfo->texture->name);
		COM_ToLowerCase(texName);

		texlight_t* ptexlight = NULL;
		for(int j = 0; j < m_numTexLights; j++)
		{
			if(!strcmp(m_texLights[j].texname, texName))
			{
				ptexlight = &m_texLights[j];
				break;
			}
		}

		if(!ptexlight)
			continue;

		unsigned int j = 0;
		for(j = 0; j < lightSurfacesVector.size(); j++)
		{
			lightsurface_t& lsurf = lightSurfacesVector[j];
			if(lsurf.ptexture != ptexinfo->texture)
				continue;

			if((lsurf.normal - psurface->plane->normal).Length() > 0.01)
				continue;
			
			unsigned int k = 0;
			for(; k < lsurf.verts.size(); k++)
			{
				int l = 0;
				for(; l < psurface->polys->numverts; l++)
				{
					Vector vcoord = Vector(psurface->polys->verts[l][0],
						psurface->polys->verts[l][1],
						psurface->polys->verts[l][2]);
			
					if(vcoord == lsurf.verts[k])
						break;
				}
			
				if(l != psurface->polys->numverts)
					break;
			}

			if(k != lsurf.verts.size())
			{
				for(int k = 0; k < psurface->polys->numverts; k++)
				{
					Vector vcoord = Vector((float)psurface->polys->verts[k][0],
						(float)psurface->polys->verts[k][1],
						(float)psurface->polys->verts[k][2]);
			
					lsurf.verts.push_back(vcoord);
			
					for(int l = 0; l < 3; l++)
					{
						if(vcoord[l] < lsurf.mins[l])
							lsurf.mins[l] = vcoord[l];
			
						if(vcoord[l] > lsurf.maxs[l])
							lsurf.maxs[l] = vcoord[l];
					}
				}
			
				break;
			}
		}

		if(j == lightSurfacesVector.size())
		{
			// Create new entry
			lightSurfacesVector.resize(lightSurfacesVector.size()+1);
			lightsurface_t& lsurf = lightSurfacesVector[j];
			lsurf.mins = Vector(999999, 999999, 999999);
			lsurf.maxs = Vector(-999999, -999999, -999999);
			lsurf.normal = psurface->plane->normal;
			lsurf.ptexture = psurface->texinfo->texture;
			lsurf.ptexlight = ptexlight;

			if(psurface->flags & SURF_PLANEBACK)
				VectorInverse(lsurf.normal);
			
			for(j = 0; j < (unsigned int)psurface->polys->numverts; j++)
			{
				Vector vcoord = Vector((float)psurface->polys->verts[j][0],
					(float)psurface->polys->verts[j][1],
					(float)psurface->polys->verts[j][2]);
			
				lsurf.verts.push_back(vcoord);
			
				for(int l = 0; l < 3; l++)
				{
					if(vcoord[l] < lsurf.mins[l])
						lsurf.mins[l] = vcoord[l];
			
					if(vcoord[l] > lsurf.maxs[l])
						lsurf.maxs[l] = vcoord[l];
				}
			}
		}
	}

	int numStuckInSolid = 0;
	for(unsigned int i = 0; i < lightSurfacesVector.size(); i++)
	{
		if(m_iNumEntityLights == MAX_ENTITY_LIGHTS)
		{
			gEngfuncs.Con_Printf("Exceeded MAX_ENTITY_LIGHTS.\n");
			break;
		}

		lightsurface_t& lsurf = lightSurfacesVector[i];
		Vector surfaceCenter = (lsurf.mins + lsurf.maxs) * 0.5;
		Vector lightOrigin = surfaceCenter + lsurf.normal * 4.0;
		
		int contents = gEngfuncs.PM_PointContents(lightOrigin, NULL);
		if(contents == CONTENTS_SOLID)
		{
			numStuckInSolid++;
			continue;
		}

		elight_t* pel = &m_pEntityLights[m_iNumEntityLights];
		m_iNumEntityLights++;

		pel->origin = lightOrigin;
		pel->color.x = (float)lsurf.ptexlight->colorr / 255.0f;
		pel->color.y = (float)lsurf.ptexlight->colorg / 255.0f;
		pel->color.z = (float)lsurf.ptexlight->colorb / 255.0f;
		
		pel->temporary = false;
		pel->entindex = -1;
		
		float colorStrength = (pel->color.x + pel->color.y + pel->color.z) / 3.0f;
		if(colorStrength < 0.3)
			colorStrength = 0.3;

		pel->radius = lsurf.ptexlight->strength * 0.06 * colorStrength;
		if(pel->radius > 512)
			pel->radius = 512;

		for(int j = 0; j < 3; j++)
		{
			pel->mins[j] = pel->origin[j] - pel->radius;
			pel->maxs[j] = pel->origin[j] + pel->radius;
		}
	}

	// Merge matching light sources that are very close
	int numOptimized = 0;
	for(int i = 0; i < m_iNumEntityLights; i++)
	{
		elight_t* pel1 = &m_pEntityLights[i];
		if(pel1->entindex != -1)
			continue;

		for(int j = 0; j < m_iNumEntityLights; j++)
		{
			if(j == i)
				continue;

			elight_t* pel2 = &m_pEntityLights[j];
			if(pel2->entindex != -1)
				continue;

			if((pel1->color.x - pel2->color.x) > 0.01
				|| (pel1->color.y - pel2->color.y) > 0.01
				|| (pel1->color.z - pel2->color.z) > 0.01)
				continue;
				
			float dist = (pel2->origin - pel1->origin).Length();
			if(dist < 32)
			{
				for(int k = j+1; k < m_iNumEntityLights; k++)
					m_pEntityLights[k-1] = m_pEntityLights[k];

				numOptimized++;
				m_iNumEntityLights--;
				j--;
			}
		}
	}

	// Check for lights being too near eachother, dim those that are too close
	int numDimmed = 0;
	for(int i = 0; i < m_iNumEntityLights; i++)
	{
		elight_t* pel1 = &m_pEntityLights[i];
		if(pel1->entindex != -1)
			continue;

		int closeMatchCount = 0;
		for(int j = 0; j < m_iNumEntityLights; j++)
		{
			if(j == i)
				continue;

			elight_t* pel2 = &m_pEntityLights[j];
			if(pel2->entindex != -1)
				continue;

			if((pel1->color.x - pel2->color.x) > 0.01
				|| (pel1->color.y - pel2->color.y) > 0.01
				|| (pel1->color.z - pel2->color.z) > 0.01)
				continue;
				
			float dist = (pel2->origin - pel1->origin).Length();
			if(dist < 64)
				closeMatchCount++;
		}

		if(closeMatchCount > 0)
		{
			float adjust = 1.0f / (float)(closeMatchCount + 1);
			VectorScale(pel1->color, adjust, pel1->color);
			numDimmed++;
		}
	}

#ifdef DEBUG
	gEngfuncs.Con_Printf("Removed %d per-vertex lights stuck in solids.\n", numStuckInSolid);
	gEngfuncs.Con_Printf("Removed %d clumped matching per-vertex lights.\n", numOptimized);
	gEngfuncs.Con_Printf("Dimmed %d near matching per-vertex lights.\n", numDimmed);
#endif
}