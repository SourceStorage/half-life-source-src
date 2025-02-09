//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Base class for all animating characters and objects.
//
//=============================================================================//

#include "cbase.h"
#include "baseanimating.h"
#include "animation.h"
#include "activitylist.h"
#include "studio.h"
#include "bone_setup.h"
#include "mathlib.h"
#include "model_types.h"
#include "engine/IVEngineCache.h"
#include "physics.h"
#include "ndebugoverlay.h"
#include "vstdlib/strtools.h"
#include "npcevent.h"
#include "isaverestore.h"
#include "KeyValues.h"
#include "tier0/vprof.h"
#include "EntityFlame.h"
#include "EntityDissolve.h"
#include "ai_basenpc.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar ai_sequence_debug( "ai_sequence_debug", "0" );

class CIKSaveRestoreOps : public CClassPtrSaveRestoreOps
{
	// save data type interface
	void Save( const SaveRestoreFieldInfo_t &fieldInfo, ISave *pSave )
	{
		Assert( fieldInfo.pTypeDesc->fieldSize == 1 );
		CIKContext **pIK = (CIKContext **)fieldInfo.pField;
		bool bHasIK = (*pIK) != 0;
		pSave->WriteBool( &bHasIK );
	}

	void Restore( const SaveRestoreFieldInfo_t &fieldInfo, IRestore *pRestore )
	{
		Assert( fieldInfo.pTypeDesc->fieldSize == 1 );
		CIKContext **pIK = (CIKContext **)fieldInfo.pField;

		bool bHasIK;
		pRestore->ReadBool( &bHasIK );
		*pIK = (bHasIK) ? new CIKContext : NULL;
	}
};


//-----------------------------------------------------------------------------
// Relative lighting entity
//-----------------------------------------------------------------------------
class CInfoLightingRelative : public CBaseEntity
{
public:
	DECLARE_CLASS( CInfoLightingRelative, CBaseEntity );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	virtual void Activate();
	virtual void SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways );

private:
	CNetworkHandle( CBaseEntity, m_hLightingLandmark );
	string_t		m_strLightingLandmark;
};

LINK_ENTITY_TO_CLASS( info_lighting_relative, CInfoLightingRelative );

BEGIN_DATADESC( CInfoLightingRelative )
	DEFINE_KEYFIELD( m_strLightingLandmark, FIELD_STRING, "LightingLandmark" ),
	DEFINE_FIELD( m_hLightingLandmark, FIELD_EHANDLE ),
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST(CInfoLightingRelative, DT_InfoLightingRelative)
	SendPropEHandle( SENDINFO( m_hLightingLandmark ) ),
END_SEND_TABLE()


//-----------------------------------------------------------------------------
// Activate!
//-----------------------------------------------------------------------------
void CInfoLightingRelative::Activate()
{
	BaseClass::Activate();
	if ( m_strLightingLandmark == NULL_STRING )
	{
		m_hLightingLandmark = NULL;
	}
	else
	{
		m_hLightingLandmark = gEntList.FindEntityByName( NULL, m_strLightingLandmark, NULL );
		if ( !m_hLightingLandmark )
		{
			DevWarning( "%s: Could not find lighting landmark '%s'!\n", GetClassname(), STRING( m_strLightingLandmark ) );
		}
	}
}


//-----------------------------------------------------------------------------
// Force our lighting landmark to be trasmitted
//-----------------------------------------------------------------------------
void CInfoLightingRelative::SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways )
{
	// Are we already marked for transmission?
	if ( pInfo->m_pTransmitEdict->Get( entindex() ) )
		return;

	BaseClass::SetTransmit( pInfo, bAlways );
	
	// Force our constraint entity to be sent too.
	if ( m_hLightingLandmark )
	{
		m_hLightingLandmark->SetTransmit( pInfo, bAlways );
	}
}


static CIKSaveRestoreOps s_IKSaveRestoreOp;


