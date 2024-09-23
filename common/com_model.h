//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================

// com_model.h

//  06/25/2002 MAH
//  This header file has been modified to now include the proper BSP model
//  structure definitions for each of the two Quakeworld client renderers:
//  software mode and GL mode. Originally, Valve only supplied it with
//  the software mode definitions, which caused General Protection Fault's
//  when accessing members of the structures that are different between
//  the two versions.  These are: 'mnode_t', 'mleaf_t', 'msurface_t' and
//  'texture_t'. To select the GL hardware rendering versions of these
//  structures, define 'HARDWARE_MODE' as a preprocessor symbol, otherwise
//  it will default to software mode as supplied.

#define HARDWARE_MODE

#pragma warning( disable: 4005 )
#pragma once

#define STUDIO_RENDER 1
#define STUDIO_EVENTS 2
#define STUDIO_BONES  4

#define MAX_EDICTS 2048

#define MAX_MODEL_NAME 64
#define MAX_MAP_HULLS 4
#define MIPLEVELS 4
#define NUM_AMBIENTS 4 // automatic ambient sounds
#define MAXLIGHTMAPS 4
#define PLANE_ANYZ 5

#define ALIAS_Z_CLIP_PLANE 5

// flags in finalvert_t.flags
#define ALIAS_LEFT_CLIP 0x0001
#define ALIAS_TOP_CLIP 0x0002
#define ALIAS_RIGHT_CLIP 0x0004
#define ALIAS_BOTTOM_CLIP 0x0008
#define ALIAS_Z_CLIP 0x0010
#define ALIAS_ONSEAM 0x0020
#define ALIAS_XY_CLIP_MASK 0x000F

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

#define ZISCALE ((float)0x8000)

#define CACHE_SIZE 32 // used to align key data structures

typedef enum
{
	mod_brush,
	mod_sprite,
	mod_alias,
	mod_studio
} modtype_t;

// must match definition in modelgen.h
#ifndef SYNCTYPE_T
#define SYNCTYPE_T

typedef enum
{
	ST_SYNC = 0,
	ST_RAND
} synctype_t;

#endif

//==============================
//		SPRITE MODEL DEFS
//
//==============================
typedef enum { SPR_SINGLE = 0, SPR_GROUP } spriteframetype_t;

#define SPR_VP_PARALLEL_UPRIGHT			0
#define SPR_FACING_UPRIGHT				1
#define SPR_VP_PARALLEL					2
#define SPR_ORIENTED					3
#define SPR_VP_PARALLEL_ORIENTED		4

#define SPR_NORMAL						0
#define SPR_ADDITIVE					1
#define SPR_INDEXALPHA					2
#define SPR_ALPHTEST					3

//==============================
//		SPRITE MODEL STRUCTS
//
//==============================

typedef struct mspriteframe_s
{
	int		width;
	int		height;
	float	up, down, left, right;
	int		gl_texturenum;
} mspriteframe_t;

typedef struct
{
	int				numframes;
	float* intervals;
	mspriteframe_t* frames[1];
} mspritegroup_t;

typedef struct
{
	spriteframetype_t	type;
	mspriteframe_t* frameptr;
} mspriteframedesc_t;

typedef struct
{
	short				type;
	short				texFormat;
	int					maxwidth;
	int					maxheight;
	int					numframes;
	int					radius;
	int					beamlength;
	int					synctype;
	mspriteframedesc_t	frames[1];
} msprite_t;


typedef struct
{
	float mins[3], maxs[3];
	float origin[3];
	int headnode[MAX_MAP_HULLS];
	int visleafs; // not including the solid leaf 0
	int firstface, numfaces;
} dmodel_t;

// plane_t structure
typedef struct mplane_s
{
	Vector normal; // surface normal
	float dist;	   // closest appoach to origin
	byte type;	   // for texture axis selection and fast side tests
	byte signbits; // signx + signy<<1 + signz<<1
	byte pad[2];
} mplane_t;

typedef struct
{
	Vector position;
} mvertex_t;

typedef struct
{
	unsigned short v[2];
	unsigned int cachededgeoffset;
} medge_t;


#ifndef     HARDWARE_MODE
//  06/23/2002 MAH
//  This structure is different between hardware and software mode
//
// software mode - QW 'model.h'
typedef struct texture_s
{
	char name[16];
	unsigned width, height;
	int anim_total;					   // total tenths in sequence ( 0 = no)
	int anim_min, anim_max;			   // time for this frame min <=time< max
	struct texture_s* anim_next;	   // in the animation sequence
	struct texture_s* alternate_anims; // bmodels in frame 1 use these
	unsigned offsets[MIPLEVELS];	   // four mip maps stored
	unsigned paloffset;
} texture_t;

