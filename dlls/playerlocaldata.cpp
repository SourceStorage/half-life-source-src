//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "playerlocaldata.h"
#include "player.h"
#include "mathlib.h"
#include "entitylist.h"
#include "SkyCamera.h"
#include "playernet_vars.h"
#include "fogcontroller.h"
#include "tier0/vprof.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//=============================================================================

BEGIN_SEND_TABLE_NOBASE( CPlayerLocalData, DT_Local )

	SendPropArray3  (SENDINFO_ARRAY3(m_chAreaBits), SendPropInt(SENDINFO_ARRAY(m_chAreaBits), 8, SPROP_UNSIGNED)),
	
	SendPropInt		(SENDINFO(m_iHideHUD), HIDEHUD_BITCOUNT, SPROP_UNSIGNED),
	SendPropInt		(SENDINFO(m_iFOV),	9),
	SendPropFloat	(SENDINFO(m_flFOVRate), 0, SPROP_NOSCALE ),
	SendPropInt		(SENDINFO(m_iDefaultFOV),	9),

	SendPropInt		(SENDINFO(m_bDucked),	1, SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_bDucking),	1, SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_bInDuckJump),	1, SPROP_UNSIGNED ),
	SendPropFloat	(SENDINFO(m_flDucktime), 12, SPROP_ROUNDDOWN|SPROP_CHANGES_OFTEN, 0.0f, 2048.0f ),
	SendPropFloat	(SENDINFO(m_flDuckJumpTime), 12, SPROP_ROUNDDOWN, 0.0f, 2048.0f ),
	SendPropFloat	(SENDINFO(m_flJumpTime), 12, SPROP_ROUNDDOWN, 0.0f, 2048.0f ),
	SendPropFloat	(SENDINFO(m_flFallVelocity), 17, SPROP_CHANGES_OFTEN, -4096.0f, 4096.0f ),
//	SendPropInt		(SENDINFO(m_nOldButtons),	22, SPROP_UNSIGNED ),
	SendPropVector	(SENDINFO(m_vecClientBaseVelocity),      -1,  SPROP_COORD),
	SendPropVector	(SENDINFO(m_vecPunchAngle),      -1,  SPROP_COORD|SPROP_CHANGES_OFTEN),
	SendPropVector	(SENDINFO(m_vecPunchAngleVel),      -1,  SPROP_COORD),
	SendPropInt		(SENDINFO(m_bDrawViewmodel), 1, SPROP_UNSIGNED ),
	SendPropInt		(SENDINFO(m_bWearingSuit), 1, SPROP_UNSIGNED ),
	SendPropBool	(SENDINFO(m_bPoisoned)),

	SendPropFloat	(SENDINFO(m_flStepSize), 16, SPROP_ROUNDUP, 0.0f, 128.0f ),
	SendPropInt		(SENDINFO(m_bAllowAutoMovement),1, SPROP_UNSIGNED ),

	// 3d skybox data
	SendPropInt(SENDINFO_STRUCTELEM(m_skybox3d.scale), 12),
	SendPropVector	(SENDINFO_STRUCTELEM(m_skybox3d.origin),      -1,  SPROP_COORD),
	SendPropInt	(SENDINFO_STRUCTELEM(m_skybox3d.area),	8, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO_STRUCTELEM( m_skybox3d.fog.enable ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO_STRUCTELEM( m_skybox3d.fog.blend ), 1, SPROP_UNSIGNED ),
	SendPropVector( SENDINFO_STRUCTELEM(m_skybox3d.fog.dirPrimary), -1, SPROP_COORD),
	SendPropInt( SENDINFO_STRUCTELEM( m_skybox3d.fog.colorPrimary ), 32, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO_STRUCTELEM( m_skybox3d.fog.colorSecondary ), 32, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_skybox3d.fog.start ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_skybox3d.fog.end ), 0, SPROP_NOSCALE ),

	// fog data
	SendPropInt( SENDINFO_STRUCTELEM( m_fog.enable ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO_STRUCTELEM( m_fog.blend ), 1, SPROP_UNSIGNED ),
	SendPropVector( SENDINFO_STRUCTELEM(m_fog.dirPrimary), -1, SPROP_COORD),
	SendPropInt( SENDINFO_STRUCTELEM( m_fog.colorPrimary ), 32, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO_STRUCTELEM( m_fog.colorSecondary ), 32, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.start ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.end ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.farz ), 0, SPROP_NOSCALE ),
	// audio data
	SendPropVector( SENDINFO_STRUCTARRAYELEM( m_audio.localSound, 0 ), -1, SPROP_COORD),
	SendPropVector( SENDINFO_STRUCTARRAYELEM( m_audio.localSound, 1 ), -1, SPROP_COORD),
	SendPropVector( SENDINFO_STRUCTARRAYELEM( m_audio.localSound, 2 ), -1, SPROP_COORD),
	SendPropVector( SENDINFO_STRUCTARRAYELEM( m_audio.localSound, 3 ), -1, SPROP_COORD),
	SendPropVector( SENDINFO_STRUCTARRAYELEM( m_audio.localSound, 4 ), -1, SPROP_COORD),
	SendPropVector( SENDINFO_STRUCTARRAYELEM( m_audio.localSound, 5 ), -1, SPROP_COORD),
	SendPropVector( SENDINFO_STRUCTARRAYELEM( m_audio.localSound, 6 ), -1, SPROP_COORD),
	SendPropVector( SENDINFO_STRUCTARRAYELEM( m_audio.localSound, 7 ), -1, SPROP_COORD),
	SendPropInt( SENDINFO_STRUCTELEM( m_audio.soundscapeIndex ), 17, 0 ),
	SendPropInt( SENDINFO_STRUCTELEM( m_audio.localBits ), NUM_AUDIO_LOCAL_SOUNDS, SPROP_UNSIGNED ),
	SendPropEHandle( SENDINFO_STRUCTELEM( m_audio.ent ) ),