BEGIN_DATADESC( CBaseAnimating )

	DEFINE_FIELD( m_flGroundSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( m_flLastEventCheck, FIELD_TIME ),
	DEFINE_FIELD( m_bSequenceFinished, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bSequenceLoops, FIELD_BOOLEAN ),

//	DEFINE_FIELD( m_nForceBone, FIELD_INTEGER ),
//	DEFINE_FIELD( m_vecForce, FIELD_VECTOR ),

	DEFINE_INPUT( m_nSkin, FIELD_INTEGER, "skin" ),
	DEFINE_KEYFIELD( m_nBody, FIELD_INTEGER, "body" ),
	DEFINE_INPUT( m_nBody, FIELD_INTEGER, "SetBodyGroup" ),
	DEFINE_KEYFIELD( m_nHitboxSet, FIELD_INTEGER, "hitboxset" ),
	DEFINE_KEYFIELD( m_nSequence, FIELD_INTEGER, "sequence" ),
	DEFINE_ARRAY( m_flPoseParameter, FIELD_FLOAT, CBaseAnimating::NUM_POSEPAREMETERS ),
	DEFINE_ARRAY( m_flEncodedController,	FIELD_FLOAT, CBaseAnimating::NUM_BONECTRLS ),
	DEFINE_KEYFIELD( m_flPlaybackRate, FIELD_FLOAT, "playbackrate" ),
	DEFINE_KEYFIELD( m_flCycle, FIELD_FLOAT, "cycle" ),
//	DEFINE_FIELD( m_flIKGroundContactTime, FIELD_TIME ),
//	DEFINE_FIELD( m_flIKGroundMinHeight, FIELD_FLOAT ),
//	DEFINE_FIELD( m_flIKGroundMaxHeight, FIELD_FLOAT ),
//	DEFINE_FIELD( m_flEstIkFloor, FIELD_FLOAT ),
//	DEFINE_FIELD( m_flEstIkOffset, FIELD_FLOAT ),
	DEFINE_CUSTOM_FIELD( m_pIk, &s_IKSaveRestoreOp ),
	DEFINE_FIELD( m_iIKCounter, FIELD_INTEGER ),
	DEFINE_FIELD( m_bClientSideAnimation, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bClientSideFrameReset, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_nNewSequenceParity, FIELD_INTEGER ),
	DEFINE_FIELD( m_nResetEventsParity, FIELD_INTEGER ),
	DEFINE_FIELD( m_nMuzzleFlashParity, FIELD_CHARACTER ),

	DEFINE_KEYFIELD( m_iszLightingOrigin, FIELD_STRING, "LightingOriginHack" ),
	DEFINE_FIELD( m_hLightingOrigin, FIELD_EHANDLE ),

	DEFINE_FIELD( m_flModelWidthScale, FIELD_FLOAT ),
	DEFINE_FIELD( m_flDissolveStartTime, FIELD_TIME ),

 // DEFINE_FIELD( m_boneCacheHandle, memhandle_t ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Ignite", InputIgnite ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetLightingOriginHack", InputSetLightingOriginHack ),
	DEFINE_OUTPUT( m_OnIgnite, "OnIgnite" ),
END_DATADESC()

// Sendtable for fields we don't want to send to clientside animating entities
BEGIN_SEND_TABLE_NOBASE( CBaseAnimating, DT_ServerAnimationData )
	// ANIMATION_CYCLE_BITS is defined in shareddefs.h
	SendPropFloat	(SENDINFO(m_flCycle),		ANIMATION_CYCLE_BITS, SPROP_ROUNDDOWN,	0.0f,   1.0f)
END_SEND_TABLE()

void *SendProxy_ClientSideAnimation( const SendProp *pProp, const void *pStruct, const void *pVarData, CSendProxyRecipients *pRecipients, int objectID );

// SendTable stuff.
IMPLEMENT_SERVERCLASS_ST(CBaseAnimating, DT_BaseAnimating)
	SendPropInt		( SENDINFO(m_nForceBone), 8, 0 ),
	SendPropVector	( SENDINFO(m_vecForce), -1, SPROP_NOSCALE ),

	SendPropInt		( SENDINFO(m_nSkin), ANIMATION_SKIN_BITS),
	SendPropInt		( SENDINFO(m_nBody), ANIMATION_BODY_BITS),

	SendPropInt		( SENDINFO(m_nHitboxSet),ANIMATION_HITBOXSET_BITS, SPROP_UNSIGNED ),

	SendPropFloat	( SENDINFO(m_flModelWidthScale), 6, SPROP_ROUNDUP, 0.0f, 1.0f ),

	SendPropArray3  ( SENDINFO_ARRAY3(m_flPoseParameter), SendPropFloat(SENDINFO_ARRAY(m_flPoseParameter), ANIMATION_POSEPARAMETER_BITS, 0, 0.0f, 1.0f ) ),
	
	SendPropInt		( SENDINFO(m_nSequence),	ANIMATION_SEQUENCE_BITS, 0),
	SendPropFloat	( SENDINFO(m_flPlaybackRate),	ANIMATION_PLAYBACKRATE_BITS,	SPROP_ROUNDUP,	-4.0,	12.0f), // NOTE: if this isn't a power of 2 than "1.0" can't be encoded correctly

	SendPropArray3 	(SENDINFO_ARRAY3(m_flEncodedController), SendPropFloat(SENDINFO_ARRAY(m_flEncodedController), 11, SPROP_ROUNDDOWN, 0.0f, 1.0f ) ),

	SendPropInt( SENDINFO( m_bClientSideAnimation ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_bClientSideFrameReset ), 1, SPROP_UNSIGNED ),

	SendPropInt( SENDINFO( m_nNewSequenceParity ), EF_PARITY_BITS, SPROP_UNSIGNED|SPROP_CHANGES_OFTEN ),
	SendPropInt( SENDINFO( m_nResetEventsParity ), EF_PARITY_BITS, SPROP_UNSIGNED|SPROP_CHANGES_OFTEN ),
	SendPropInt( SENDINFO( m_nMuzzleFlashParity ), EF_MUZZLEFLASH_BITS, SPROP_UNSIGNED|SPROP_CHANGES_OFTEN ),

	SendPropEHandle( SENDINFO( m_hLightingOrigin ) ),

	SendPropDataTable( "serveranimdata", 0, &REFERENCE_SEND_TABLE( DT_ServerAnimationData ), SendProxy_ClientSideAnimation ),
END_SEND_TABLE()


CBaseAnimating::CBaseAnimating()
{
#ifdef _DEBUG
	m_vecForce.GetForModify().Init();
#endif

	m_bClientSideAnimation = false;
	m_pIk = NULL;
	m_iIKCounter = 0;

	m_flIKGroundContactTime = 0;
	m_flIKGroundMinHeight = 0;
	m_flIKGroundMaxHeight = 0;
	m_flEstIkFloor = GetAbsOrigin().z;
	m_flEstIkOffset = 0;

	m_flModelWidthScale = 1.0f;
	// initialize anim clock
	m_flAnimTime = gpGlobals->curtime;
	m_flPrevAnimTime = gpGlobals->curtime;
	m_nNewSequenceParity = 0;
	m_nResetEventsParity = 0;
	m_boneCacheHandle = 0;
}

CBaseAnimating::~CBaseAnimating()
{
	Studio_DestroyBoneCache( m_boneCacheHandle );
	delete m_pIk;
}


//-----------------------------------------------------------------------------
// Activate!
//-----------------------------------------------------------------------------
void CBaseAnimating::Activate()
{
	BaseClass::Activate();
	SetLightingOrigin( m_iszLightingOrigin );
}


//-----------------------------------------------------------------------------
// Force our lighting origin to be trasmitted
//-----------------------------------------------------------------------------
void CBaseAnimating::SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways )
{
	// Are we already marked for transmission?
	if ( pInfo->m_pTransmitEdict->Get( entindex() ) )
		return;

	BaseClass::SetTransmit( pInfo, bAlways );
	
	// Force our constraint entity to be sent too.
	if ( m_hLightingOrigin )
	{
		m_hLightingOrigin->SetTransmit( pInfo, bAlways );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseAnimating::OnRestore()
{
	BaseClass::OnRestore();

	if ( m_nSequence != -1 && GetModelPtr() && !IsValidSequence( m_nSequence ) )
		m_nSequence = 0;

	m_flEstIkFloor = GetLocalOrigin().z;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseAnimating::UseClientSideAnimation()
{
	m_bClientSideAnimation = true;
}

#define MAX_ANIMTIME_INTERVAL 0.2f

//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float CBaseAnimating::GetAnimTimeInterval( void ) const
{
	float flInterval;
	if (m_flAnimTime < gpGlobals->curtime)
	{
		// estimate what it'll be this frame
		flInterval = clamp( gpGlobals->curtime - m_flAnimTime, 0, MAX_ANIMTIME_INTERVAL );
	}
	else
	{
		// report actual
		flInterval = clamp( m_flAnimTime - m_flPrevAnimTime, 0, MAX_ANIMTIME_INTERVAL );
	}
	return flInterval;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseAnimating::StudioFrameAdvanceInternal( float flCycleDelta )
{
	float flNewCycle = GetCycle() + flCycleDelta;
	if (flNewCycle < 0.0 || flNewCycle >= 1.0) 
	{
		if (m_bSequenceLoops)
		{
			flNewCycle -= (int)(flNewCycle);
		}
		else
		{
			flNewCycle = (flNewCycle < 0.0f) ? 0.0f : 0.999f;
		}
		m_bSequenceFinished = true;	// just in case it wasn't caught in GetEvents
	}
	else if (flNewCycle > GetLastVisibleCycle( GetSequence() ))
	{
		m_bSequenceFinished = true;
	}

	SetCycle( flNewCycle );

	/*
	if (!IsPlayer())
		Msg("%s %6.3f : %6.3f %6.3f (%.3f) %.3f\n", 
			GetClassname(), gpGlobals->curtime, 
			m_flAnimTime.Get(), m_flPrevAnimTime, flInterval, GetCycle() );
	*/
 
	m_flGroundSpeed = GetSequenceGroundSpeed( GetSequence() );

	// Msg("%s : %s : %5.1f\n", GetClassname(), GetSequenceName( GetSequence() ), GetCycle() );
	InvalidatePhysicsRecursive( ANIMATION_CHANGED );

	InvalidateBoneCacheIfOlderThan( 0 );
}

void CBaseAnimating::InvalidateBoneCacheIfOlderThan( float deltaTime )
{
	CBoneCache *pcache = Studio_GetBoneCache( m_boneCacheHandle );
	if ( !pcache || !pcache->IsValid( gpGlobals->curtime, deltaTime ) )
	{
		InvalidateBoneCache();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseAnimating::StudioFrameAdvanceManual( float flInterval )
{
	UpdateModelWidthScale();
	m_flAnimTime = gpGlobals->curtime;
	m_flPrevAnimTime = m_flAnimTime - flInterval;
	StudioFrameAdvanceInternal( flInterval );
}


//=========================================================
// StudioFrameAdvance - advance the animation frame up some interval (default 0.1) into the future
//=========================================================
void CBaseAnimating::StudioFrameAdvance()
{
	UpdateModelWidthScale();

	if ( !m_flPrevAnimTime )
	{
		m_flPrevAnimTime = m_flAnimTime;
	}

	// Time since last animation
	float flInterval = gpGlobals->curtime - m_flAnimTime;
	flInterval = clamp( flInterval, 0, MAX_ANIMTIME_INTERVAL );

	//Msg( "%i %s interval %f\n", entindex(), GetClassname(), flInterval );
	if (flInterval <= 0.001)
	{
		// Msg("%s : %s : %5.3f (skip)\n", GetClassname(), GetSequenceName( GetSequence() ), GetCycle() );
		return;
	}

	// Latch prev
	m_flPrevAnimTime = m_flAnimTime;
	// Set current
	m_flAnimTime = gpGlobals->curtime;

	// Drive cycle
	float flCycleRate = GetSequenceCycleRate( GetSequence() ) * m_flPlaybackRate;

	StudioFrameAdvanceInternal( flInterval * flCycleRate );

	if (ai_sequence_debug.GetBool() == true && m_debugOverlays & OVERLAY_NPC_SELECTED_BIT)
	{
		Msg("%5.2f : %s : %s : %5.3f\n", gpGlobals->curtime, GetClassname(), GetSequenceName( GetSequence() ), GetCycle() );
	}
}


//-----------------------------------------------------------------------------
// Set the lighting origin
//-----------------------------------------------------------------------------
void CBaseAnimating::SetLightingOrigin( string_t strLightingOrigin )
{
	if ( strLightingOrigin == NULL_STRING )
	{
		SetLightingOrigin( NULL );
	}
	else
	{
		CBaseEntity *pLightingOrigin = gEntList.FindEntityByName( NULL, strLightingOrigin, NULL );
		if ( !pLightingOrigin )
		{
			DevWarning( "%s: Could not find info_lighting_relative '%s'!\n", GetClassname(), STRING( strLightingOrigin ) );
		}
		else if ( !dynamic_cast<CInfoLightingRelative *>(pLightingOrigin) )
		{
			if( !pLightingOrigin )
			{
				DevWarning( "%s: Cannot find Lighting Origin named: %s\n", GetEntityName(), strLightingOrigin );
			}
			else
			{
				DevWarning( "%s: Specified entity '%s' must be a info_lighting_relative!\n", 
					pLightingOrigin->GetClassname(), pLightingOrigin->GetEntityName() );
			}
			return;
		}

		SetLightingOrigin( pLightingOrigin );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputdata - 
//-----------------------------------------------------------------------------
void CBaseAnimating::InputSetLightingOriginHack( inputdata_t &inputdata )
{ 
	// Find our specified target
	string_t strLightingOrigin = MAKE_STRING( inputdata.value.String() );
	SetLightingOrigin( strLightingOrigin );
}



//=========================================================
// SelectWeightedSequence
//=========================================================
int CBaseAnimating::SelectWeightedSequence ( Activity activity )
{
	Assert( activity != ACT_INVALID );
	Assert( GetModelPtr() );
	return ::SelectWeightedSequence( GetModelPtr(), activity, GetSequence() );
}


int CBaseAnimating::SelectWeightedSequence ( Activity activity, int curSequence )
{
	Assert( activity != ACT_INVALID );
	Assert( GetModelPtr() );
	return ::SelectWeightedSequence( GetModelPtr(), activity, curSequence );
}

//=========================================================
// ResetActivityIndexes
//=========================================================
void CBaseAnimating::ResetActivityIndexes ( void )
{
	Assert( GetModelPtr() );
	::ResetActivityIndexes( GetModelPtr() );
}

//=========================================================
// ResetEventIndexes
//=========================================================
void CBaseAnimating::ResetEventIndexes ( void )
{
	Assert( GetModelPtr() );
	::ResetEventIndexes( GetModelPtr() );
}

//=========================================================
// LookupHeaviestSequence
//
// Get sequence with highest 'weight' for this activity
//
//=========================================================
int CBaseAnimating::SelectHeaviestSequence ( Activity activity )
{
	Assert( GetModelPtr() );
	return ::SelectHeaviestSequence( GetModelPtr(), activity );
}


//-----------------------------------------------------------------------------
// Purpose: Looks up an activity by name.
// Input  : label - Name of the activity, ie "ACT_IDLE".
// Output : Returns the activity ID or ACT_INVALID.
//-----------------------------------------------------------------------------
int CBaseAnimating::LookupActivity( const char *label )
{
	Assert( GetModelPtr() );
	return ::LookupActivity( GetModelPtr(), label );
}

//=========================================================
//=========================================================
int CBaseAnimating::LookupSequence( const char *label )
{
	Assert( GetModelPtr() );
	return ::LookupSequence( GetModelPtr(), label );
}



//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
KeyValues *CBaseAnimating::GetSequenceKeyValues( int iSequence )
{
	const char *szText = Studio_GetKeyValueText( GetModelPtr(), iSequence );

	if (szText)
	{
		KeyValues *seqKeyValues = new KeyValues("");
		if ( seqKeyValues->LoadFromBuffer( modelinfo->GetModelName( GetModel() ), szText ) )
		{
			return seqKeyValues;
		}
		seqKeyValues->deleteThis();
	}
	return NULL;
}



//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : float - 
//-----------------------------------------------------------------------------
float CBaseAnimating::GetSequenceMoveYaw( int iSequence )
{
	Vector				vecReturn;
	
	Assert( GetModelPtr() );
	::GetSequenceLinearMotion( GetModelPtr(), iSequence, GetPoseParameterArray(), &vecReturn );

	if (vecReturn.Length() > 0)
	{
		return UTIL_VecToYaw( vecReturn );
	}

	return NOMOTION;
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : float
//-----------------------------------------------------------------------------
float CBaseAnimating::GetSequenceMoveDist( int iSequence )
{
	Vector				vecReturn;
	
	Assert( GetModelPtr() );
	::GetSequenceLinearMotion( GetModelPtr(), iSequence, GetPoseParameterArray(), &vecReturn );

	return vecReturn.Length();
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//			*pVec - 
//
//-----------------------------------------------------------------------------
void CBaseAnimating::GetSequenceLinearMotion( int iSequence, Vector *pVec )
{
	Assert( GetModelPtr() );
	::GetSequenceLinearMotion( GetModelPtr(), iSequence, GetPoseParameterArray(), pVec );
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : char
//-----------------------------------------------------------------------------
const char *CBaseAnimating::GetSequenceName( int iSequence )
{
	if( iSequence == -1 )
	{
		return "Not Found!";
	}

	if ( !GetModelPtr() )
		return "No model!";

	return ::GetSequenceName( GetModelPtr(), iSequence );
}
//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : char
//-----------------------------------------------------------------------------
const char *CBaseAnimating::GetSequenceActivityName( int iSequence )
{
	if( iSequence == -1 )
	{
		return "Not Found!";
	}

	if ( !GetModelPtr() )
		return "No model!";

	return ::GetSequenceActivityName( GetModelPtr(), iSequence );
}


//-----------------------------------------------------------------------------
// Purpose: Make this a client-side simulated entity
// Input  : force - vector of force to be exerted in the physics simulation
//			forceBone - bone to exert force upon
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseAnimating::BecomeRagdollOnClient( const Vector &force )
{
	// If this character has a ragdoll animation, turn it over to the physics system
	if ( CanBecomeRagdoll() ) 
	{
		VPhysicsDestroyObject();
		AddSolidFlags( FSOLID_NOT_SOLID );
		m_nRenderFX = kRenderFxRagdoll;
		
		// Have to do this dance because m_vecForce is a network vector
		// and can't be sent to ClampRagdollForce as a Vector *
		Vector vecClampedForce;
		ClampRagdollForce( force, &vecClampedForce );
		m_vecForce = vecClampedForce;

		SetParent( NULL );

		AddFlag( FL_TRANSRAGDOLL );

		SetMoveType( MOVETYPE_NONE );
		//UTIL_SetSize( this, vec3_origin, vec3_origin );
		SetThink( NULL );
	
		SetNextThink( gpGlobals->curtime + 2.0f );
		//If we're here, then we can vanish safely
		SetThink( &CBaseEntity::SUB_Remove );

		return true;
	}
	return false;
}

bool CBaseAnimating::IsRagdoll()
{
	return ( m_nRenderFX == kRenderFxRagdoll ) ? true : false;
}

bool CBaseAnimating::CanBecomeRagdoll( void ) 
{
	int ragdollSequence = SelectWeightedSequence( ACT_DIERAGDOLL );

	//Can't cause we don't have a ragdoll sequence.
	if ( ragdollSequence == ACTIVITY_NOT_AVAILABLE )
		 return false;
	
	if ( GetFlags() & FL_TRANSRAGDOLL )
		 return false;

	return true;
}

//=========================================================
//=========================================================
void CBaseAnimating::ResetSequenceInfo ( )
{
	if (GetSequence() == -1)
	{
		// This shouldn't happen.  Setting m_nSequence blindly is a horrible coding practice.
		SetSequence( 0 );
	}

	m_flGroundSpeed = GetSequenceGroundSpeed( GetSequence() );
	m_bSequenceLoops = ((GetSequenceFlags( GetSequence() ) & STUDIO_LOOPING) != 0);
	// m_flAnimTime = gpGlobals->time;
	m_flPlaybackRate = 1.0;
	m_bSequenceFinished = false;
	m_flLastEventCheck = 0;

	m_nNewSequenceParity = ( m_nNewSequenceParity+1 ) & EF_PARITY_MASK;
	m_nResetEventsParity = ( m_nResetEventsParity+1 ) & EF_PARITY_MASK;

	// FIXME: why is this called here?  Nothing should have changed to make this nessesary
	SetEventIndexForSequence( GetModelPtr()->pSeqdesc( GetSequence() ) );
}

//=========================================================
//=========================================================
bool CBaseAnimating::IsValidSequence( int iSequence )
{
	Assert( GetModelPtr() );
	studiohdr_t* pstudiohdr = GetModelPtr( );
	if (iSequence < 0 || iSequence >= pstudiohdr->GetNumSeq())
	{
		return false;
	}
	return true;
}


//=========================================================
//=========================================================
int CBaseAnimating::GetSequenceFlags( int iSequence )
{
	Assert( GetModelPtr() );
	return ::GetSequenceFlags( GetModelPtr(), iSequence );
}

//=========================================================
//=========================================================
float CBaseAnimating::SequenceDuration( int iSequence )
{
//	Assert( iSequence != -1 );
	studiohdr_t* pstudiohdr = GetModelPtr( );
	if ( !pstudiohdr )
	{
		DevWarning( 2, "CBaseAnimating::SequenceDuration( %d ) NULL pstudiohdr on %s!\n", iSequence, GetClassname() );
		return 0.1;
	}
	if (iSequence >= pstudiohdr->GetNumSeq() || iSequence < 0 )
	{
		DevWarning( 2, "CBaseAnimating::SequenceDuration( %d ) out of range\n", iSequence );
		return 0.1;
	}

	return Studio_Duration( pstudiohdr, iSequence, GetPoseParameterArray() );
}

float CBaseAnimating::GetSequenceCycleRate( int iSequence )
{
	float t = SequenceDuration( iSequence );

	if (t > 0.0f)
	{
		return 1.0f / t;
	}
	else
	{
		return 1.0f / 0.1f;
	}
}


float CBaseAnimating::GetLastVisibleCycle( int iSequence )
{
	studiohdr_t* pstudiohdr = GetModelPtr( );
	if ( !pstudiohdr )
	{
		DevWarning( 2, "CBaseAnimating::LastVisibleCycle( %d ) NULL pstudiohdr on %s!\n", iSequence, GetClassname() );
		return 1.0;
	}

	if (!(GetSequenceFlags( iSequence ) & STUDIO_LOOPING))
	{
		return 1.0f - (pstudiohdr->pSeqdesc( iSequence ).fadeouttime) * GetSequenceCycleRate( iSequence ) * m_flPlaybackRate;
	}
	else
	{
		return 1.0;
	}
}


float CBaseAnimating::GetSequenceGroundSpeed( int iSequence )
{
	float t = SequenceDuration( iSequence );

	if (t > 0)
	{
		return GetSequenceMoveDist( iSequence ) / t;
	}
	else
	{
		return 0;
	}
}

float CBaseAnimating::GetIdealSpeed( ) const
{
	return m_flGroundSpeed;
}

float CBaseAnimating::GetIdealAccel( ) const
{
	// return ideal max velocity change over 1 second.
	// tuned for run-walk range of humans
	return GetIdealSpeed() + 50;
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if the given sequence has the anim event, false if not.
// Input  : nSequence - sequence number to check
//			nEvent - anim event number to look for
//-----------------------------------------------------------------------------
bool CBaseAnimating::HasAnimEvent( int nSequence, int nEvent )
{
	studiohdr_t *pstudiohdr = GetModelPtr();
	if ( !pstudiohdr )
	{
		return false;
	}

  	animevent_t event;

	int index = 0;
	while ( ( index = GetAnimationEvent( pstudiohdr, nSequence, &event, 0.0f, 1.0f, index ) ) != 0 )
	{
		if ( event.event == nEvent )
		{
			return true;
		}
	}

	return false;
}


//=========================================================
// DispatchAnimEvents
//=========================================================
void CBaseAnimating::DispatchAnimEvents ( CBaseAnimating *eventHandler )
{
  	animevent_t	event;

	studiohdr_t *pstudiohdr = GetModelPtr( );

	if ( !pstudiohdr )
	{
		Assert(!"CBaseAnimating::DispatchAnimEvents: model missing");
		return;
	}

	// don't fire events if the framerate is 0, and skip this altogether if there are no events
	if (m_flPlaybackRate == 0.0 || pstudiohdr->pSeqdesc( GetSequence() ).numevents == 0)
	{
		return;
	}

	// look from when it last checked to some short time in the future	
	float flCycleRate = GetSequenceCycleRate( GetSequence() ) * m_flPlaybackRate;
	float flStart = m_flLastEventCheck;
	float flEnd = GetCycle();

	if (!m_bSequenceLoops && m_bSequenceFinished)
	{
		flEnd = 1.01f;
	}
	m_flLastEventCheck = flEnd;

	/*
	if (m_debugOverlays & OVERLAY_NPC_SELECTED_BIT)
	{
		Msg( "%s:%s : checking %.2f %.2f (%d)\n", STRING(GetModelName()), pstudiohdr->pSeqdesc( GetSequence() ).pszLabel(), flStart, flEnd, m_bSequenceFinished );
	}
	*/

	// FIXME: does not handle negative framerates!
	int index = 0;
	while ( (index = GetAnimationEvent( pstudiohdr, GetSequence(), &event, flStart, flEnd, index ) ) != 0 )
	{
		event.pSource = this;
		// calc when this event should happen
		if (flCycleRate > 0.0)
		{
			float flCycle = event.cycle;
			if (flCycle > GetCycle())
			{
				flCycle = flCycle - 1.0;
			}
			event.eventtime = m_flAnimTime + (flCycle - GetCycle()) / flCycleRate + GetAnimTimeInterval();
		}

		/*
		if (m_debugOverlays & OVERLAY_NPC_SELECTED_BIT)
		{
			Msg( "dispatch %i (%i) cycle %f event cycle %f cyclerate %f\n", 
				(int)(index - 1), 
				(int)event.event, 
				(float)GetCycle(), 
				(float)event.cycle, 
				(float)flCycleRate );
		}
		*/
		eventHandler->HandleAnimEvent( &event );
	}
}


// SetPoseParamater()

//=========================================================
//=========================================================
float CBaseAnimating::SetPoseParameter( const char *szName, float flValue )
{
	return SetPoseParameter( LookupPoseParameter( szName ), flValue );
}

float CBaseAnimating::SetPoseParameter( int iParameter, float flValue )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );

	if ( !pstudiohdr )
	{
		return flValue;
	}

	if (iParameter >= 0)
	{
		float flNewValue;
		flValue = Studio_SetPoseParameter( pstudiohdr, iParameter, flValue, flNewValue );
		m_flPoseParameter.Set( iParameter, flNewValue );
	}

	return flValue;
}

//=========================================================
//=========================================================
float CBaseAnimating::GetPoseParameter( const char *szName )
{
	return GetPoseParameter( LookupPoseParameter( szName ) );
}

float CBaseAnimating::GetPoseParameter( int iParameter )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );

	if ( !pstudiohdr )
	{
		Assert(!"CBaseAnimating::GetPoseParameter: model missing");
		return 0.0;
	}

	if (iParameter >= 0)
	{
		return Studio_GetPoseParameter( pstudiohdr, iParameter, m_flPoseParameter[ iParameter ] );
	}

	return 0.0;
}

//=========================================================
//=========================================================
int CBaseAnimating::LookupPoseParameter( const char *szName )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );

	if ( !pstudiohdr )
	{
		return 0;
	}

	for (int i = 0; i < pstudiohdr->GetNumPoseParameters(); i++)
	{
		if (stricmp( pstudiohdr->pPoseParameter( i ).pszName(), szName ) == 0)
		{
			return i;
		}
	}

	// AssertMsg( 0, UTIL_VarArgs( "poseparameter %s couldn't be mapped!!!\n", szName ) );
	return -1; // Error
}

//=========================================================
//=========================================================
bool CBaseAnimating::HasPoseParameter( int iSequence, const char *szName )
{
	int iParameter = LookupPoseParameter( szName );
	if (iParameter == -1)
	{
		return false;
	}

	return HasPoseParameter( iSequence, iParameter );
}

//=========================================================
//=========================================================
bool CBaseAnimating::HasPoseParameter( int iSequence, int iParameter )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );

	if ( !pstudiohdr )
	{
		return false;
	}

	if (iSequence < 0 || iSequence >= pstudiohdr->GetNumSeq())
	{
		return false;
	}

	if (pstudiohdr->GetSharedPoseParameter( iSequence, 0 ) == iParameter || pstudiohdr->GetSharedPoseParameter( iSequence, 0 ) == iParameter)
	{
		return true;
	}
	return false;
}


//=========================================================
// Purpose: from input of 75% to 200% of maximum range, rescale smoothly from 75% to 100%
//=========================================================
float CBaseAnimating::EdgeLimitPoseParameter( int iParameter, float flValue, float flBase )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if ( !pstudiohdr )
	{
		return flValue;
	}

	if (iParameter < 0 || iParameter >= pstudiohdr->GetNumPoseParameters())
	{
		return flValue;
	}

	const mstudioposeparamdesc_t &Pose = pstudiohdr->pPoseParameter( iParameter );

	if (Pose.loop || Pose.start == Pose.end)
	{
		return flValue;
	}

	return RangeCompressor( flValue, Pose.start, Pose.end, flBase );
}


//-----------------------------------------------------------------------------
// Purpose: Returns index number of a given named bone
// Input  : name of a bone
// Output :	Bone index number or -1 if bone not found
//-----------------------------------------------------------------------------
int CBaseAnimating::LookupBone( const char *szName )
{
	Assert( GetModelPtr() );

	return Studio_BoneIndexByName( GetModelPtr(), szName );
}


//=========================================================
//=========================================================
void CBaseAnimating::GetBonePosition ( int iBone, Vector &origin, QAngle &angles )
{
	studiohdr_t *pStudioHdr = GetModelPtr( );
	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetBonePosition: model missing");
		return;
	}

	if (iBone < 0 || iBone >= pStudioHdr->numbones)
	{
		Assert(!"CBaseAnimating::GetBonePosition: invalid bone index");
		return;
	}

	matrix3x4_t bonetoworld;
	GetBoneTransform( iBone, bonetoworld );
	
	MatrixAngles( bonetoworld, angles, origin );
}



//=========================================================
//=========================================================

void CBaseAnimating::GetBoneTransform( int iBone, matrix3x4_t &pBoneToWorld )
{
	studiohdr_t *pStudioHdr = GetModelPtr( );

	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetBoneTransform: model missing");
		return;
	}

	if (iBone < 0 || iBone >= pStudioHdr->numbones)
	{
		Assert(!"CBaseAnimating::GetBoneTransform: invalid bone index");
		return;
	}

	CBoneCache *pcache = GetBoneCache( );

	matrix3x4_t *pmatrix = pcache->GetCachedBone( iBone );

	if ( !pmatrix )
	{
		MatrixCopy( EntityToWorldTransform(), pBoneToWorld );
		return;
	}

	Assert( pmatrix );
	
	// FIXME
	MatrixCopy( *pmatrix, pBoneToWorld );
}

class CTraceFilterSkipNPCs : public CTraceFilterSimple
{
public:
	CTraceFilterSkipNPCs( const IHandleEntity *passentity, int collisionGroup )
		: CTraceFilterSimple( passentity, collisionGroup )
	{
	}

	virtual bool ShouldHitEntity( IHandleEntity *pServerEntity, int contentsMask )
	{
		if ( CTraceFilterSimple::ShouldHitEntity(pServerEntity, contentsMask) )
		{
			CBaseEntity *pEntity = EntityFromEntityHandle( pServerEntity );
			if ( pEntity->IsNPC() )
				return false;

			return true;
		}
		return false;
	}
};


//-----------------------------------------------------------------------------
// Purpose: Receives the clients IK floor position
//-----------------------------------------------------------------------------


void CBaseAnimating::SetIKGroundContactInfo( float minHeight, float maxHeight )
{
	m_flIKGroundContactTime = gpGlobals->curtime;
	m_flIKGroundMinHeight = minHeight;
	m_flIKGroundMaxHeight = maxHeight;
}

ConVar npc_height_adjust( "npc_height_adjust", "1", FCVAR_ARCHIVE, "Enable test mode for ik height adjustment" );

void CBaseAnimating::UpdateStepOrigin()
{
	if (!npc_height_adjust.GetBool())
	{
		m_flEstIkOffset = 0;
		m_flEstIkFloor = GetLocalOrigin().z;
		return;
	}

	if (m_flIKGroundContactTime > 0.2 && m_flIKGroundContactTime > gpGlobals->curtime - 0.2)
	{
		if ((GetFlags() & (FL_FLY | FL_SWIM)) == 0 && GetMoveParent() == NULL && GetGroundEntity() != NULL && !GetGroundEntity()->IsMoving())
		{
			Vector toAbs = GetAbsOrigin() - GetLocalOrigin();
			if (toAbs.z == 0.0)
			{
				// debounce floor location
				m_flEstIkFloor = m_flEstIkFloor * 0.2 + m_flIKGroundMinHeight * 0.8;
				// FIXME:  if it's an npc, get the StepHeight();

				// don't let heigth difference between min and max exceed step height
				float bias = clamp( (m_flIKGroundMaxHeight - m_flIKGroundMinHeight) - 18, 0, 18 );
				// save off reasonable offset
				m_flEstIkOffset = clamp( m_flEstIkFloor - GetAbsOrigin().z, -18.0f + bias, 0.0f );
				return;
			}
		}
	}

	// don't use floor offset, decay the value
	m_flEstIkOffset *= 0.5;
	m_flEstIkFloor = GetLocalOrigin().z;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the origin to use for model rendering
//-----------------------------------------------------------------------------

Vector CBaseAnimating::GetStepOrigin( void ) const 
{ 
	Vector tmp = GetLocalOrigin();
	tmp.z += m_flEstIkOffset;
	return tmp;
}

//-----------------------------------------------------------------------------
// Purpose: Returns the origin to use for model rendering
//-----------------------------------------------------------------------------

QAngle CBaseAnimating::GetStepAngles( void ) const
{
	// TODO: Add in body lean
	return GetLocalAngles();
}

//-----------------------------------------------------------------------------
// Purpose: Find IK collisions with world
// Input  : 
// Output :	fills out m_pIk targets, calcs floor offset for rendering
//-----------------------------------------------------------------------------

void CBaseAnimating::CalculateIKLocks( float currentTime )
{
	if ( m_pIk )
	{
		Ray_t ray;
		CTraceFilterSkipNPCs traceFilter( this, GetCollisionGroup() );
		Vector up;
		GetVectors( NULL, NULL, &up );
		// FIXME: check number of slots?
		for (int i = 0; i < m_pIk->m_target.Count(); i++)
		{
			trace_t trace;
			CIKTarget *pTarget = &m_pIk->m_target[i];

			if (!pTarget->IsActive())
				continue;

			switch( pTarget->type )
			{
			case IK_GROUND:
				{
					Vector p1, p2;
					VectorMA( pTarget->est.pos, pTarget->est.height, up, p1 );
					VectorMA( pTarget->est.pos, -pTarget->est.height, up, p2 );

					float r = max(pTarget->est.radius,1);

					// don't IK to other characters
					ray.Init( p1, p2, Vector(-r,-r,0), Vector(r,r,1) );
					enginetrace->TraceRay( ray, MASK_SOLID, &traceFilter, &trace );

					if (trace.startsolid)
					{
						ray.Init( pTarget->trace.p1, pTarget->est.pos, Vector(-r,-r,0), Vector(r,r,1) );

						enginetrace->TraceRay( ray, MASK_SOLID, &traceFilter, &trace );

						p1 = trace.endpos;
						VectorMA( p1, - pTarget->est.height, up, p2 );
						ray.Init( p1, p2, Vector(-r,-r,0), Vector(r,r,1) );

						enginetrace->TraceRay( ray, MASK_SOLID, &traceFilter, &trace );
					}

					if (!trace.startsolid)
					{
						if (trace.DidHitWorld())
						{
							pTarget->SetPosWithNormalOffset( trace.endpos, trace.plane.normal );
							pTarget->SetNormal( trace.plane.normal );
						}
						else
						{
							pTarget->SetPos( trace.endpos );
							pTarget->SetAngles( GetAbsAngles() );
						}

					}
				}
				break;
			case IK_ATTACHMENT:
				{
					// anything on the server?
				}
				break;
			}
		}
	}
}


void CBaseAnimating::Teleport( const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity )
{
	BaseClass::Teleport( newPosition, newAngles, newVelocity );
	if (m_pIk)
	{
		m_pIk->ClearTargets( );
	}
}


void BuildMatricesWithBoneMerge( 
	const studiohdr_t *pStudioHdr,
	const QAngle& angles, 
	const Vector& origin, 
	const Vector pos[MAXSTUDIOBONES],
	const Quaternion q[MAXSTUDIOBONES],
	matrix3x4_t bonetoworld[MAXSTUDIOBONES],
	CBaseAnimating *pParent,
	CBoneCache *pParentCache
	)
{
	studiohdr_t *fhdr = pParent->GetModelPtr();
	mstudiobone_t *pbones = pStudioHdr->pBone( 0 );

	matrix3x4_t rotationmatrix; // model to world transformation
	AngleMatrix( angles, origin, rotationmatrix);

	for ( int i=0; i < pStudioHdr->numbones; i++ )
	{
		// Now find the bone in the parent entity.
		bool merged = false;
		int parentBoneIndex = Studio_BoneIndexByName( fhdr, pbones[i].pszName() );
		if ( parentBoneIndex >= 0 )
		{
			matrix3x4_t *pMat = pParentCache->GetCachedBone( parentBoneIndex );
			if ( pMat )
			{
				MatrixCopy( *pMat, bonetoworld[ i ] );
				merged = true;
			}
		}

		if ( !merged )
		{
			// If we get down here, then the bone wasn't merged.
			matrix3x4_t bonematrix;
			QuaternionMatrix( q[i], pos[i], bonematrix );

			if (pbones[i].parent == -1) 
			{
				ConcatTransforms (rotationmatrix, bonematrix, bonetoworld[i]);
			} 
			else 
			{
				ConcatTransforms (bonetoworld[pbones[i].parent], bonematrix, bonetoworld[i]);
			}
		}
	}
}

void CBaseAnimating::SetupBones( matrix3x4_t *pBoneToWorld, int boneMask )
{
	VPROF_BUDGET( "CBaseAnimating::SetupBones", VPROF_BUDGETGROUP_SERVER_ANIM );
	
	CEngineCacheCriticalSection cacheCriticalSection( engineCache );

	Assert( GetModelPtr() );

	studiohdr_t *pStudioHdr = GetModelPtr( );

	Vector pos[MAXSTUDIOBONES];
	Quaternion q[MAXSTUDIOBONES];

	// adjust hit boxes based on IK driven offset
	Vector adjOrigin = GetAbsOrigin() + Vector( 0, 0, m_flEstIkOffset );

	// FIXME: pass this into Studio_BuildMatrices to skip transforms
	CBoneBitList boneComputed;
	if ( m_pIk )
	{
		m_iIKCounter++;
		m_pIk->Init( pStudioHdr, GetAbsAngles(), adjOrigin, gpGlobals->curtime, m_iIKCounter, boneMask );
		GetSkeleton( pos, q, boneMask );

		m_pIk->UpdateTargets( pos, q, pBoneToWorld, boneComputed );
		CalculateIKLocks( gpGlobals->curtime );
		m_pIk->SolveDependencies( pos, q, pBoneToWorld, boneComputed );
	}
	else
	{
		GetSkeleton( pos, q, boneMask );
	}
	
	CBaseAnimating *pParent = dynamic_cast< CBaseAnimating* >( GetMoveParent() );
	if ( pParent )
	{
		// We're doing bone merging, so do special stuff here.
		CBoneCache *pParentCache = pParent->GetBoneCache();
		if ( pParentCache )
		{
			BuildMatricesWithBoneMerge( 
				pStudioHdr, 
				GetAbsAngles(), 
				adjOrigin, 
				pos, 
				q, 
				pBoneToWorld, 
				pParent, 
				pParentCache );
			
			return;
		}
	}

	Studio_BuildMatrices( 
		pStudioHdr, 
		GetAbsAngles(), 
		adjOrigin, 
		pos, 
		q, 
		-1,
		pBoneToWorld,
		boneMask );
}

//=========================================================
//=========================================================
int CBaseAnimating::GetNumBones ( void )
{
	studiohdr_t *pStudioHdr = GetModelPtr( );
	if(pStudioHdr)
	{
		return pStudioHdr->numbones;
	}
	else
	{
		Assert(!"CBaseAnimating::GetNumBones: model missing");
		return 0;
	}
}


//=========================================================
//=========================================================

//-----------------------------------------------------------------------------
// Purpose: Returns index number of a given named attachment
// Input  : name of attachment
// Output :	attachment index number or -1 if attachment not found
//-----------------------------------------------------------------------------
int CBaseAnimating::LookupAttachment( const char *szName )
{
	studiohdr_t *pStudioHdr = GetModelPtr( );
	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::LookupAttachment: model missing");
		return 0;
	}

	// The +1 is to make attachment indices be 1-based (namely 0 == invalid or unused attachment)
	return Studio_FindAttachment( pStudioHdr, szName ) + 1;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the world location and world angles of an attachment
// Input  : attachment name
// Output :	location and angles
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetAttachment( const char *szName, Vector &absOrigin, QAngle &absAngles )
{																
	return GetAttachment( LookupAttachment( szName ), absOrigin, absAngles );
}

//-----------------------------------------------------------------------------
// Purpose: Returns the world location and world angles of an attachment
// Input  : attachment index
// Output :	location and angles
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetAttachment ( int iAttachment, Vector &absOrigin, QAngle &absAngles )
{
	matrix3x4_t attachmentToWorld;

	if (GetAttachment( iAttachment, attachmentToWorld ))
	{
		MatrixAngles( attachmentToWorld, absAngles, absOrigin );
		return true;
	}

	absOrigin = GetAbsOrigin();
	absAngles = GetAbsAngles();
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the world location and world angles of an attachment
// Input  : attachment index
// Output :	location and angles
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetAttachment( int iAttachment, matrix3x4_t &attachmentToWorld )
{
	studiohdr_t *pStudioHdr = GetModelPtr( );
	if (!pStudioHdr)
	{
		AssertOnce(!"CBaseAnimating::GetAttachment: model missing");
		return false;
	}

	if (iAttachment < 1 || iAttachment > pStudioHdr->GetNumAttachments())
	{
//		Assert(!"CBaseAnimating::GetAttachment: invalid attachment index");
		return false;
	}

	const mstudioattachment_t &pattachment = pStudioHdr->pAttachment( iAttachment-1 );
	int iBone = pStudioHdr->GetAttachmentBone( iAttachment-1 );

	matrix3x4_t bonetoworld;
	GetBoneTransform( iBone, bonetoworld );
	if ( (pattachment.flags & ATTACHMENT_FLAG_WORLD_ALIGN) == 0 )
	{
		ConcatTransforms( bonetoworld, pattachment.local, attachmentToWorld ); 
	}
	else
	{
		Vector vecLocalBonePos, vecWorldBonePos;
		MatrixGetColumn( pattachment.local, 3, vecLocalBonePos );
		VectorTransform( vecLocalBonePos, bonetoworld, vecWorldBonePos );

		SetIdentityMatrix( attachmentToWorld );
		MatrixSetColumn( vecWorldBonePos, 3, attachmentToWorld );
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the world location of an attachment
// Input  : attachment index
// Output :	location and angles
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetAttachment( const char *szName, Vector &absOrigin, Vector *forward, Vector *right, Vector *up )
{																
	return GetAttachment( LookupAttachment( szName ), absOrigin, forward, right, up );
}

bool CBaseAnimating::GetAttachment( int iAttachment, Vector &absOrigin, Vector *forward, Vector *right, Vector *up )
{
	matrix3x4_t attachmentToWorld;

	if (GetAttachment( iAttachment, attachmentToWorld ))
	{
		MatrixPosition( attachmentToWorld, absOrigin );
		if (forward)
		{
			MatrixGetColumn( attachmentToWorld, 0, forward );
		}
		if (right)
		{
			MatrixGetColumn( attachmentToWorld, 1, right );
		}
		if (up)
		{
			MatrixGetColumn( attachmentToWorld, 2, up );
		}
		return true;
	}

	absOrigin.Init();

	if (forward)
	{
		forward->Init();
	}
	if (right)
	{
		right->Init();
	}
	if (up)
	{
		up->Init();
	}
	return false;
}


//-----------------------------------------------------------------------------
// Returns the attachment in local space
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetAttachmentLocal( const char *szName, Vector &origin, QAngle &angles )
{
	return GetAttachmentLocal( LookupAttachment( szName ), origin, angles );
}

bool CBaseAnimating::GetAttachmentLocal( int iAttachment, Vector &origin, QAngle &angles )
{
	matrix3x4_t attachmentToEntity;

	if (GetAttachmentLocal( iAttachment, attachmentToEntity ))
	{
		MatrixAngles( attachmentToEntity, angles, origin );
		return true;
	}
	return false;
}

bool CBaseAnimating::GetAttachmentLocal( int iAttachment, matrix3x4_t &attachmentToLocal )
{
	matrix3x4_t attachmentToWorld;
	if (!GetAttachment(iAttachment, attachmentToWorld))
		return false;

	matrix3x4_t worldToEntity;
	MatrixInvert( EntityToWorldTransform(), worldToEntity );
	ConcatTransforms( worldToEntity, attachmentToWorld, attachmentToLocal ); 
	return true;
}


//=========================================================
//=========================================================
void CBaseAnimating::GetEyeballs( Vector &origin, QAngle &angles )
{
	studiohdr_t *pStudioHdr = GetModelPtr( );
	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetAttachment: model missing");
		return;
	}

	for (int iBodypart = 0; iBodypart < pStudioHdr->numbodyparts; iBodypart++)
	{
		mstudiobodyparts_t *pBodypart = pStudioHdr->pBodypart( iBodypart );
		for (int iModel = 0; iModel < pBodypart->nummodels; iModel++)
		{
			mstudiomodel_t *pModel = pBodypart->pModel( iModel );
			for (int iEyeball = 0; iEyeball < pModel->numeyeballs; iEyeball++)
			{
				mstudioeyeball_t *pEyeball = pModel->pEyeball( iEyeball );
				matrix3x4_t bonetoworld;
				GetBoneTransform( pEyeball->bone, bonetoworld );
				VectorTransform( pEyeball->org, bonetoworld,  origin );
				MatrixAngles( bonetoworld, angles ); // ???
			}
		}
	}
}


//=========================================================
//=========================================================
int CBaseAnimating::FindTransitionSequence( int iCurrentSequence, int iGoalSequence, int *piDir )
{
	Assert( GetModelPtr() );

	if (piDir == NULL)
	{
		int iDir = 1;
		int sequence = ::FindTransitionSequence( GetModelPtr(), iCurrentSequence, iGoalSequence, &iDir );
		if (iDir != 1)
			return -1;
		else
			return sequence;
	}

	return ::FindTransitionSequence( GetModelPtr(), iCurrentSequence, iGoalSequence, piDir );
}



int CBaseAnimating::GetEntryNode( int iSequence )
{
	studiohdr_t *pstudiohdr = GetModelPtr();
	if (! pstudiohdr)
		return 0;

	return pstudiohdr->EntryNode( iSequence );
}


int CBaseAnimating::GetExitNode( int iSequence )
{
	studiohdr_t *pstudiohdr = GetModelPtr();
	if (! pstudiohdr)
		return 0;
	
	return pstudiohdr->ExitNode( iSequence );
}


float CBaseAnimating::GetExitPhase( int iSequence )
{
	studiohdr_t *pstudiohdr = GetModelPtr();
	if (! pstudiohdr)
		return 0;
	
	mstudioseqdesc_t &seqdesc = pstudiohdr->pSeqdesc( iSequence );

	return seqdesc.exitphase;
}

//=========================================================
//=========================================================

void CBaseAnimating::SetBodygroup( int iGroup, int iValue )
{
	Assert( GetModelPtr() );

	int newBody = m_nBody;
	::SetBodygroup( GetModelPtr( ), newBody, iGroup, iValue );
	m_nBody = newBody;
}

int CBaseAnimating::GetBodygroup( int iGroup )
{
	Assert( GetModelPtr() );

	return ::GetBodygroup( GetModelPtr( ), m_nBody, iGroup );
}

const char *CBaseAnimating::GetBodygroupName( int iGroup )
{
	Assert( GetModelPtr() );

	return ::GetBodygroupName( GetModelPtr( ), iGroup );
}

int CBaseAnimating::FindBodygroupByName( const char *name )
{
	Assert( GetModelPtr() );

	return ::FindBodygroupByName( GetModelPtr( ), name );
}

int CBaseAnimating::GetBodygroupCount( int iGroup )
{
	Assert( GetModelPtr() );

	return ::GetBodygroupCount( GetModelPtr( ), iGroup );
}

int CBaseAnimating::GetNumBodyGroups( void )
{
	Assert( GetModelPtr() );

	return ::GetNumBodyGroups( GetModelPtr( ) );
}

int CBaseAnimating::ExtractBbox( int sequence, Vector& mins, Vector& maxs )
{
	Assert( GetModelPtr() );

	return ::ExtractBbox( GetModelPtr( ), sequence, mins, maxs );
}

//=========================================================
//=========================================================

void CBaseAnimating::SetSequenceBox( void )
{
	Vector mins, maxs;

	// Get sequence bbox
	if ( ExtractBbox( GetSequence(), mins, maxs ) )
	{
		// expand box for rotation
		// find min / max for rotations
		float yaw = GetLocalAngles().y * (M_PI / 180.0);
		
		Vector xvector, yvector;
		xvector.x = cos(yaw);
		xvector.y = sin(yaw);
		yvector.x = -sin(yaw);
		yvector.y = cos(yaw);
		Vector bounds[2];

		bounds[0] = mins;
		bounds[1] = maxs;
		
		Vector rmin( 9999, 9999, 9999 );
		Vector rmax( -9999, -9999, -9999 );
		Vector base, transformed;

		for (int i = 0; i <= 1; i++ )
		{
			base.x = bounds[i].x;
			for ( int j = 0; j <= 1; j++ )
			{
				base.y = bounds[j].y;
				for ( int k = 0; k <= 1; k++ )
				{
					base.z = bounds[k].z;
					
				// transform the point
					transformed.x = xvector.x*base.x + yvector.x*base.y;
					transformed.y = xvector.y*base.x + yvector.y*base.y;
					transformed.z = base.z;
					
					for ( int l = 0; l < 3; l++ )
					{
						if (transformed[l] < rmin[l])
							rmin[l] = transformed[l];
						if (transformed[l] > rmax[l])
							rmax[l] = transformed[l];
					}
				}
			}
		}
		rmin.z = 0;
		rmax.z = rmin.z + 1;
		UTIL_SetSize( this, rmin, rmax );
	}
}

//=========================================================
//=========================================================
int CBaseAnimating::RegisterPrivateActivity( const char *pszActivityName )
{
	return ActivityList_RegisterPrivateActivity( pszActivityName );
}

//-----------------------------------------------------------------------------
// Purpose: Notifies the console that this entity could not retrieve an
//			animation sequence for the specified activity. This probably means
//			there's a typo in the model QC file, or the sequence is missing
//			entirely.
//			
//
// Input  : iActivity - The activity that failed to resolve to a sequence.
//
//
// NOTE   :	IMPORTANT - Something needs to be done so that private activities
//			(which are allowed to collide in the activity list) remember each
//			entity that registered an activity there, and the activity name
//			each character registered.
//-----------------------------------------------------------------------------
void CBaseAnimating::ReportMissingActivity( int iActivity )
{
	Msg( "%s has no sequence for act:%s\n", GetClassname(), ActivityList_NameForIndex(iActivity) );
}


int CBaseAnimating::GetNumFlexControllers( void )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	return pstudiohdr->numflexcontrollers;
}


const char *CBaseAnimating::GetFlexDescFacs( int iFlexDesc )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	mstudioflexdesc_t *pflexdesc = pstudiohdr->pFlexdesc( iFlexDesc );

	return pflexdesc->pszFACS( );
}

const char *CBaseAnimating::GetFlexControllerName( int iFlexController )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	mstudioflexcontroller_t *pflexcontroller = pstudiohdr->pFlexcontroller( iFlexController );

	return pflexcontroller->pszName( );
}

const char *CBaseAnimating::GetFlexControllerType( int iFlexController )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	mstudioflexcontroller_t *pflexcontroller = pstudiohdr->pFlexcontroller( iFlexController );

	return pflexcontroller->pszType( );
}

