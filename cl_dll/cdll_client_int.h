//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef CDLL_CLIENT_INT_H
#define CDLL_CLIENT_INT_H
#ifdef _WIN32
#pragma once
#endif


#include "iclientnetworkable.h"
#include "utllinkedlist.h"
#include "cdll_int.h"


class IVModelRender;
class IVEngineClient;
class IVModelRender;
class IVEfx;
class ICvar;
class IVRenderView;
class IVDebugOverlay;
class IMaterialSystem;
class IMaterialSystemStub;
class IVEngineCache;
class IVModelInfoClient;
class IEngineVGui;
class ISpatialPartition;
class IBaseClientDLL;
class ISpatialPartition;
class IFileSystem;
class IStaticPropMgrClient;
class IShadowMgr;
class IUniformRandomStream;
class CGaussianRandomStream;
class IEngineSound;
class IMatSystemSurface;
class IMaterialSystemHardwareConfig;
class ISharedGameRules;
class IEngineTrace;
class IGameUIFuncs;
class IGameEventManager;
class IPhysicsGameTrace;
class CGlobalVarsBase;

extern IVModelRender *modelrender;
extern IVEngineClient	*engine;
extern IVModelRender *modelrender;
extern IVEfx *effects;
extern ICvar *cvar;
extern IVRenderView *render;
extern IVDebugOverlay *debugoverlay;
extern IMaterialSystem *materials;
extern IMaterialSystemStub *materials_stub;
extern IMaterialSystemHardwareConfig *g_pMaterialSystemHardwareConfig;
extern IVEngineCache *engineCache;
extern IVModelInfoClient *modelinfo;
extern IEngineVGui *enginevgui;
extern ISpatialPartition* partition;
extern IBaseClientDLL *clientdll;
extern IFileSystem *filesystem;
extern IStaticPropMgrClient *staticpropmgr;
extern IShadowMgr *shadowmgr;
extern IUniformRandomStream *random;
extern CGaussianRandomStream *randomgaussian;
extern IEngineSound *enginesound;
extern IMatSystemSurface *g_pMatSystemSurface;
extern IEngineTrace *enginetrace;
extern IGameUIFuncs *gameuifuncs;
extern IGameEventManager *gameeventmanager;
extern IPhysicsGameTrace *physgametrace;
extern CGlobalVarsBase *gpGlobals;

// Set to true between LevelInit and LevelShutdown.
extern bool	g_bLevelInitialized;
extern bool g_bTextMode;


// Returns true if a new OnDataChanged event is registered for this frame.
bool AddDataChangeEvent( IClientNetworkable *ent, DataUpdateType_t updateType, int *pStoredEvent );

void ClearDataChangedEvent( int iStoredEvent );

//-----------------------------------------------------------------------------
// Precaches a material
//-----------------------------------------------------------------------------
void PrecacheMaterial( const char *pMaterialName );

//-----------------------------------------------------------------------------
// Converts a previously precached material into an index
//-----------------------------------------------------------------------------
int GetMaterialIndex( const char *pMaterialName );

//-----------------------------------------------------------------------------
// Converts precached material indices into strings
//-----------------------------------------------------------------------------
const char *GetMaterialNameFromIndex( int nIndex );


#endif // CDLL_CLIENT_INT_H
