//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "usercmd.h"
#include "igamesystem.h"
#include "ilagcompensationmanager.h"
#include "inetchannelinfo.h"
#include "utlfixedlinkedlist.h"
#include "tier0/vprof.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define LC_NONE				0
#define LC_ALIVE			(1<<0)

#define LC_ORIGIN_CHANGED	(1<<8)
#define LC_ANGLES_CHANGED	(1<<9)
#define LC_SIZE_CHANGED		(1<<10)

#define LAG_COMPENSATION_TELEPORTED_DISTANCE_SQR ( 64.0f * 64.0f )
#define LAG_COMPENSATION_EPS_SQR ( 0.1f * 0.1f )
// Allow 4 units of error ( about 1 / 8 bbox width )
#define LAG_COMPENSATION_ERROR_EPS_SQR ( 4.0f * 4.0f )

ConVar sv_unlag( "sv_unlag", "1", 0, "Enables player lag compensation" );
ConVar sv_maxunlag( "sv_maxunlag", "1.0", 0, "Maximum lag compensation in seconds", true, 0.0f, true, 1.0f );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
struct LagRecord
{
public:
	LagRecord()
	{
		m_fFlags = 0;
		m_vecOrigin.Init();
		m_vecAngles.Init();
		m_vecMins.Init();
		m_vecMaxs.Init();
		m_flSimulationTime = -1;
	}

	LagRecord( const LagRecord& src )
	{
		m_fFlags = src.m_fFlags;
		m_vecOrigin = src.m_vecOrigin;
		m_vecAngles = src.m_vecAngles;
		m_vecMins = src.m_vecMins;
		m_vecMaxs = src.m_vecMaxs;
		m_flSimulationTime = src.m_flSimulationTime;
	}

	// Did player die this frame
	int						m_fFlags;

	// Player position, orientation and bbox
	Vector					m_vecOrigin;
	QAngle					m_vecAngles;
	Vector					m_vecMins;
	Vector					m_vecMaxs;

	float					m_flSimulationTime;	
	
	// Fixme, do we care about animation frame?
	// float				m_flFrame;
	// int					m_nSequence;
};