//-----------------------------------------------------------------------------
// Purpose: Converts the ground speed of the animating entity into a true velocity
// Output : Vector - velocity of the character at its current m_flGroundSpeed
//-----------------------------------------------------------------------------
Vector CBaseAnimating::GetGroundSpeedVelocity( void )
{
	studiohdr_t *pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return vec3_origin;

	QAngle  vecAngles;
	Vector	vecVelocity;

	vecAngles.y = GetSequenceMoveYaw( GetSequence() );
	vecAngles.x = 0;
	vecAngles.z = 0;

	vecAngles.y += GetLocalAngles().y;

	AngleVectors( vecAngles, &vecVelocity );

	vecVelocity = vecVelocity * m_flGroundSpeed;

	return vecVelocity;
}


//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
float CBaseAnimating::GetInstantaneousVelocity( float flInterval )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	// FIXME: someone needs to check for last frame, etc.
	float flNextCycle = GetCycle() + flInterval * GetSequenceCycleRate( GetSequence() ) * m_flPlaybackRate;

	Vector vecVelocity;
	Studio_SeqVelocity( pstudiohdr, GetSequence(), flNextCycle, GetPoseParameterArray(), vecVelocity );
	vecVelocity *= m_flPlaybackRate;

	return vecVelocity.Length();
}



