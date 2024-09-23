//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#ifndef ELIGHT_H
#define ELIGHT_H

#define MAX_MODEL_ENTITY_LIGHTS	32

struct elight_t
{
	int entindex;

	Vector origin;
	Vector color;
	float radius;

	Vector mins;
	Vector maxs;

	bool temporary;
};
#endif