END_SEND_TABLE()

BEGIN_DATADESC_NO_BASE( fogparams_t )

	DEFINE_FIELD( enable, FIELD_BOOLEAN ),
	DEFINE_FIELD( blend, FIELD_BOOLEAN ),
	DEFINE_FIELD( dirPrimary, FIELD_VECTOR ),
	DEFINE_FIELD( colorPrimary, FIELD_COLOR32 ),
	DEFINE_FIELD( colorSecondary, FIELD_COLOR32 ),
	DEFINE_FIELD( start, FIELD_FLOAT ),
	DEFINE_FIELD( end, FIELD_FLOAT ),
	DEFINE_FIELD( farz, FIELD_FLOAT ),

END_DATADESC()

BEGIN_DATADESC_NO_BASE( sky3dparams_t )

	DEFINE_FIELD( scale, FIELD_INTEGER ),
	DEFINE_FIELD( origin, FIELD_VECTOR ),
	DEFINE_FIELD( area, FIELD_INTEGER ),
	DEFINE_EMBEDDED( fog ),

END_DATADESC()

BEGIN_DATADESC_NO_BASE( audioparams_t )

	DEFINE_AUTO_ARRAY( localSound, FIELD_VECTOR ),
	DEFINE_FIELD( soundscapeIndex, FIELD_INTEGER ),
	DEFINE_FIELD( localBits, FIELD_INTEGER ),
	DEFINE_FIELD( ent, FIELD_EHANDLE ),

END_DATADESC()