//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
float CBaseAnimating::GetEntryVelocity( int iSequence )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	Vector vecVelocity;
	Studio_SeqVelocity( pstudiohdr, iSequence, 0.0, GetPoseParameterArray(), vecVelocity );

	return vecVelocity.Length();
}

float CBaseAnimating::GetExitVelocity( int iSequence )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	Vector vecVelocity;
	Studio_SeqVelocity( pstudiohdr, iSequence, 1.0, GetPoseParameterArray(), vecVelocity );

	return vecVelocity.Length();
}

//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetIntervalMovement( float flIntervalUsed, bool &bMoveSeqFinished, Vector &newPosition, QAngle &newAngles )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return false;

	float flComputedCycleRate = GetSequenceCycleRate( GetSequence() );

	float flNextCycle = GetCycle() + flIntervalUsed * flComputedCycleRate * m_flPlaybackRate;

	if ((!m_bSequenceLoops) && flNextCycle > 1.0)
	{
		flIntervalUsed = GetCycle() / (flComputedCycleRate * m_flPlaybackRate);
		flNextCycle = 1.0;
		bMoveSeqFinished = true;
	}
	else
	{
		bMoveSeqFinished = false;
	}

	Vector deltaPos;
	QAngle deltaAngles;

	if (Studio_SeqMovement( pstudiohdr, GetSequence(), GetCycle(), flNextCycle, GetPoseParameterArray(), deltaPos, deltaAngles ))
	{
		VectorYawRotate( deltaPos, GetLocalAngles().y, deltaPos );
		newPosition = GetLocalOrigin() + deltaPos;
		newAngles.Init();
		newAngles.y = GetLocalAngles().y + deltaAngles.y;
		return true;
	}
	else
	{
		newPosition = GetLocalOrigin();
		newAngles = GetLocalAngles();
		return false;
	}
}