//
// Try to take the player from his current origin to vWantedPos.
// If it can't get there, leave the player where he is.
// 
float g_flFractionScale = 0.95;
static void RestorePlayerTo( CBasePlayer *pPlayer, const Vector &vWantedPos )
{
	// Try to move to the wanted position from our current position.
	trace_t tr;
	UTIL_TraceEntity( pPlayer, vWantedPos, vWantedPos, MASK_PLAYERSOLID, &tr );
	if ( tr.startsolid || tr.allsolid )
	{
		UTIL_TraceEntity( pPlayer, pPlayer->GetLocalOrigin(), vWantedPos, MASK_PLAYERSOLID, &tr );
		if ( tr.startsolid || tr.allsolid )
		{
			// In this case, the guy got stuck back wherever we lag compensated him to. Nasty.
		}
		else
		{
			// We can get to a valid place, but not all the way back to where we were.
			Vector vPos;
			VectorLerp( pPlayer->GetLocalOrigin(), vWantedPos, tr.fraction * g_flFractionScale, vPos );
			UTIL_SetOrigin( pPlayer, vPos, true );
		}
	}
	else
	{
		// Cool, the player can go back to whence he came.
		UTIL_SetOrigin( pPlayer, tr.endpos, true );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CLagCompensationManager : public CAutoGameSystem, public ILagCompensationManager
{
public:
	// IServerSystem stuff
	virtual void Shutdown()
	{
		ClearHistory();
	}

	virtual void LevelShutdownPostEntity()
	{
		ClearHistory();
	}

	// called after entities think
	virtual void FrameUpdatePostEntityThink();

	// ILagCompensationManager stuff

	// Called during player movement to set up/restore after lag compensation
	void			StartLagCompensation( CBasePlayer *player, CUserCmd *cmd );
	void			FinishLagCompensation( CBasePlayer *player );

private:
	void			BacktrackPlayer( CBasePlayer *player, float flTargetTime );

	void ClearHistory()
	{
		for ( int i=0; i<MAX_PLAYERS; i++ )
			m_PlayerTrack[i].Purge();
	}

	// keep a list of lag records for each player
	CUtlFixedLinkedList< LagRecord >	m_PlayerTrack[ MAX_PLAYERS ];

	// Scratchpad for determining what needs to be restored
	CBitVec<MAX_PLAYERS>	m_RestorePlayer;
	bool					m_bNeedToRestore;
	
	LagRecord				m_RestoreData[ MAX_PLAYERS ];	// player data before we moved him back
	LagRecord				m_ChangeData[ MAX_PLAYERS ];	// player data where we moved him back
};

static CLagCompensationManager g_LagCompensationManager;
ILagCompensationManager *lagcompensation = &g_LagCompensationManager;


//-----------------------------------------------------------------------------
// Purpose: Called once per frame after all entities have had a chance to think
//-----------------------------------------------------------------------------
void CLagCompensationManager::FrameUpdatePostEntityThink()
{
	if ( (gpGlobals->maxClients <= 1) || !sv_unlag.GetBool() )
	{
		ClearHistory();
		return;
	}
	
	VPROF_BUDGET( "FrameUpdatePostEntityThink", "CLagCompensationManager" );

	// remove all records before that time:
	int flDeadtime = gpGlobals->curtime - sv_maxunlag.GetFloat();

	// Iterate all active players
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );

		CUtlFixedLinkedList< LagRecord > *track = &m_PlayerTrack[i-1];

		if ( !pPlayer )
		{
			track->RemoveAll();
			continue;
		}

		Assert( track->Count() < 1000 ); // insanity check

		// remove tail records that are too old
		int tailIndex = track->Tail();
		while ( track->IsValidIndex( tailIndex ) )
		{
			LagRecord &tail = track->Element( tailIndex );

			// if tail is within limits, stop
			if ( tail.m_flSimulationTime >= flDeadtime )
				break;
			
			// remove tail, get new tail
			track->Remove( tailIndex );
			tailIndex = track->Tail();
		}

		// check if head has same simulation time
		if ( track->Count() > 0 )
		{
			LagRecord &head = track->Element( track->Head() );

			// check if player changed simulation time since last time updated
			if ( head.m_flSimulationTime >= pPlayer->GetSimulationTime() )
				continue; // don't add new entry for same or older time
		}

		// add new record to player track
		LagRecord &record = track->Element( track->AddToHead() );

		record.m_fFlags = 0;
		if ( pPlayer->IsAlive() )
		{
			record.m_fFlags |= LC_ALIVE;
		}

		record.m_flSimulationTime	= pPlayer->GetSimulationTime();
		record.m_vecAngles			= pPlayer->GetLocalAngles();
		record.m_vecOrigin			= pPlayer->GetLocalOrigin();
		record.m_vecMaxs			= pPlayer->WorldAlignMaxs();
		record.m_vecMins			= pPlayer->WorldAlignMins();
	}
}

// Called during player movement to set up/restore after lag compensation
void CLagCompensationManager::StartLagCompensation( CBasePlayer *player, CUserCmd *cmd )
{
	// Assume no players need to be restored
	m_RestorePlayer.ClearAll();
	m_bNeedToRestore = false;
	
	if ( !player->m_bLagCompensation		// Player not wanting lag compensation
		 || (gpGlobals->maxClients <= 1)	// no lag compensation in sngle player
		 || !sv_unlag.GetBool()				// disabled by server admin
		 || player->IsBot() 				// not for bots
		 || player->IsObserver()			// not for spectators
		)
		return;

	// NOTE: Put this here so that it won't show up in single player mode.
	VPROF_BUDGET( "StartLagCompensation", "CLagCompensationManager" );
	Q_memset( m_RestoreData, 0, sizeof( m_RestoreData ) );
	Q_memset( m_ChangeData, 0, sizeof( m_ChangeData ) );

	// Get true latency

	// correct is the amout of time we have to correct game time
	float correct = 0.0f;

	INetChannelInfo *nci = engine->GetPlayerNetInfo( player->entindex() ); 

	if ( nci )
	{
		// add network latency
		correct+= nci->GetLatency( FLOW_OUTGOING );
	}

	// calc number of view interpolation ticks - 1
	int lerpTicks = TIME_TO_TICKS( player->m_fLerpTime );

	// add view interpolation latency see C_BaseEntity::GetInterpolationAmount()
	correct += TICKS_TO_TIME( lerpTicks );
	
	// check bouns [0,sv_maxunlag]
	correct = clamp( correct, 0.0f, sv_maxunlag.GetFloat() );

	// correct tick send by player 
	int targettick = cmd->tick_count - lerpTicks;

	// calc difference between tick send by player and our latency based tick
	float deltaTime =  correct - TICKS_TO_TIME(gpGlobals->tickcount - targettick);

	if ( fabs( deltaTime ) > 0.2f )
	{
		// difference between cmd time and latency is too big > 200ms, use time correction based on latency
		// DevMsg("StartLagCompensation: delta too big (%.3f)\n", deltaTime );
		targettick = gpGlobals->tickcount - TIME_TO_TICKS( correct );
	}
	
	// Iterate all active players
	const CBitVec<MAX_EDICTS> *pEntityTranmsitBits = engine->GetEntityTransmitBitsForClient( player->entindex() - 1 );
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );

		if ( !pPlayer )
		{
			continue;
		}

		// Don't lag compensate yourself you loser...
		if ( player == pPlayer )
		{
			continue;
		}

		// Custom checks for if things should lag compensate (based on things like what team the player is on).
		if ( !player->WantsLagCompensationOnEntity( pPlayer, cmd, pEntityTranmsitBits ) )
			continue;

		// Move other player back in time
		BacktrackPlayer( pPlayer, TICKS_TO_TIME( targettick ) );
	}
}

