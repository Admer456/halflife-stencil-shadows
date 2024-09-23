//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#ifndef LIGHT_LIST_H
#define LIGHT_LIST_H

#include "elight.h"
#include "dlight.h"
#include <vector>
#include "com_model.h"

#define MAX_ENTITY_LIGHTS		1024 // Arbitrary
#define MAX_GOLDSRC_ELIGHTS		256 // Actual limit, based on IDA pro
#define MAX_GOLDSRC_DLIGHTS		32 // Actual limit, based on IDA pro
#define MAX_TEXLIGHTS			1024 // Arbitrary

/*
====================
CLightList

====================
*/
class CLightList
{
public:
	struct texlight_t
	{
		char texname[64];
		int colorr;
		int colorg;
		int colorb;
		int strength;
	};

	struct lightsurface_t
	{
		texture_t* ptexture;
		texlight_t* ptexlight;
		Vector normal;

		Vector mins;
		Vector maxs;

		std::vector<Vector> verts;
	};

public:
	// Called by HUD::Init
	void Init( void );
	// Called by HUD::VidInit
	void VidInit( void );
	// Run calcrefdef funcs
	void CalcRefDef( void );
	// Loads the lights.rad file
	void ReadLightsRadFile( void );
	// For debugging lights
	void DrawNormal( void );

	// Message function for a light source
	int MsgFunc_LightSource( const char *pszName, int iSize, void *pBuf );

	// Adds a light to the list
	void AddLight(int entindex, const Vector& origin, const Vector& color, float radius, bool isTemporary);
	// Removes a light from the list
	void RemoveLight( int entindex );

	// Returns a list of lights for an entity
	void GetLightList(Vector& origin, const Vector& mins, const Vector& maxs, elight_t** lightArray, unsigned int* numLights);

	// Performs bbox check for elight
	bool CheckBBox(elight_t* plight, const Vector& vmins, const Vector& vmaxs);

private:
	// Elights kept in memory
	elight_t	m_pEntityLights[MAX_ENTITY_LIGHTS];
	int			m_iNumEntityLights;

	// elights fetched from goldsrc
	elight_t	m_pTempEntityLights[MAX_GOLDSRC_ELIGHTS];
	int			m_iNumTempEntityLights;

	// Pointers to goldsrc's elight and dlight arrays
	dlight_t	*m_pGoldSrcELights;
	dlight_t	*m_pGoldSrcDLights;

	// Texture lights in level
	texlight_t	m_texLights[MAX_TEXLIGHTS];
	int			m_numTexLights;

	// True if we've read lights.rad
	bool		m_readLightsRad;

	// Shows elights rendered for the world
	cvar_t*		m_pCvarDebugELights;
};

extern CLightList gLightList;
#endif // ELIGHTLIST_H