//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
bool CBaseAnimating::GetSequenceMovement( int nSequence, float fromCycle, float toCycle, Vector &deltaPosition, QAngle &deltaAngles )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return false;

	return Studio_SeqMovement( pstudiohdr, nSequence, fromCycle, toCycle, GetPoseParameterArray(), deltaPosition, deltaAngles );
}


//-----------------------------------------------------------------------------
// Purpose: find frame where they animation has moved a given distance.
// Output :
//-----------------------------------------------------------------------------
float CBaseAnimating::GetMovementFrame( float flDist )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return 0;

	float t = Studio_FindSeqDistance( pstudiohdr, GetSequence(), GetPoseParameterArray(), flDist );

	return t;
}


//-----------------------------------------------------------------------------
// Purpose: does a specific sequence have movement?
// Output :
//-----------------------------------------------------------------------------
bool CBaseAnimating::HasMovement( int iSequence )
{
	studiohdr_t *pstudiohdr = GetModelPtr( );
	if (! pstudiohdr)
		return false;

	// FIXME: this needs to check to see if there are keys, and the object is walking
	Vector deltaPos;
	QAngle deltaAngles;
	if (Studio_SeqMovement( pstudiohdr, iSequence, 0.0f, 1.0f, GetPoseParameterArray(), deltaPos, deltaAngles ))
	{
		return true;
	}

	return false;
}