void CLagCompensationManager::BacktrackPlayer( CBasePlayer *pPlayer, float flTargetTime )
{
	Vector org, mins, maxs;
	QAngle ang;

	int pl_index = pPlayer->entindex() - 1;

	// get track history of this player
	CUtlFixedLinkedList< LagRecord > *track = &m_PlayerTrack[ pl_index ];

	// check if we have at leat one entry
	if ( track->Count() <= 0 )
		return;

	int curr = track->Head();

	LagRecord *prevRecord = NULL;
	LagRecord *record = NULL;
	
	// Walk context looking for any invalidating event
	while( track->IsValidIndex(curr) )
	{
		// remeber last record
		prevRecord = record;

		// get next record
		record = &track->Element( curr );

		if ( !(record->m_fFlags & LC_ALIVE) )
		{
			// player most be alive, lost track
			return;
		}

		if ( prevRecord )
		{
			Assert( prevRecord->m_flSimulationTime > record->m_flSimulationTime );

			Vector delta = record->m_vecOrigin - prevRecord->m_vecOrigin;
			if ( delta.LengthSqr() > LAG_COMPENSATION_TELEPORTED_DISTANCE_SQR )
			{
				// lost track, too much difference
				return; 
			}
		}

		// did we found a context smaller then target time ?
		if ( record->m_flSimulationTime <= flTargetTime )
			break; // hurra, stop

		// go one step back
		curr = track->Next( curr );
	}

	Assert( record );

	if ( !record )
		return; // that should never happen

	if ( record->m_flSimulationTime > flTargetTime && prevRecord && prevRecord != record
		 && prevRecord->m_flSimulationTime != record->m_flSimulationTime )
	{
		// we didn't found the extact time but have a valid previous record
		// so interpolate between these two records;

		Assert( prevRecord->m_flSimulationTime > record->m_flSimulationTime );
		Assert( flTargetTime > record->m_flSimulationTime &&
			    flTargetTime < prevRecord->m_flSimulationTime );

		// calc fraction between both records
		float frac = ( flTargetTime - record->m_flSimulationTime ) / 
			( prevRecord->m_flSimulationTime - record->m_flSimulationTime );

		Assert( frac > 0 && frac < 1 ); // should never extrapolate

		ang  = Lerp( frac, prevRecord->m_vecAngles, record->m_vecAngles );
		org  = Lerp( frac, prevRecord->m_vecOrigin, record->m_vecOrigin );
		mins = Lerp( frac, prevRecord->m_vecMins, record->m_vecMins );
		maxs = Lerp( frac, prevRecord->m_vecMaxs, record->m_vecMaxs );
	}
	else
	{
		// we found the exact record or no other record to interpolate with
		// just copy these values since they are the best we have
		ang  = record->m_vecAngles;
		org  = record->m_vecOrigin;
		mins = record->m_vecMins;
		maxs = record->m_vecMaxs;
	}
	
	// See if this represents a change for the player
	int flags = 0;
	LagRecord *restore = &m_RestoreData[ pl_index ];
	LagRecord *change  = &m_ChangeData[ pl_index ];

	QAngle angdiff = pPlayer->GetLocalAngles() - ang;

	// Always remember the pristine simulation time in case we need to restore it.
	restore->m_flSimulationTime = pPlayer->GetSimulationTime();

	if ( angdiff.LengthSqr() > LAG_COMPENSATION_EPS_SQR )
	{
		flags |= LC_ANGLES_CHANGED;
		restore->m_vecAngles = pPlayer->GetLocalAngles();
		pPlayer->SetLocalAngles( ang );
		change->m_vecAngles = ang;
	}

	// Use absoluate equality here
	if ( ( mins != pPlayer->WorldAlignMins() ) ||
		 ( maxs != pPlayer->WorldAlignMaxs() ) )
	{
		flags |= LC_SIZE_CHANGED;
		restore->m_vecMins = pPlayer->WorldAlignMins() ;
		restore->m_vecMaxs = pPlayer->WorldAlignMaxs();
		pPlayer->SetSize( mins, maxs );
		change->m_vecMins = mins;
		change->m_vecMaxs = maxs;
	}

	Vector orgdiff = pPlayer->GetLocalOrigin() - org;

	// Note, do origin at end since it causes a relink into the k/d tree
	if ( orgdiff.LengthSqr() > LAG_COMPENSATION_EPS_SQR )
	{
		flags |= LC_ORIGIN_CHANGED;
		restore->m_vecOrigin = pPlayer->GetLocalOrigin();
		pPlayer->SetLocalOrigin( org );
		change->m_vecOrigin = org;
	}

	if ( !flags )
		return; // we didn't change anything
	
	// pPlayer->DrawServerHitboxes();
	// NDebugOverlay::EntityBounds( pPlayer, 255, 0, 0, 64, 0.1 );

	m_RestorePlayer.Set( pl_index ); //remember that we changed this player
	m_bNeedToRestore = true;  // we changed at least one player
	restore->m_fFlags = flags; // we need to restore these flags
	change->m_fFlags = flags; // we have changed these flags
}