BEGIN_SIMPLE_DATADESC( CPlayerLocalData )
	DEFINE_AUTO_ARRAY( m_chAreaBits, FIELD_CHARACTER ),
	DEFINE_FIELD( m_iHideHUD, FIELD_INTEGER ),
	DEFINE_FIELD( m_iFOV, FIELD_INTEGER ),
	DEFINE_FIELD( m_flFOVRate, FIELD_FLOAT ),
	DEFINE_FIELD( m_iDefaultFOV, FIELD_INTEGER ),
	DEFINE_FIELD( m_vecOverViewpoint, FIELD_VECTOR ),
	DEFINE_FIELD( m_bDucked, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bDucking, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bInDuckJump, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flDucktime, FIELD_TIME ),
	DEFINE_FIELD( m_flDuckJumpTime, FIELD_TIME ),
	DEFINE_FIELD( m_flJumpTime, FIELD_TIME ),
	DEFINE_FIELD( m_nStepside, FIELD_INTEGER ),
	DEFINE_FIELD( m_flFallVelocity, FIELD_FLOAT ),
	DEFINE_FIELD( m_nOldButtons, FIELD_INTEGER ),
	DEFINE_FIELD( m_vecClientBaseVelocity, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecPunchAngle, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecPunchAngleVel, FIELD_VECTOR ),
	DEFINE_FIELD( m_bDrawViewmodel, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bWearingSuit, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bPoisoned, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flStepSize, FIELD_FLOAT ),
	DEFINE_FIELD( m_bAllowAutoMovement, FIELD_BOOLEAN ),
	DEFINE_EMBEDDED( m_skybox3d ),
	DEFINE_EMBEDDED( m_fog ),
	DEFINE_EMBEDDED( m_audio ),
	DEFINE_FIELD( m_bSlowMovement, FIELD_BOOLEAN ),
	
END_DATADESC()

BEGIN_PREDICTION_DATA_NO_BASE( CPlayerLocalData )
END_PREDICTION_DATA()

//-----------------------------------------------------------------------------
// Purpose: Constructor.
//-----------------------------------------------------------------------------
CPlayerLocalData::CPlayerLocalData()
{
#ifdef _DEBUG
	m_vecOverViewpoint.Init();
	m_vecClientBaseVelocity.Init();
	m_vecPunchAngle.Init();
#endif
	m_audio.soundscapeIndex = 0;
	m_audio.localBits = 0;
	m_audio.ent.Set( NULL );
}


void CPlayerLocalData::UpdateAreaBits( CBasePlayer *pl )
{
	Vector origin = pl->EyePosition();

	unsigned char tempBits[32];
	COMPILE_TIME_ASSERT( sizeof( tempBits ) >= sizeof( ((CPlayerLocalData*)0)->m_chAreaBits ) );

	int area = engine->GetArea( origin );
	engine->GetAreaBits( area, tempBits, sizeof( tempBits ) );
	for ( int i=0; i < m_chAreaBits.Count(); i++ )
	{
		if ( tempBits[i] != m_chAreaBits[ i ] )
		{
			m_chAreaBits.Set( i, tempBits[i] );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Fills in CClientData values for local player just before sending over wire
// Input  : player - 
//-----------------------------------------------------------------------------

void ClientData_Update( CBasePlayer *pl )
{
	// HACKHACK: for 3d skybox 
	// UNDONE: Support multiple sky cameras?
	CSkyCamera *pSkyCamera = GetCurrentSkyCamera();
	if ( pSkyCamera )
	{
		pl->m_Local.m_skybox3d = pSkyCamera->m_skyboxData;
	}
	else
	{
		pl->m_Local.m_skybox3d.area = 255;
	}

	GetWorldFogParams( pl->m_Local.m_fog );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void UpdateAllClientData( void )
{
	VPROF( "UpdateAllClientData" );
	int i;
	CBasePlayer *pl;

	for ( i = 1; i <= gpGlobals->maxClients; i++ )
	{
		pl = ( CBasePlayer * )UTIL_PlayerByIndex( i );
		if ( !pl )
			continue;

		ClientData_Update( pl );
	}
}

//-----------------------------------------------------------------------------
// Purpose: If the recipient is the same as objectID, go ahead and iterate down
//  the m_Local stuff, otherwise, act like it wasn't there at all.
// This way, only the local player receives information about him/herself.
// Input  : *pVarData - 
//			*pOut - 
//			objectID - 
//-----------------------------------------------------------------------------

void* SendProxy_SendLocalDataTable( const SendProp *pProp, const void *pStruct, const void *pVarData, CSendProxyRecipients *pRecipients, int objectID )
{
	pRecipients->SetOnly( objectID - 1 );
	return ( void * )pVarData;
}




