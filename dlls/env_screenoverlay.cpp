//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Entity to control screen overlays on a player
//
//=============================================================================//

#include "cbase.h"
#include "shareddefs.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CEnvScreenOverlay : public CPointEntity
{
	DECLARE_CLASS( CEnvScreenOverlay, CPointEntity );
public:
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	CEnvScreenOverlay();

	// Always transmit to clients
	virtual int UpdateTransmitState();

	void	InputStartOverlay( inputdata_t &inputdata );
	void	InputStopOverlay( inputdata_t &inputdata );
	void	InputSwitchOverlay( inputdata_t &inputdata );

	void	SetActive( bool bActive ) { m_bIsActive = bActive; }
	
protected:
	CNetworkArray( string_t, m_iszOverlayNames, MAX_SCREEN_OVERLAYS );
	CNetworkArray( float, m_flOverlayTimes, MAX_SCREEN_OVERLAYS );
	CNetworkVar( float, m_flStartTime );
	CNetworkVar( int, m_iDesiredOverlay );
	CNetworkVar( bool, m_bIsActive );
};

LINK_ENTITY_TO_CLASS( env_screenoverlay, CEnvScreenOverlay );

BEGIN_DATADESC( CEnvScreenOverlay )

// Silence, Classcheck!
//	DEFINE_ARRAY( m_iszOverlayNames, FIELD_STRING, MAX_SCREEN_OVERLAYS ),
//	DEFINE_ARRAY( m_flOverlayTimes, FIELD_FLOAT, MAX_SCREEN_OVERLAYS ),

	DEFINE_KEYFIELD( m_iszOverlayNames[0], FIELD_STRING, "OverlayName1" ),
	DEFINE_KEYFIELD( m_iszOverlayNames[1], FIELD_STRING, "OverlayName2" ),
	DEFINE_KEYFIELD( m_iszOverlayNames[2], FIELD_STRING, "OverlayName3" ),
	DEFINE_KEYFIELD( m_iszOverlayNames[3], FIELD_STRING, "OverlayName4" ),
	DEFINE_KEYFIELD( m_iszOverlayNames[4], FIELD_STRING, "OverlayName5" ),
	DEFINE_KEYFIELD( m_iszOverlayNames[5], FIELD_STRING, "OverlayName6" ),
	DEFINE_KEYFIELD( m_iszOverlayNames[6], FIELD_STRING, "OverlayName7" ),
	DEFINE_KEYFIELD( m_iszOverlayNames[7], FIELD_STRING, "OverlayName8" ),
	DEFINE_KEYFIELD( m_iszOverlayNames[8], FIELD_STRING, "OverlayName9" ),
	DEFINE_KEYFIELD( m_iszOverlayNames[9], FIELD_STRING, "OverlayName10" ),
	DEFINE_KEYFIELD( m_flOverlayTimes[0], FIELD_FLOAT, "OverlayTime1" ),
	DEFINE_KEYFIELD( m_flOverlayTimes[1], FIELD_FLOAT, "OverlayTime2" ),
	DEFINE_KEYFIELD( m_flOverlayTimes[2], FIELD_FLOAT, "OverlayTime3" ),
	DEFINE_KEYFIELD( m_flOverlayTimes[3], FIELD_FLOAT, "OverlayTime4" ),
	DEFINE_KEYFIELD( m_flOverlayTimes[4], FIELD_FLOAT, "OverlayTime5" ),
	DEFINE_KEYFIELD( m_flOverlayTimes[5], FIELD_FLOAT, "OverlayTime6" ),
	DEFINE_KEYFIELD( m_flOverlayTimes[6], FIELD_FLOAT, "OverlayTime7" ),
	DEFINE_KEYFIELD( m_flOverlayTimes[7], FIELD_FLOAT, "OverlayTime8" ),
	DEFINE_KEYFIELD( m_flOverlayTimes[8], FIELD_FLOAT, "OverlayTime9" ),
	DEFINE_KEYFIELD( m_flOverlayTimes[9], FIELD_FLOAT, "OverlayTime10" ),
	
	// Class CEnvScreenOverlay:
	DEFINE_FIELD( m_iDesiredOverlay, FIELD_INTEGER ),
	DEFINE_FIELD( m_flStartTime, FIELD_TIME ),
	DEFINE_FIELD( m_bIsActive, FIELD_BOOLEAN ),

	DEFINE_INPUTFUNC( FIELD_VOID, "StartOverlays", InputStartOverlay ),
	DEFINE_INPUTFUNC( FIELD_VOID, "StopOverlays", InputStopOverlay ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SwitchOverlay", InputSwitchOverlay ),

END_DATADESC()

void SendProxy_String_tToString( const SendProp *pProp, const void *pStruct, const void *pData, DVariant *pOut, int iElement, int objectID )
{
	string_t *pString = (string_t*)pData;
	pOut->m_pString = (char*)STRING( *pString );
}

IMPLEMENT_SERVERCLASS_ST( CEnvScreenOverlay, DT_EnvScreenOverlay )
	SendPropArray( SendPropString( SENDINFO_ARRAY( m_iszOverlayNames ), 0, SendProxy_String_tToString ), m_iszOverlayNames ),
	SendPropArray( SendPropFloat( SENDINFO_ARRAY( m_flOverlayTimes ), 11, SPROP_ROUNDDOWN, -1.0f, 63.0f ), m_flOverlayTimes ),
	SendPropFloat( SENDINFO(m_flStartTime), 10 ),
	SendPropInt( SENDINFO( m_iDesiredOverlay ), 5 ),
	SendPropBool( SENDINFO( m_bIsActive ) ),
END_SEND_TABLE()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CEnvScreenOverlay::CEnvScreenOverlay( void )
{
	m_flStartTime = 0;
	m_iDesiredOverlay = 0;
	m_bIsActive = false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputdata - 
//-----------------------------------------------------------------------------
void CEnvScreenOverlay::InputStartOverlay( inputdata_t &inputdata )
{
	if ( m_iszOverlayNames[0] == NULL_STRING )
	{
		Warning("env_screenoverlay %s has no overlays to display.\n", STRING(GetEntityName()) );
		return;
	}

	m_flStartTime = gpGlobals->curtime;
	m_bIsActive = true;

	// Turn off any other screen overlays out there
	CBaseEntity *pEnt = NULL;
	while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "env_screenoverlay" )) != NULL )
	{
		if ( pEnt != this )
		{
			CEnvScreenOverlay *pOverlay = assert_cast<CEnvScreenOverlay*>(pEnt);
			pOverlay->SetActive( false );
		}
	}
}

int CEnvScreenOverlay::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}


void CEnvScreenOverlay::InputSwitchOverlay( inputdata_t &inputdata )
{
	int iNewOverlay = inputdata.value.Int() - 1;
	iNewOverlay = abs( iNewOverlay );

	if ( m_iszOverlayNames[iNewOverlay] == NULL_STRING )
	{
		Warning("env_screenoverlay %s has no overlays to display.\n", STRING(GetEntityName()) );
		return;
	}

	m_iDesiredOverlay = iNewOverlay;
	m_flStartTime = gpGlobals->curtime;
}

void CEnvScreenOverlay::InputStopOverlay( inputdata_t &inputdata )
{
	if ( m_iszOverlayNames[0] == NULL_STRING )
	{
		Warning("env_screenoverlay %s has no overlays to display.\n", STRING(GetEntityName()) );
		return;
	}

	m_flStartTime = -1;
	m_bIsActive = false; 
}