void CLagCompensationManager::FinishLagCompensation( CBasePlayer *player )
{
	VPROF_BUDGET_FLAGS( "FinishLagCompensation", "CLagCompensationManager", BUDGETFLAG_CLIENT|BUDGETFLAG_SERVER );
	if ( !m_bNeedToRestore )
		return; // no player was changed at all

	// Iterate all active players
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		int pl_index = i - 1;
		
		if ( !m_RestorePlayer.Get( pl_index ) )
		{
			// player wasn't changed by lag compensation
			continue;
		}

		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( !pPlayer )
		{
			continue;
		}

		LagRecord *restore = &m_RestoreData[ pl_index ];
		LagRecord *change  = &m_ChangeData[ pl_index ];

		bool restoreSimulationTime = false;

		if ( restore->m_fFlags & LC_SIZE_CHANGED )
		{
			restoreSimulationTime = true;
	
			// see if simulation made any changes, if no, then do the restore, otherwise,
			//  leave new values in
			if ( pPlayer->WorldAlignMins() == change->m_vecMins && 
				 pPlayer->WorldAlignMaxs() == change->m_vecMaxs )
			{
				// Restore it
				pPlayer->SetSize( restore->m_vecMins, restore->m_vecMaxs );
			}
		}

		if ( restore->m_fFlags & LC_ANGLES_CHANGED )
		{		   
			restoreSimulationTime = true;

			if ( pPlayer->GetLocalAngles() == change->m_vecAngles )
			{
				pPlayer->SetLocalAngles( restore->m_vecAngles );
			}
		}

		if ( restore->m_fFlags & LC_ORIGIN_CHANGED )
		{
			restoreSimulationTime = true;

			// Okay, let's see if we can do something reasonable with the change
			Vector delta = pPlayer->GetLocalOrigin() - change->m_vecOrigin;
			
			// If it moved really far, just leave the player in the new spot!!!
			if ( delta.LengthSqr() < LAG_COMPENSATION_TELEPORTED_DISTANCE_SQR )
			{
				RestorePlayerTo( pPlayer, restore->m_vecOrigin + delta );
			}
		}


		if ( restoreSimulationTime )
		{
			pPlayer->SetSimulationTime( restore->m_flSimulationTime );
		}
	}
}