//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *szModelName - 
//-----------------------------------------------------------------------------
void CBaseAnimating::SetModel( const char *szModelName )
{
	if ( szModelName[0] )
	{
		int modelIndex = modelinfo->GetModelIndex( szModelName );
		const model_t *model = modelinfo->GetModel( modelIndex );
		if ( model && ( modelinfo->GetModelType( model ) != mod_studio ) )
		{
			Msg( "Setting CBaseAnimating to non-studio model %s  (type:%i)\n",	szModelName, modelinfo->GetModelType( model ) );
		}
	}
	Studio_DestroyBoneCache( m_boneCacheHandle );
	m_boneCacheHandle = 0;

	UTIL_SetModel( this, szModelName );

	InitBoneControllers( );
}

studiohdr_t *CBaseAnimating::GetModelPtr( void ) 
{ 
	model_t *model = GetModel();
	if ( !model )
		return NULL;

	// In TF2, certain base animating classes (gates, etc) can be mod_brush
	if ( modelinfo->GetModelType( model ) != mod_studio )
		return NULL;

	return static_cast< studiohdr_t * >( modelinfo->GetModelExtraData( model ) ); 
}

//-----------------------------------------------------------------------------
// Purpose: return the index to the shared bone cache
// Output :
//-----------------------------------------------------------------------------
CBoneCache *CBaseAnimating::GetBoneCache( void )
{
	studiohdr_t *pStudioHdr = GetModelPtr( );
	Assert(pStudioHdr);

	CBoneCache *pcache = Studio_GetBoneCache( m_boneCacheHandle );
	int boneMask = BONE_USED_BY_HITBOX | BONE_USED_BY_ATTACHMENT;
	if ( pcache )
	{
		if ( pcache->IsValid( gpGlobals->curtime ) )
		{
			// in memory and still valid, use it!
			return pcache;
		}
		// in memory, but not the same bone set, destroy & rebuild
		if ( pcache->m_boneMask != boneMask )
		{
			Studio_DestroyBoneCache( m_boneCacheHandle );
			m_boneCacheHandle = 0;
			pcache = NULL;
		}
	}

	matrix3x4_t bonetoworld[MAXSTUDIOBONES];
	SetupBones( bonetoworld, boneMask );

	if ( pcache )
	{
		// still in memory but out of date, refresh the bones.
		pcache->UpdateBones( bonetoworld, pStudioHdr->numbones, gpGlobals->curtime );
	}
	else
	{
		bonecacheparams_t params;
		params.pStudioHdr = pStudioHdr;
		params.pBoneToWorld = bonetoworld;
		params.curtime = gpGlobals->curtime;
		params.boneMask = boneMask;

		m_boneCacheHandle = Studio_CreateBoneCache( params );
		pcache = Studio_GetBoneCache( m_boneCacheHandle );
	}
	Assert(pcache);
	return pcache;
}


void CBaseAnimating::InvalidateBoneCache( void )
{
	Studio_InvalidateBoneCache( m_boneCacheHandle );
}

bool CBaseAnimating::TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	if ( ray.m_IsRay && IsSolidFlagSet( FSOLID_CUSTOMRAYTEST ))
	{
		if (!TestHitboxes( ray, fContentsMask, tr ))
			return true;

		return tr.DidHit();
	}

	if ( !ray.m_IsRay && IsSolidFlagSet( FSOLID_CUSTOMBOXTEST ))
	{
		if (!TestHitboxes( ray, fContentsMask, tr ))
			return true;

		return tr.DidHit();
	}

	// We shouldn't get here.
	Assert(0);
	return false;
}

bool CBaseAnimating::TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	studiohdr_t *pStudioHdr = GetModelPtr( );
	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetBonePosition: model missing");
		return false;
	}

	mstudiohitboxset_t *set = pStudioHdr->pHitboxSet( m_nHitboxSet );
	if ( !set || !set->numhitboxes )
		return false;

	CBoneCache *pcache = GetBoneCache( );

	matrix3x4_t *hitboxbones[MAXSTUDIOBONES];
	pcache->ReadCachedBonePointers( hitboxbones, pStudioHdr->numbones );

	if ( TraceToStudio( ray, pStudioHdr, set, hitboxbones, fContentsMask, tr ) )
	{
		mstudiobbox_t *pbox = set->pHitbox( tr.hitbox );
		mstudiobone_t *pBone = pStudioHdr->pBone(pbox->bone);
		tr.surface.name = "**studio**";
		tr.surface.flags = SURF_HITBOX;
		tr.surface.surfaceProps = physprops->GetSurfaceIndex( pBone->pszSurfaceProp() );
	}
	return true;
}