#else
//
//  hardware mode - QW 'gl_model.h'
typedef struct texture_s
{
	char        name[16];
	unsigned    width, height;
	int         gl_texturenum;
	struct msurface_s* texturechain;  // for gl_texsort drawing
	int         anim_total;             // total tenths in sequence ( 0 = no)
	int         anim_min, anim_max;     // time for this frame min <=time< max
	struct texture_s* anim_next;        // in the animation sequence
	struct texture_s* alternate_anims;  // bmodels in frmae 1 use these
	unsigned    offsets[MIPLEVELS];     // four mip maps stored

} texture_t;

#endif

// 06/23/2002 MAH
// This structure is the same in QW source files
//  'model.h' and 'gl_model.h'
typedef struct
{
	float vecs[2][4]; // [s/t] unit vectors in world space.
					  // [i][3] is the s/t offset relative to the origin.
					  // s or t = dot(3Dpoint,vecs[i])+vecs[i][3]
	float mipadjust;  // ?? mipmap limits for very small surfaces
	texture_t* texture;
	int flags; // sky or slime, no lightmap or 256 subdivision
} mtexinfo_t;

#ifdef  HARDWARE_MODE
// 06/23/2002 MAH
// This structure is only need for hardware rendering
#define VERTEXSIZE  7

typedef struct glpoly_s
{
	struct  glpoly_s* next;
	struct  glpoly_s* chain;
	int     numverts;
	int     flags;          	// for SURF_UNDERWATER
	float verts[4][VERTEXSIZE];   // variable sized (xyz s1t1 s2t2)
} glpoly_t;

#endif


#ifndef HARDWARE_MODE
//  06/23/2002 MAH
//  This structure is different between hardware and software mode
//
//  Software Mode - QW 'model.h'
typedef struct mnode_s
{
	// common with leaf
	int contents; // 0, to differentiate from leafs
	int visframe; // node needs to be traversed if current

	short minmaxs[6]; // for bounding box culling


	struct mnode_s* parent;

	// node specific
	mplane_t* plane;
	struct mnode_s* children[2];

	unsigned short      firstsurface;
	unsigned short      numsurfaces;
} mnode_t;

#else
//
//  hardware mode - QW 'gl_model.h'
typedef struct mnode_s
{
	// common with leaf
	int             contents;       // 0, to differentiate from leafs
	int             visframe;       // node needs to be traversed if current

	float           minmaxs[6];     // for bounding box culling

	struct mnode_s* parent;

	// node specific
	mplane_t* plane;
	struct mnode_s* children[2];

	unsigned short firstsurface;
	unsigned short numsurfaces;
} mnode_t;

#endif


typedef struct msurface_s msurface_t;
typedef struct decal_s decal_t;

// JAY: Compress this as much as possible
struct decal_s
{
	decal_t* pnext;		  // linked list for each surface
	msurface_t* psurface; // Surface id for persistence / unlinking
	short dx;			  // Offsets into surface texture (in texture coordinates, so we don't need floats)
	short dy;
	short texture; // Decal texture
	byte scale;	   // Pixel scale
	byte flags;	   // Decal flags

	short entityIndex; // Entity this is attached to
};

#ifndef HARDWARE_MODE
//  06/23/2002 MAH
//  This structure is different between hardware and software mode
//
//  Software Mode - QW 'model.h'
typedef struct mleaf_s
{
	// common with node
	int contents; // wil be a negative contents number
	int visframe; // node needs to be traversed if current

	short minmaxs[6]; // for bounding box culling

	struct mnode_s* parent;

	// leaf specific
	byte* compressed_vis;
	struct efrag_s* efrags;

	msurface_t** firstmarksurface;
	int nummarksurfaces;
	int key; // BSP sequence number for leaf's contents
	byte ambient_sound_level[NUM_AMBIENTS];
} mleaf_t;
#else
//
//  hardware renderer - QW 'gl_model.h'
typedef struct mleaf_s
{
	// common with node
	int         contents;       // wil be a negative contents number
	int         visframe;       // node needs to be traversed if current

	float       minmaxs[6];     // for bounding box culling

	struct mnode_s* parent;

	// leaf specific
	byte* compressed_vis;
	struct efrag_s* efrags;

	msurface_t** firstmarksurface;
	int         nummarksurfaces;
	int			key;
	byte        ambient_sound_level[NUM_AMBIENTS];
} mleaf_t;
#endif

#ifndef     HARDWARE_MODE
//
//  software renderer - QW 'model.h'
struct msurface_s
{
	int visframe; // should be drawn when node is crossed

	int dlightframe; // last frame the surface was checked by an animated light
	int dlightbits;	 // dynamically generated. Indicates if the surface illumination
					 // is modified by an animated light.

	mplane_t* plane; // pointer to shared plane
	int flags;		 // see SURF_ #defines

	int firstedge; // look up in model->surfedges[], negative numbers
	int numedges;  // are backwards edges

	// surface generation data
	struct surfcache_s* cachespots[MIPLEVELS];

	short texturemins[2]; // smallest s/t position on the surface.
	short extents[2];	  // ?? s/t texture size, 1..256 for all non-sky surfaces