void CBaseAnimating::InitBoneControllers ( void ) // FIXME: rename
{
	int i;

	studiohdr_t *pStudioHdr = GetModelPtr( );
	if (!pStudioHdr)
		return;

	for (i = 0; i < pStudioHdr->numbonecontrollers; i++)
	{
		SetBoneController( i, 0.0 );
	}

	for (i = 0; i < pStudioHdr->GetNumPoseParameters(); i++)
	{
		SetPoseParameter( i, 0.0 );
	}
}

//=========================================================
//=========================================================
float CBaseAnimating::SetBoneController ( int iController, float flValue )
{
	Assert( GetModelPtr() );

	studiohdr_t *pmodel = (studiohdr_t*)GetModelPtr();

	Assert(iController >= 0 && iController < NUM_BONECTRLS);

	float newValue;
	float retVal = Studio_SetController( pmodel, iController, flValue, newValue );
	m_flEncodedController.Set( iController, newValue );

	return retVal;
}

//=========================================================
//=========================================================
float CBaseAnimating::GetBoneController ( int iController )
{
	Assert( GetModelPtr() );

	studiohdr_t *pmodel = (studiohdr_t*)GetModelPtr();

	return Studio_GetController( pmodel, iController, m_flEncodedController[iController] );
}

//------------------------------------------------------------------------------
// Purpose : Returns velcocity of the NPC from it's animation.  
//			 If physically simulated gets velocity from physics object
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CBaseAnimating::GetVelocity(Vector *vVelocity, AngularImpulse *vAngVelocity) 
{
	if ( GetMoveType() == MOVETYPE_VPHYSICS )
	{
		BaseClass::GetVelocity(vVelocity,vAngVelocity);
	}
	else if ( !(GetFlags() & FL_ONGROUND) )
	{
		BaseClass::GetVelocity(vVelocity,vAngVelocity);
	}
	else
	{
		if (vVelocity != NULL)
		{
			Vector	vRawVel;

			GetSequenceLinearMotion( GetSequence(), &vRawVel );

			// Build a rotation matrix from NPC orientation
			matrix3x4_t fRotateMatrix;
			AngleMatrix(GetLocalAngles(), fRotateMatrix);
			VectorRotate( vRawVel, fRotateMatrix, *vVelocity);
		}
		if (vAngVelocity != NULL)
		{
			QAngle tmp = GetLocalAngularVelocity();
			QAngleToAngularImpulse( tmp, *vAngVelocity );
		}
	}
}


//=========================================================
//=========================================================

void CBaseAnimating::GetSkeleton( Vector pos[], Quaternion q[], int boneMask )
{
	studiohdr_t *pStudioHdr = GetModelPtr();
	if(!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetSkeleton() without a model");
		return;
	}

	InitPose( pStudioHdr, pos, q );

	AccumulatePose( pStudioHdr, m_pIk, pos, q, GetSequence(), GetCycle(), GetPoseParameterArray(), boneMask, 1.0, gpGlobals->curtime );

	if ( m_pIk )
	{
		CIKContext auto_ik;
		auto_ik.Init( pStudioHdr, GetAbsAngles(), GetAbsOrigin(), gpGlobals->curtime, 0, boneMask );
		CalcAutoplaySequences( pStudioHdr, &auto_ik, pos, q, GetPoseParameterArray(), boneMask, gpGlobals->curtime );
	}
	else
	{
		CalcAutoplaySequences( pStudioHdr, NULL, pos, q, GetPoseParameterArray(), boneMask, gpGlobals->curtime );
	}
	CalcBoneAdj( pStudioHdr, pos, q, GetEncodedControllerArray(), boneMask );
}

int CBaseAnimating::DrawDebugTextOverlays(void) 
{
	int text_offset = BaseClass::DrawDebugTextOverlays();

	if (m_debugOverlays & OVERLAY_TEXT_BIT) 
	{
		// ----------------
		// Print Look time
		// ----------------
		char tempstr[1024];
		Q_snprintf(tempstr, sizeof(tempstr), "Sequence: (%3d) %s",GetSequence(), GetSequenceName( GetSequence() ) );
		NDebugOverlay::EntityText(entindex(),text_offset,tempstr,0);
		text_offset++;
		const char *pActname = GetSequenceActivityName(GetSequence());
		if ( pActname && strlen(pActname) )
		{
			Q_snprintf(tempstr, sizeof(tempstr), "Activity %s", pActname );
			NDebugOverlay::EntityText(entindex(),text_offset,tempstr,0);
			text_offset++;
		}

		Q_snprintf(tempstr, sizeof(tempstr), "Cycle: %.5f", (float)GetCycle() );
		NDebugOverlay::EntityText(entindex(),text_offset,tempstr,0);
		text_offset++;
	}
	return text_offset;
}

//-----------------------------------------------------------------------------
// Purpose: Force a clientside-animating entity to reset it's frame
//-----------------------------------------------------------------------------
void CBaseAnimating::ResetClientsideFrame( void )
{
	// TODO: Once we can chain MSG_ENTITY messages, use one of them
	m_bClientSideFrameReset = !(bool)m_bClientSideFrameReset;
	NetworkStateChanged();
}

//-----------------------------------------------------------------------------
// Purpose: Returns the origin at which to play an inputted dispatcheffect 
//-----------------------------------------------------------------------------
void CBaseAnimating::GetInputDispatchEffectPosition( const char *sInputString, Vector &pOrigin, QAngle &pAngles )
{
	// See if there's a specified attachment point
	int iAttachment;
	if ( GetModelPtr() && sscanf( sInputString, "%d", &iAttachment ) )
	{
		if ( !GetAttachment( iAttachment, pOrigin, pAngles ) )
		{
			Msg( "ERROR: Mapmaker tried to spawn DispatchEffect %s, but %s has no attachment %d\n", 
				sInputString, STRING(GetModelName()), iAttachment );
		}
		return;
	}

	BaseClass::GetInputDispatchEffectPosition( sInputString, pOrigin, pAngles );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : setnum - 
//-----------------------------------------------------------------------------
void CBaseAnimating::SetHitboxSet( int setnum )
{
#ifdef _DEBUG
	studiohdr_t *pStudioHdr = GetModelPtr();
	if ( !pStudioHdr )
		return;

	if (setnum > pStudioHdr->numhitboxsets)
	{
		// Warn if an bogus hitbox set is being used....
		static bool s_bWarned = false;
		if (!s_bWarned)
		{
			Warning("Using bogus hitbox set in entity %s!\n", GetClassname() );
			s_bWarned = true;
		}
		setnum = 0;
	}
#endif

	m_nHitboxSet = setnum;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *setname - 
//-----------------------------------------------------------------------------
void CBaseAnimating::SetHitboxSetByName( const char *setname )
{
	Assert( GetModelPtr() );
	m_nHitboxSet = FindHitboxSetByName( GetModelPtr(), setname );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CBaseAnimating::GetHitboxSet( void )
{
	return m_nHitboxSet;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : char const
//-----------------------------------------------------------------------------
const char *CBaseAnimating::GetHitboxSetName( void )
{
	Assert( GetModelPtr() );
	return ::GetHitboxSetName( GetModelPtr(), m_nHitboxSet );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CBaseAnimating::GetHitboxSetCount( void )
{
	Assert( GetModelPtr() );
	return ::GetHitboxSetCount( GetModelPtr() );
}

static Vector	hullcolor[8] = 
{
	Vector( 1.0, 1.0, 1.0 ),
	Vector( 1.0, 0.5, 0.5 ),
	Vector( 0.5, 1.0, 0.5 ),
	Vector( 1.0, 1.0, 0.5 ),
	Vector( 0.5, 0.5, 1.0 ),
	Vector( 1.0, 0.5, 1.0 ),
	Vector( 0.5, 1.0, 1.0 ),
	Vector( 1.0, 1.0, 1.0 )
};

//-----------------------------------------------------------------------------
// Purpose: Send the current hitboxes for this model to the client ( to compare with
//  r_drawentities 3 client side boxes ).
// WARNING:  This uses a ton of bandwidth, only use on a listen server
//-----------------------------------------------------------------------------
void CBaseAnimating::DrawServerHitboxes( float duration /*= 0.0f*/, bool monocolor /*= false*/  )
{
	studiohdr_t *pStudioHdr = GetModelPtr();
	if ( !pStudioHdr )
		return;

	mstudiohitboxset_t *set =pStudioHdr->pHitboxSet( m_nHitboxSet );
	if ( !set )
		return;

	Vector position;
	QAngle angles;

	int r = 0;
	int g = 0;
	int b = 255;

	for ( int i = 0; i < set->numhitboxes; i++ )
	{
		mstudiobbox_t *pbox = set->pHitbox( i );

		GetBonePosition( pbox->bone, position, angles );

		if ( !monocolor )
		{
			int j = (pbox->group % 8);
			
			r = ( int ) ( 255.0f * hullcolor[j][0] );
			g = ( int ) ( 255.0f * hullcolor[j][1] );
			b = ( int ) ( 255.0f * hullcolor[j][2] );
		}

		NDebugOverlay::BoxAngles( position, pbox->bbmin, pbox->bbmax, angles, r, g, b, 0 ,duration );
	}
}

int CBaseAnimating::GetHitboxBone( int hitboxIndex )
{
	studiohdr_t *pStudioHdr = GetModelPtr();
	if ( pStudioHdr )
	{
		mstudiohitboxset_t *set =pStudioHdr->pHitboxSet( m_nHitboxSet );
		if ( set && hitboxIndex < set->numhitboxes )
		{
			return set->pHitbox( hitboxIndex )->bone;
		}
	}
	return 0;
}


//-----------------------------------------------------------------------------
// Computes a box that surrounds all hitboxes
//-----------------------------------------------------------------------------
bool CBaseAnimating::ComputeHitboxSurroundingBox( Vector *pVecWorldMins, Vector *pVecWorldMaxs )
{
	// Note that this currently should not be called during Relink because of IK.
	// The code below recomputes bones so as to get at the hitboxes,
	// which causes IK to trigger, which causes raycasts against the other entities to occur,
	// which is illegal to do while in the Relink phase.

	studiohdr_t *pStudioHdr = modelinfo->GetStudiomodel( GetModel() );
	if (!pStudioHdr)
		return false;

	mstudiohitboxset_t *set = pStudioHdr->pHitboxSet( m_nHitboxSet );
	if ( !set || !set->numhitboxes )
		return false;

	CBoneCache *pCache = GetBoneCache();

	// Compute a box in world space that surrounds this entity
	pVecWorldMins->Init( FLT_MAX, FLT_MAX, FLT_MAX );
	pVecWorldMaxs->Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );

	Vector vecBoxAbsMins, vecBoxAbsMaxs;
	for ( int i = 0; i < set->numhitboxes; i++ )
	{
		mstudiobbox_t *pbox = set->pHitbox(i);
		matrix3x4_t *pMatrix = pCache->GetCachedBone(pbox->bone);

		if ( pMatrix )
		{
			TransformAABB( *pMatrix, pbox->bbmin, pbox->bbmax, vecBoxAbsMins, vecBoxAbsMaxs );
			VectorMin( *pVecWorldMins, vecBoxAbsMins, *pVecWorldMins );
			VectorMax( *pVecWorldMaxs, vecBoxAbsMaxs, *pVecWorldMaxs );
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Computes a box that surrounds all hitboxes, in entity space
//-----------------------------------------------------------------------------
bool CBaseAnimating::ComputeEntitySpaceHitboxSurroundingBox( Vector *pVecWorldMins, Vector *pVecWorldMaxs )
{
	// Note that this currently should not be called during position recomputation because of IK.
	// The code below recomputes bones so as to get at the hitboxes,
	// which causes IK to trigger, which causes raycasts against the other entities to occur,
	// which is illegal to do while in the computeabsposition phase.

	studiohdr_t *pStudioHdr = modelinfo->GetStudiomodel( GetModel() );
	if (!pStudioHdr)
		return false;

	mstudiohitboxset_t *set = pStudioHdr->pHitboxSet( m_nHitboxSet );
	if ( !set || !set->numhitboxes )
		return false;

	CBoneCache *pCache = GetBoneCache();
	matrix3x4_t *hitboxbones[MAXSTUDIOBONES];
	pCache->ReadCachedBonePointers( hitboxbones, pStudioHdr->numbones );

	// Compute a box in world space that surrounds this entity
	pVecWorldMins->Init( FLT_MAX, FLT_MAX, FLT_MAX );
	pVecWorldMaxs->Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );

	matrix3x4_t worldToEntity, boneToEntity;
	MatrixInvert( EntityToWorldTransform(), worldToEntity );

	Vector vecBoxAbsMins, vecBoxAbsMaxs;
	for ( int i = 0; i < set->numhitboxes; i++ )
	{
		mstudiobbox_t *pbox = set->pHitbox(i);

		ConcatTransforms( worldToEntity, *hitboxbones[pbox->bone], boneToEntity );
		TransformAABB( boneToEntity, pbox->bbmin, pbox->bbmax, vecBoxAbsMins, vecBoxAbsMaxs );
		VectorMin( *pVecWorldMins, vecBoxAbsMins, *pVecWorldMins );
		VectorMax( *pVecWorldMaxs, vecBoxAbsMaxs, *pVecWorldMaxs );
	}
	return true;
}


int CBaseAnimating::GetPhysicsBone( int boneIndex )
{
	studiohdr_t *pStudioHdr = GetModelPtr();
	if ( pStudioHdr )
	{
		if ( boneIndex >= 0 && boneIndex < pStudioHdr->numbones )
			return pStudioHdr->pBone( boneIndex )->physicsbone;
	}
	return 0;
}

bool CBaseAnimating::LookupHitbox( const char *szName, int& outSet, int& outBox )
{
	studiohdr_t* pHdr = GetModelPtr();

	outSet = -1;
	outBox = -1;

	if( !pHdr )
		return false;

	for( int set=0; set < pHdr->numhitboxsets; set++ )
	{
		for( int i = 0; i < pHdr->iHitboxCount(set); i++ )
		{
			mstudiobbox_t* pBox = pHdr->pHitbox( i, set );
			
			if( !pBox )
				continue;
			
			const char* szBoxName = pBox->pszHitboxName(pHdr);
			if( Q_stricmp( szBoxName, szName ) == 0 )
			{
				outSet = set;
				outBox = i;
				return true;
			}
		}
	}

	return false;
}

void CBaseAnimating::CopyAnimationDataFrom( CBaseAnimating *pSource )
{
	this->SetModelName( pSource->GetModelName() );
	this->SetModelIndex( pSource->GetModelIndex() );
	this->SetCycle( pSource->GetCycle() );
	this->SetEffects( pSource->GetEffects() | EF_NOINTERP );
	this->SetSequence( pSource->GetSequence() );
	this->m_flAnimTime = pSource->m_flAnimTime;
	this->m_nBody = pSource->m_nBody;
	this->m_nSkin = pSource->m_nSkin;
}

int CBaseAnimating::GetHitboxesFrontside( int *boxList, int boxMax, const Vector &normal, float dist )
{
	int count = 0;
	studiohdr_t *pStudioHdr = GetModelPtr();
	if ( pStudioHdr )
	{
		mstudiohitboxset_t *set = pStudioHdr->pHitboxSet( m_nHitboxSet );
		if ( set )
		{
			matrix3x4_t matrix;
			for ( int b = 0; b < set->numhitboxes; b++ )
			{
				mstudiobbox_t *pbox = set->pHitbox( b );

				GetBoneTransform( pbox->bone, matrix );
				Vector center = (pbox->bbmax + pbox->bbmin) * 0.5;
				Vector centerWs;
				VectorTransform( center, matrix, centerWs );
				if ( DotProduct( centerWs, normal ) >= dist )
				{
					if ( count < boxMax )
					{
						boxList[count] = b;
						count++;
					}
				}
			}
		}
	}

	return count;
}

void CBaseAnimating::EnableServerIK()
{
	if (!m_pIk)
	{
		m_pIk = new CIKContext;
		m_iIKCounter = 0;
	}
}

void CBaseAnimating::DisableServerIK()
{
	delete m_pIk;
	m_pIk = NULL;
}

Activity CBaseAnimating::GetSequenceActivity( int iSequence )
{
	if( iSequence == -1 )
	{
		return ACT_INVALID;
	}

	if ( !GetModelPtr() )
		return ACT_INVALID;

	return (Activity)::GetSequenceActivity( GetModelPtr(), iSequence );
}

void CBaseAnimating::ModifyOrAppendCriteria( AI_CriteriaSet& set )
{
	BaseClass::ModifyOrAppendCriteria( set );

	// TODO
	// Append any animation state parameters here
}


void CBaseAnimating::DoMuzzleFlash()
{
	m_nMuzzleFlashParity = (m_nMuzzleFlashParity+1) & ((1 << EF_MUZZLEFLASH_BITS) - 1);
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : scale - 
//-----------------------------------------------------------------------------
void CBaseAnimating::SetModelWidthScale( float scale, float change_duration /*= 0.0f*/  )
{
	if ( change_duration > 0.0f )
	{
		ModelWidthScale *mvs = ( ModelWidthScale * )CreateDataObject( MODELWIDTHSCALE );
		mvs->m_flModelWidthScaleStart = m_flModelWidthScale;
		mvs->m_flModelWidthScaleGoal = scale;
		mvs->m_flModelWidthScaleStartTime = gpGlobals->curtime;
		mvs->m_flModelWidthScaleFinishTime = mvs->m_flModelWidthScaleStartTime + change_duration;
	}
	else
	{
		m_flModelWidthScale = scale;
		if ( HasDataObjectType( MODELWIDTHSCALE ) )
		{
			DestroyDataObject( MODELWIDTHSCALE );
		}
	}
}

void CBaseAnimating::UpdateModelWidthScale()
{
	ModelWidthScale *mvs = ( ModelWidthScale * )GetDataObject( MODELWIDTHSCALE );
	if ( !mvs )
	{
		return;
	}

	float dt = mvs->m_flModelWidthScaleFinishTime - mvs->m_flModelWidthScaleStartTime;
	Assert( dt > 0.0f );

	float frac = ( gpGlobals->curtime - mvs->m_flModelWidthScaleStartTime ) / dt;
	frac = clamp( frac, 0.0f, 1.0f );

	if ( gpGlobals->curtime >= mvs->m_flModelWidthScaleFinishTime )
	{
		m_flModelWidthScale = mvs->m_flModelWidthScaleGoal;
		DestroyDataObject( MODELWIDTHSCALE );
	}
	else
	{
		m_flModelWidthScale = Lerp( frac, mvs->m_flModelWidthScaleStart, mvs->m_flModelWidthScaleGoal );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float CBaseAnimating::GetModelWidthScale() const
{
	return m_flModelWidthScale;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseAnimating::Ignite( float flFlameLifetime, bool bNPCOnly, float flSize, bool bCalledByLevelDesigner )
{
	if( IsOnFire() )
		return;

	bool bIsNPC = IsNPC();

	// Right now this prevents stuff we don't want to catch on fire from catching on fire.
	if( bNPCOnly && bIsNPC == false )
	{
		return;
	}

	if( bIsNPC == true && bCalledByLevelDesigner == false )
	{
		CAI_BaseNPC *pNPC = MyNPCPointer();

		if ( pNPC && pNPC->AllowedToIgnite() == false )
			 return;
	}

	CEntityFlame *pFlame = CEntityFlame::Create( this );
	if (pFlame)
	{
		pFlame->SetLifetime( flFlameLifetime );
		AddFlag( FL_ONFIRE );

		SetEffectEntity( pFlame );

		if ( flSize > 0.0f )
		{
			pFlame->SetSize( flSize );
		}
	}

	m_OnIgnite.FireOutput( this, this );
}

//-----------------------------------------------------------------------------
// Fades out!
//-----------------------------------------------------------------------------
bool CBaseAnimating::Dissolve( const char *pMaterialName, float flStartTime, bool bNPCOnly, int nDissolveType )
{
	// Right now this prevents stuff we don't want to catch on fire from catching on fire.
	if( bNPCOnly && !(GetFlags() & FL_NPC) )
		return false;

	// Can't dissolve twice
	if ( IsDissolving() )
		return false;

	bool bRagdollCreated = false;
	CEntityDissolve *pDissolve = CEntityDissolve::Create( this, pMaterialName, flStartTime, nDissolveType, &bRagdollCreated );
	if (pDissolve)
	{
		SetEffectEntity( pDissolve );

		AddFlag( FL_DISSOLVING );
		m_flDissolveStartTime = flStartTime;
	}

	return bRagdollCreated;
}


//-----------------------------------------------------------------------------
// Make a model look as though it's burning. 
//-----------------------------------------------------------------------------
void CBaseAnimating::Scorch( int rate, int floor )
{
	color32 color = GetRenderColor();

	if( color.r > floor )
		color.r -= rate;

	if( color.g > floor )
		color.g -= rate;

	if( color.b > floor )
		color.b -= rate;

	SetRenderColor( color.r, color.g, color.b );
}


void CBaseAnimating::ResetSequence(int nSequence)
{
	if (ai_sequence_debug.GetBool() == true && (m_debugOverlays & OVERLAY_NPC_SELECTED_BIT))
	{
		DevMsg("ResetSequence : %s: %s -> %s\n", GetClassname(), GetSequenceName(GetSequence()), GetSequenceName(nSequence));
	}
	
	if ( !SequenceLoops() )
	{
		SetCycle( 0 );
	}

	// Tracker 17868:  If the sequence number didn't actually change, but you call resetsequence info, it changes
	//  the newsequenceparity bit which causes the client to call m_flCycle.Reset() which causes a very slight 
	//  discontinuity in looping animations as they reset around to cycle 0.0.  This was causing the parentattached
	//  helmet on barney to hitch every time barney's idle cycled back around to its start.
	bool changed = nSequence != GetSequence() ? true : false;

	SetSequence( nSequence );
	if ( changed || !SequenceLoops() )
	{
		ResetSequenceInfo();
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseAnimating::InputIgnite( inputdata_t &inputdata )
{
	Ignite( 30, false, 0.0f, true );
}

BEGIN_PREDICTION_DATA_NO_BASE( CBaseAnimating )
END_PREDICTION_DATA()