	mtexinfo_t* texinfo;

	// lighting info
	byte styles[MAXLIGHTMAPS]; // index into d_lightstylevalue[] for animated lights
							   // no one surface can be effected by more than 4
							   // animated lights.
	color24* samples;

	decal_t* pdecals;
};

#else
//
//  hardware renderer - QW 'gl_model.h'
//  06/23/2002 2230 MAH
//  WARNING - the above indicates this structure was modified
//      for Half-Life this structure needs VERIFICATION
//  06/23/2002 2300 MAH - the below version for hardware agrees
//      with a hexadecimal data dump of these structures taken
//      from a running game.


#ifdef HL25_UPDATE
// Thanks to *(int*)0 = 0XDEADBEEF for this info
typedef struct mdisplaylist_s
{
	unsigned int gl_displaylist;
	int rendermode;
	float scrolloffset;
	int renderDetailTexture;
} mdisplaylist_t;
#endif

typedef struct msurface_s
{
	int         visframe;       // should be drawn when node is crossed

	mplane_t* plane;
	int         flags;

	int         firstedge;  // look up in model->surfedges[], negative numbers
	int         numedges;   // are backwards edges

	short       texturemins[2];
	short       extents[2];

	int         light_s, light_t;           // gl lightmap coordinates

	glpoly_t* polys;                     // multiple if warped
	struct msurface_s* texturechain;

	mtexinfo_t* texinfo;

	// lighting info
	int         dlightframe;
	int         dlightbits;

	int         lightmaptexturenum;
	byte        styles[MAXLIGHTMAPS];
	int         cached_light[MAXLIGHTMAPS]; // values currently used in lightmap
	qboolean    cached_dlight;              // true if dynamic light in cache

	//  byte        *samples;                   // [numstyles*surfsize]
	color24* samples;                   // note: this is the actual lightmap data for this surface
	decal_t* pdecals;
} msurface_t;
#endif
typedef struct
{
	int planenum;
	short children[2]; // negative numbers are contents
} dclipnode_t;

typedef struct hull_s
{
	dclipnode_t* clipnodes;
	mplane_t* planes;
	int firstclipnode;
	int lastclipnode;
	Vector clip_mins;
	Vector clip_maxs;
} hull_t;

#if !defined(CACHE_USER) && !defined(QUAKEDEF_H)
#define CACHE_USER
typedef struct cache_user_s
{
	void* data;
} cache_user_t;
#endif

typedef struct model_s
{
	char name[MAX_MODEL_NAME];
	qboolean needload; // bmodels and sprites don't cache normally

	modtype_t type;
	int numframes;
	synctype_t synctype;

	int flags;

	//
	// volume occupied by the model
	//
	Vector mins, maxs;
	float radius;

	//
	// brush model
	//
	int firstmodelsurface, nummodelsurfaces;

	int numsubmodels;
	dmodel_t* submodels;

	int numplanes;
	mplane_t* planes;

	int numleafs; // number of visible leafs, not counting 0
	struct mleaf_s* leafs;

	int numvertexes;
	mvertex_t* vertexes;

	int numedges;
	medge_t* edges;

	int numnodes;
	mnode_t* nodes;

	int numtexinfo;
	mtexinfo_t* texinfo;

	int numsurfaces;
	msurface_t* surfaces;

	int numsurfedges;
	int* surfedges;

	int numclipnodes;
	dclipnode_t* clipnodes;

	int nummarksurfaces;
	msurface_t** marksurfaces;

	hull_t hulls[MAX_MAP_HULLS];

	int numtextures;
	texture_t** textures;

	byte* visdata;

	color24* lightdata;

	char* entities;

	//
	// additional model data
	//
	cache_user_t cache; // only access through Mod_Extradata

} model_t;

typedef vec_t vec4_t[4];

typedef struct alight_s
{
	int ambientlight; // clip at 128
	int shadelight;	  // clip at 192 - ambientlight
	Vector color;
	float* plightvec;
} alight_t;

typedef struct auxvert_s
{
	float fv[3]; // viewspace x, y
} auxvert_t;

#include "custom.h"

#define MAX_INFO_STRING 256
#define MAX_SCOREBOARDNAME 32
typedef struct player_info_s
{
	// User id on server
	int userid;

	// User info string
	char userinfo[MAX_INFO_STRING];

	// Name
	char name[MAX_SCOREBOARDNAME];

	// Spectator or not, unused
	int spectator;

	int ping;
	int packet_loss;

	// skin information
	char model[MAX_QPATH];
	int topcolor;
	int bottomcolor;

	// last frame rendered
	int renderframe;

	// Gait frame estimation
	int gaitsequence;
	float gaitframe;
	float gaityaw;
	Vector prevgaitorigin;

	customization_t customdata;

	char hashedcdkey[16];
	uint64 m_nSteamID;
} player_info_t;

extern mvertex_t* globalVertexTable;

