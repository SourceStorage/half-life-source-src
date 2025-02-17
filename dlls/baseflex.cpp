//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "animation.h"
#include "baseflex.h"
#include "filesystem.h"
#include "studio.h"
#include "choreoevent.h"
#include "choreoscene.h"
#include "choreoactor.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "vstdlib/strtools.h"
#include "KeyValues.h"
#include "ai_basenpc.h"
#include "ai_navigator.h"
#include "ai_moveprobe.h"
#include "sceneentity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar scene_allowoverrides( "scene_allowoverrides", "1", 0, "When playing back a choreographed scene, allow per-model expression overrides." );
static ConVar scene_showlook( "scene_showlook", "0", FCVAR_ARCHIVE, "When playing back, show the directions of look events." );
static ConVar scene_showmoveto( "scene_showmoveto", "0", FCVAR_ARCHIVE, "When moving, show the end location." );

// static ConVar scene_checktagposition( "scene_checktagposition", "0", FCVAR_ARCHIVE, "When playing back a choreographed scene, check the current position of the tags relative to where they were authored." );

// Fake layer # to force HandleProcessSceneEvent to actually allocate the layer during npc think time instead of in between.
#define REQUEST_DEFERRED_LAYER_ALLOCATION	-2

// ---------------------------------------------------------------------
//
// CBaseFlex -- physically simulated brush rectangular solid
//
// ---------------------------------------------------------------------


// SendTable stuff.
IMPLEMENT_SERVERCLASS_ST(CBaseFlex, DT_BaseFlex)
	SendPropArray3	(SENDINFO_ARRAY3(m_flexWeight), SendPropFloat(SENDINFO_ARRAY(m_flexWeight), 12, SPROP_ROUNDDOWN, 0.0f, 1.0f ) ),
	SendPropInt		(SENDINFO(m_blinktoggle), 1, SPROP_UNSIGNED ),
	SendPropVector	(SENDINFO(m_viewtarget), -1, SPROP_COORD),
#ifdef HL2_DLL
	SendPropFloat	( SENDINFO_VECTORELEM(m_vecViewOffset, 0), 8, 0,	-32.0, 32.0f),
	SendPropFloat	( SENDINFO_VECTORELEM(m_vecViewOffset, 1), 8, 0,	-32.0, 32.0f),
	SendPropFloat	( SENDINFO_VECTORELEM(m_vecViewOffset, 2), 16, 0,	-64.0f, 128.0f),
#endif
	
END_SEND_TABLE()


BEGIN_DATADESC( CBaseFlex )

	//						m_blinktoggle
	DEFINE_ARRAY( m_flexWeight, FIELD_FLOAT, 64 ),
	DEFINE_FIELD( m_viewtarget, FIELD_POSITION_VECTOR ),
	//						m_SceneEvents
	//						m_FileList
	DEFINE_FIELD( m_flAllowResponsesEndTime, FIELD_TIME ),
	//						m_ActiveChoreoScenes
	// DEFINE_FIELD( m_LocalToGlobal, CUtlRBTree < FS_LocalToGlobal_t , unsigned short > ),
	//						m_bUpdateLayerPriorities
	DEFINE_FIELD( m_flLastFlexAnimationTime, FIELD_TIME ),
END_DATADESC()



LINK_ENTITY_TO_CLASS( funCBaseFlex, CBaseFlex ); // meaningless independant class!!

BEGIN_PREDICTION_DATA( CBaseFlex )
END_PREDICTION_DATA()

CBaseFlex::CBaseFlex( void ) : 
	m_LocalToGlobal( 0, 0, FlexSettingLessFunc )
{
#ifdef _DEBUG
	// default constructor sets the viewtarget to NAN
	m_viewtarget.Init();
#endif
	m_bUpdateLayerPriorities = true;
	m_flLastFlexAnimationTime = 0.0;
}

CBaseFlex::~CBaseFlex( void )
{
	m_LocalToGlobal.RemoveAll();
	Assert( m_SceneEvents.Count() == 0 );
}

void CBaseFlex::SetModel( const char *szModelName )
{
	BaseClass::SetModel( szModelName );

	for (int i = 0; i < GetNumFlexControllers(); i++)
	{
		SetFlexWeight( i, 0.0 );
	}
}


void CBaseFlex::SetViewtarget( const Vector &viewtarget )
{
	m_viewtarget = viewtarget;	// bah
}

void CBaseFlex::SetFlexWeight( int index, float value )
{
	if (index >= 0 && index < GetNumFlexControllers())
	{
		studiohdr_t *pstudiohdr = GetModelPtr( );
		if (! pstudiohdr)
			return;

		mstudioflexcontroller_t *pflexcontroller = pstudiohdr->pFlexcontroller( index );

		if (pflexcontroller->max != pflexcontroller->min)
		{
			value = (value - pflexcontroller->min) / (pflexcontroller->max - pflexcontroller->min);
			value = clamp( value, 0.0, 1.0 );
		}

		m_flexWeight.Set( index, value );
	}
}

float CBaseFlex::GetFlexWeight( int index )
{
	if (index >= 0 && index < GetNumFlexControllers())
	{
		studiohdr_t *pstudiohdr = GetModelPtr( );
		if (! pstudiohdr)
			return 0;

		mstudioflexcontroller_t *pflexcontroller = pstudiohdr->pFlexcontroller( index );

		if (pflexcontroller->max != pflexcontroller->min)
		{
			return m_flexWeight[index] * (pflexcontroller->max - pflexcontroller->min) + pflexcontroller->min;
		}
				
		return m_flexWeight[index];
	}
	return 0.0;
}

int CBaseFlex::FindFlexController( const char *szName )
{
	for (int i = 0; i < GetNumFlexControllers(); i++)
	{
		if (stricmp( GetFlexControllerName( i ), szName ) == 0)
		{
			return i;
		}
	}

	// AssertMsg( 0, UTIL_VarArgs( "flexcontroller %s couldn't be mapped!!!\n", szName ) );
	return 0;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseFlex::StartChoreoScene( CChoreoScene *scene )
{
	if ( m_ActiveChoreoScenes.Find( scene ) != m_ActiveChoreoScenes.InvalidIndex() )
	{
		return;
	}

	m_ActiveChoreoScenes.AddToTail( scene );
	m_bUpdateLayerPriorities = true;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseFlex::RemoveChoreoScene( CChoreoScene *scene )
{
	// Assert( m_ActiveChoreoScenes.Find( scene ) != m_ActiveChoreoScenes.InvalidIndex() );

	m_ActiveChoreoScenes.FindAndRemove( scene );
	m_bUpdateLayerPriorities = true;
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

int CBaseFlex::GetScenePriority( CChoreoScene *scene )
{
	int iPriority = 0;
	int c = m_ActiveChoreoScenes.Count();
	// count number of channels in scenes older than current
	for ( int i = 0; i < c; i++ )
	{
		CChoreoScene *pScene = m_ActiveChoreoScenes[ i ];
		if ( !pScene )
		{
			continue;
		}

		if ( pScene == scene )
		{
			break;
		}

		iPriority += pScene->GetNumChannels( );
	}
	return iPriority;
}




//-----------------------------------------------------------------------------
// Purpose: Remove all active SceneEvents
//-----------------------------------------------------------------------------
void CBaseFlex::ClearSceneEvents( CChoreoScene *scene, bool canceled )
{
	if ( !scene )
	{
		m_SceneEvents.RemoveAll();
		return;
	}

	for ( int i = m_SceneEvents.Count() - 1; i >= 0; i-- )
	{
		CSceneEventInfo *info = &m_SceneEvents[ i ];

		Assert( info );
		Assert( info->m_pScene );
		Assert( info->m_pEvent );

		if ( info->m_pScene != scene )
			continue;

		if ( !ClearSceneEvent( info, false, canceled ))
		{
			// unknown expression to clear!!
			Assert( 0 );
		}

		// Free this slot
		info->m_pEvent		= NULL;
		info->m_pScene		= NULL;
		info->m_bStarted	= false;

		m_SceneEvents.Remove( i );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Stop specifics of expression
//-----------------------------------------------------------------------------

bool CBaseFlex::ClearSceneEvent( CSceneEventInfo *info, bool fastKill, bool canceled )
{
	Assert( info );
	Assert( info->m_pScene );
	Assert( info->m_pEvent );

	// FIXME: this code looks duplicated
	switch ( info->m_pEvent->GetType() )
	{
	case CChoreoEvent::GESTURE:
	case CChoreoEvent::SEQUENCE: 
		{
			if (info->m_iLayer >= 0)
			{
				if ( fastKill )
				{
					FastRemoveLayer( info->m_iLayer );
				}
				else if (info->m_pEvent->GetType() == CChoreoEvent::GESTURE)
				{
					if (canceled)
					{
						// remove slower if interrupted
						RemoveLayer( info->m_iLayer, 0.5 );
					}
					else
					{
						RemoveLayer( info->m_iLayer, 0.1 );
					}
				}
				else
				{
					RemoveLayer( info->m_iLayer, 0.3 );
				}
			}
		}
		return true;

	case CChoreoEvent::MOVETO: 
		{
			CAI_BaseNPC *myNpc = MyNPCPointer( );
			if (!myNpc)
				return true;

			// cancel moveto if it's distance based, of if the event was part of a canceled vcd
			if (IsMoving() && (canceled || info->m_pEvent->GetDistanceToTarget() > 0.0))
			{
				myNpc->GetNavigator()->StopMoving();		// Stop moving
			}
		}
		return true;
	case CChoreoEvent::FACE: 
	case CChoreoEvent::FLEXANIMATION:
	case CChoreoEvent::EXPRESSION:
	case CChoreoEvent::LOOKAT:
	case CChoreoEvent::GENERIC:
		{
			// no special rules
		}
		return true;
	case CChoreoEvent::SPEAK:
		{
			// Tracker 15420:  Issue stopsound if we need to cut this short...
			if ( canceled )
			{
				StopSound( info->m_pEvent->GetParameters() );
			}
		}
		return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Add string indexed scene/expression/duration to list of active SceneEvents
// Input  : scenefile - 
//			expression - 
//			duration - 
//-----------------------------------------------------------------------------
void CBaseFlex::AddSceneEvent( CChoreoScene *scene, CChoreoEvent *event, CBaseEntity *pTarget )
{
	if ( !scene || !event )
	{
		Msg( "CBaseFlex::AddSceneEvent:  scene or event was NULL!!!\n" );
		return;
	}

	CChoreoActor *actor = event->GetActor();
	if ( !actor )
	{
		Msg( "CBaseFlex::AddSceneEvent:  event->GetActor() was NULL!!!\n" );
		return;
	}


	CSceneEventInfo info;

	memset( (void *)&info, 0, sizeof( info ) );

	info.m_pEvent		= event;
	info.m_pScene		= scene;
	info.m_hTarget		= pTarget;
	info.m_bStarted	= false;

	if (StartSceneEvent( &info, scene, event, actor, pTarget ))
	{
		m_SceneEvents.AddToTail( info );
	}
	else
	{
		scene->SceneMsg( "CBaseFlex::AddSceneEvent:  event failed\n" );
		// Assert( 0 ); // expression failed to start
	}
}


//-----------------------------------------------------------------------------
// Starting various expression types 
//-----------------------------------------------------------------------------

bool CBaseFlex::RequestStartSequenceSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget )
{
	info->m_nSequence = LookupSequence( event->GetParameters() );

	// make sure sequence exists
	if (info->m_nSequence < 0)
	{
		Warning( "CSceneEntity %s :\"%s\" unable to find sequence \"%s\"\n", STRING(GetEntityName()), actor->GetName(), event->GetParameters() );
		return false;
	}

	// This is a bit of a hack, but we need to defer the actual allocation until Process which will sync the layer allocation
	//  to the NPCs think/m_flAnimTime instead of some arbitrary tick
	info->m_iLayer = REQUEST_DEFERRED_LAYER_ALLOCATION;
	info->m_pActor = actor;
	return true;
}

bool CBaseFlex::RequestStartGestureSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget )
{
	info->m_nSequence = LookupSequence( event->GetParameters() );

	// make sure sequence exists
	if (info->m_nSequence < 0)
	{
		Warning( "CSceneEntity %s :\"%s\" unable to find gesture \"%s\"\n", STRING(GetEntityName()), actor->GetName(), event->GetParameters() );
		return false;
	}

	// This is a bit of a hack, but we need to defer the actual allocation until Process which will sync the layer allocation
	//  to the NPCs think/m_flAnimTime instead of some arbitrary tick
	info->m_iLayer = REQUEST_DEFERRED_LAYER_ALLOCATION;
	info->m_pActor = actor;
	return true;
}

bool CBaseFlex::HandleStartSequenceSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor )
{
	Assert( info->m_iLayer == REQUEST_DEFERRED_LAYER_ALLOCATION );

	info->m_nSequence = LookupSequence( event->GetParameters() );
	info->m_iLayer = -1;

	if (info->m_nSequence < 0)
	{
		Warning( "CSceneEntity %s :\"%s\" unable to find sequence \"%s\"\n", STRING(GetEntityName()), actor->GetName(), event->GetParameters() );
		return false;
	}
	
	if (!EnterSceneSequence())
	{
		// this has failed to start
		return false;
	}

	info->m_iPriority = actor->FindChannelIndex( event->GetChannel() );
	info->m_iLayer = AddLayeredSequence( info->m_nSequence, info->m_iPriority + GetScenePriority( scene ) );
	SetLayerNoRestore( info->m_iLayer, true );
	SetLayerWeight( info->m_iLayer, 0.0 );

	bool looping = ((GetSequenceFlags( info->m_nSequence ) & STUDIO_LOOPING) != 0);
	if (!looping)
	{
		// figure out the animtime when this was frame 0
		float dt =  scene->GetTime() - event->GetStartTime();
		float seq_duration = SequenceDuration( info->m_nSequence );
		float flCycle = dt / seq_duration;
		flCycle = flCycle - (int)flCycle; // loop
		SetLayerCycle( info->m_iLayer, flCycle, flCycle );

		SetLayerPlaybackRate( info->m_iLayer, 0.0 );
	}
	else
	{
		SetLayerPlaybackRate( info->m_iLayer, 1.0 );
	}

	if (IsMoving())
	{
		info->m_flWeight = 0.0;
	}
	else
	{
		info->m_flWeight = 1.0;
	}

	return true;
}

bool CBaseFlex::HandleStartGestureSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor )
{
	Assert( info->m_iLayer == REQUEST_DEFERRED_LAYER_ALLOCATION );

	info->m_nSequence = LookupSequence( event->GetParameters() );
	info->m_iLayer = -1;

	if (info->m_nSequence < 0)
	{
		Warning( "CSceneEntity %s :\"%s\" unable to find gesture \"%s\"\n", STRING(GetEntityName()), actor->GetName(), event->GetParameters() );
		return false;
	}

	// FIXME: this seems like way too much code
	info->m_bIsPosture = false;
	KeyValues *seqKeyValues = GetSequenceKeyValues( info->m_nSequence );
	if (seqKeyValues)
	{
		// Do we have a build point section?
		KeyValues *pkvAllFaceposer = seqKeyValues->FindKey("faceposer");
		if ( pkvAllFaceposer )
		{
			KeyValues *pkvType = pkvAllFaceposer->FindKey("type");

			if (pkvType)
			{
				info->m_bIsPosture = (stricmp( pkvType->GetString(), "posture" ) == 0) ? true : false;
			}
		}

		// FIXME: fixup tags that should be set as "linear", should be done in faceposer
		char szStartLoop[CEventAbsoluteTag::MAX_EVENTTAG_LENGTH] = { "loop" };
		char szEndLoop[CEventAbsoluteTag::MAX_EVENTTAG_LENGTH] = { "end" };

		// check in the tag indexes
		KeyValues *pkvFaceposer;
		for ( pkvFaceposer = pkvAllFaceposer->GetFirstSubKey(); pkvFaceposer; pkvFaceposer = pkvFaceposer->GetNextKey() )
		{
			if (!stricmp( pkvFaceposer->GetName(), "startloop" ))
			{
				strcpy( szStartLoop, pkvFaceposer->GetString() );
			}
			else if (!stricmp( pkvFaceposer->GetName(), "endloop" ))
			{
				strcpy( szEndLoop, pkvFaceposer->GetString() );
			}
		}

		CEventAbsoluteTag *ptag;
		ptag = event->FindAbsoluteTag( CChoreoEvent::ORIGINAL, szStartLoop );
		if (ptag)
		{
			ptag->SetLinear( true );
		}
		ptag = event->FindAbsoluteTag( CChoreoEvent::PLAYBACK, szStartLoop );
		if (ptag)
		{
			ptag->SetLinear( true );
		}
		ptag = event->FindAbsoluteTag( CChoreoEvent::ORIGINAL, szEndLoop );
		if (ptag)
		{
			ptag->SetLinear( true );
		}
		ptag = event->FindAbsoluteTag( CChoreoEvent::PLAYBACK, szEndLoop );
		if (ptag)
		{
			ptag->SetLinear( true );
		}

		if ( pkvAllFaceposer )
		{
			studiohdr_t *pstudiohdr = GetModelPtr();
			
			mstudioseqdesc_t &seqdesc = pstudiohdr->pSeqdesc( info->m_nSequence );
			mstudioanimdesc_t &animdesc = pstudiohdr->pAnimdesc( pstudiohdr->iRelativeAnim( info->m_nSequence, seqdesc.anim(0,0) ) );

			// check in the tag indexes
			KeyValues *pkvFaceposer;
			for ( pkvFaceposer = pkvAllFaceposer->GetFirstSubKey(); pkvFaceposer; pkvFaceposer = pkvFaceposer->GetNextKey() )
			{
				if (!stricmp( pkvFaceposer->GetName(), "tags" ))
				{
					KeyValues *pkvTags;
					for ( pkvTags = pkvFaceposer->GetFirstSubKey(); pkvTags; pkvTags = pkvTags->GetNextKey() )
					{
						int maxFrame = animdesc.numframes - 2; // FIXME: this is off by one!

						if ( maxFrame > 0)
						{
							float percentage = (float)pkvTags->GetInt() / maxFrame;

							CEventAbsoluteTag *ptag = event->FindAbsoluteTag( CChoreoEvent::ORIGINAL, pkvTags->GetName() );
							if (ptag)
							{
								if (fabs(ptag->GetPercentage() - percentage) > 0.05)
								{
									DevWarning("%s repositioned tag: %s : %.3f -> %.3f (%s:%s:%s)\n", scene->GetFilename(), pkvTags->GetName(), ptag->GetPercentage(), percentage, scene->GetFilename(), actor->GetName(), event->GetParameters() );
									// reposition tag
									ptag->SetPercentage( percentage );
								}
							}
						}
					}
				}
			}

			if (!event->VerifyTagOrder())
			{
				DevWarning("out of order tags : %s : (%s:%s:%s)\n", scene->GetFilename(), actor->GetName(), event->GetName(), event->GetParameters() );
			}
		}

		seqKeyValues->deleteThis();
	}

	// initialize posture suppression
	// FIXME: move priority of base animation so that layers can be inserted before
	// FIXME: query stopping, post idle layer to figure out correct weight
	//			GetIdleLayerWeight()?
	if (info->m_bIsPosture && IsMoving())
	{
		info->m_flWeight = 0.0;
	}
	else
	{
		info->m_flWeight = 1.0;
	}

	// this happens before StudioFrameAdvance()
	info->m_iPriority = actor->FindChannelIndex( event->GetChannel() );
	info->m_iLayer = AddLayeredSequence( info->m_nSequence, info->m_iPriority + GetScenePriority( scene ) );
	SetLayerNoRestore( info->m_iLayer, true );
	SetLayerDuration( info->m_iLayer, event->GetDuration() );
	SetLayerWeight( info->m_iLayer, 0.0 );

	bool looping = ((GetSequenceFlags( info->m_nSequence ) & STUDIO_LOOPING) != 0);
	if ( looping )
	{
		DevMsg( 1, "vcd error, gesture %s of model %s is marked as STUDIO_LOOPING!\n", 
			event->GetParameters(), GetModelName() );
	}

	SetLayerLooping( info->m_iLayer, false ); // force to not loop

	float duration = event->GetDuration( );

	// figure out the animtime when this was frame 0
	float flEventCycle = (scene->GetTime() - event->GetStartTime()) / duration;
	float flCycle = event->GetOriginalPercentageFromPlaybackPercentage( flEventCycle );
	SetLayerCycle( info->m_iLayer, flCycle, 0.0 );
	SetLayerPlaybackRate( info->m_iLayer, 0.0 );

	return true;
}

bool CBaseFlex::StartFacingSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget )
{
	if (pTarget)
	{
		info->m_bIsMoving = false;
		return true;
	}
	return false;
}

bool CBaseFlex::StartMoveToSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget )
{
	if (pTarget)
	{
		info->m_bIsMoving = false;
		info->m_bHasArrived = false;
		CAI_BaseNPC *myNpc = MyNPCPointer( );
		if (!myNpc)
		{
			return false;
		}

		if (EnterSceneSequence( true ))
		{
			myNpc->AddSceneLock( min( 2.0, event->GetEndTime() - scene->GetTime() + 0.2 ) );
		}

		// if they're already moving, stop them
		if (myNpc->IsMoving())
		{
			myNpc->GetNavigator()->StopMoving( );
		}

		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseFlex::StartSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event, CChoreoActor *actor, CBaseEntity *pTarget )
{
	switch ( event->GetType() )
	{
	case CChoreoEvent::SEQUENCE: 
		return RequestStartSequenceSceneEvent( info, scene, event, actor, pTarget );

	case CChoreoEvent::GESTURE:
		return RequestStartGestureSceneEvent( info, scene, event, actor, pTarget );

	case CChoreoEvent::FACE: 
		return StartFacingSceneEvent( info, scene, event, actor, pTarget );

	// FIXME: move this to an CBaseActor
	case CChoreoEvent::MOVETO: 
		return StartMoveToSceneEvent( info, scene, event, actor, pTarget );

	case CChoreoEvent::LOOKAT:
		info->m_hTarget = pTarget;
		return true;

	case CChoreoEvent::FLEXANIMATION:
		info->InitWeight( this );
		return true;

	case CChoreoEvent::SPEAK:
		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Remove expression
// Input  : scenefile - 
//			expression - 
//-----------------------------------------------------------------------------
void CBaseFlex::RemoveSceneEvent( CChoreoScene *scene, CChoreoEvent *event, bool fastKill )
{
	Assert( event );

	for ( int i = 0 ; i < m_SceneEvents.Count(); i++ )
	{
		CSceneEventInfo *info = &m_SceneEvents[ i ];

		Assert( info );
		Assert( info->m_pEvent );

		if ( info->m_pScene != scene )
			continue;

		if ( info->m_pEvent != event)
			continue;

		if (ClearSceneEvent( info, fastKill, false ))
		{
			// Free this slot
			info->m_pEvent		= NULL;
			info->m_pScene		= NULL;
			info->m_bStarted	= false;

			m_SceneEvents.Remove( i );
			return;
		}
	}

	// many events refuse to start due to bogus parameters
}

//-----------------------------------------------------------------------------
// Purpose: Checks to see if the event should be considered "completed"
//-----------------------------------------------------------------------------
bool CBaseFlex::CheckSceneEvent( float currenttime, CChoreoScene *scene, CChoreoEvent *event )
{
	for ( int i = 0 ; i < m_SceneEvents.Count(); i++ )
	{
		CSceneEventInfo *info = &m_SceneEvents[ i ];

		Assert( info );
		Assert( info->m_pEvent );

		if ( info->m_pScene != scene )
			continue;

		if ( info->m_pEvent != event)
			continue;
		
		return CheckSceneEventCompletion( info, currenttime, scene, event );
	}
	return true;
}



bool CBaseFlex::CheckSceneEventCompletion( CSceneEventInfo *info, float currenttime, CChoreoScene *scene, CChoreoEvent *event )
{
	switch ( event->GetType() )
	{
	case CChoreoEvent::MOVETO:
		{
			CAI_BaseNPC *npc = MyNPCPointer( );

			if (npc)
			{
				// check movement, check arrival
				if (npc->GetNavigator()->IsGoalActive())
				{
					float preload = event->GetEndTime() - currenttime;
					if (preload < 0)
					{
						//Msg("%.1f: no preload\n", currenttime );
						return false;
					}
					float t = npc->GetTimeToNavGoal();

					// Msg("%.1f: preload (%s:%.1f) %.1f %.1f\n", currenttime, event->GetName(), event->GetEndTime(), preload, t );

					// FIXME: t is zero if no path can be built!

					if (t > 0.0f && t <= preload)
					{
						return true;
					}
					return false;
				}
				else if (info->m_bHasArrived)
				{
					return true;
				}
				return false;		
			}
		}
		break;
	default:
		break;
	}
	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Default implementation
//-----------------------------------------------------------------------------
void CBaseFlex::ProcessSceneEvents( void )
{
	// slowly decay to netural expression
	int i;
	for ( i = 0; i < GetNumFlexControllers(); i++)
	{
		SetFlexWeight( i, GetFlexWeight( i ) * 0.95 );
	}

	// Iterate SceneEvents and look for active slots
	for ( i = 0; i < m_SceneEvents.Count(); i++ )
	{
		CSceneEventInfo *info = &m_SceneEvents[ i ];
		Assert( info );

		// FIXME:  Need a safe handle to m_pEvent in case of memory deletion?
		CChoreoEvent *event = info->m_pEvent;
		Assert( event );

		CChoreoScene *scene = info->m_pScene;
		Assert( scene );

		if (ProcessSceneEvent( info, scene, event ))
		{
			info->m_bStarted = true;
		}
	}

	// any needed layer priorites have now been reset
	m_bUpdateLayerPriorities = false;
}

class CFlexSceneFileManager : CAutoGameSystem
{
public:

	virtual bool Init()
	{
		// Trakcer 16692:  Preload these at startup to avoid hitch first time we try to load them during actual gameplay
		FindSceneFile( NULL, "phonemes" );
		FindSceneFile( NULL, "phonemes_weak" );
		FindSceneFile( NULL, "phonemes_strong" );
#if defined( HL2_DLL )
		FindSceneFile( NULL, "random" );
		FindSceneFile( NULL, "randomAlert" );
#endif
		return true;
	}

	// Tracker 14992:  We used to load 18K of .vfes for every CBaseFlex who lipsynced, but now we only load those files once globally.
	// Note, we could wipe these between levels, but they don't ever load more than the weak/normal/strong phoneme classes that I can tell
	//  so I'll just leave them loaded forever for now
	virtual void Shutdown()
	{
		DeleteSceneFiles();
	}

	//-----------------------------------------------------------------------------
	// Purpose: Sets up translations
	// Input  : *instance - 
	//			*pSettinghdr - 
	// Output : 	void
	//-----------------------------------------------------------------------------
	void EnsureTranslations( CBaseFlex *instance, const flexsettinghdr_t *pSettinghdr )
	{
		// The only time instance is NULL is in Init() above, where we're just loading the .vfe files off of the hard disk.
		if ( instance )
		{
			instance->EnsureTranslations( pSettinghdr );
		}
	}

	const void *FindSceneFile( CBaseFlex *instance, const char *filename )
	{
		// See if it's already loaded
		int i;
		for ( i = 0; i < m_FileList.Size(); i++ )
		{
			CFlexSceneFile *file = m_FileList[ i ];
			if ( file && !stricmp( file->filename, filename ) )
			{
				// Make sure translations (local to global flex controller) are set up for this instance
				EnsureTranslations( instance, ( const flexsettinghdr_t * )file->buffer );
				return file->buffer;
			}
		}
		
		// Load file into memory
		FileHandle_t file = filesystem->Open( UTIL_VarArgs( "expressions/%s.vfe", filename ), "rb" );

		if ( !file )
			return NULL;

		int len = filesystem->Size( file );

		// read the file
		byte *buffer = new unsigned char[ len ];
		Assert( buffer );
		filesystem->Read( buffer, len, file );
		filesystem->Close( file );

		// Create scene entry
		CFlexSceneFile *pfile = new CFlexSceneFile;
		// Remember filename
		Q_strncpy( pfile->filename, filename, sizeof( pfile->filename ) );
		// Remember data pointer
		pfile->buffer = buffer;
		// Add to list
		m_FileList.AddToTail( pfile );

		// Fill in translation table
		EnsureTranslations( instance, ( const flexsettinghdr_t * )pfile->buffer );

		// Return data
		return pfile->buffer;
	}

private:

	void DeleteSceneFiles()
	{
		while ( m_FileList.Size() > 0 )
		{
			CFlexSceneFile *file = m_FileList[ 0 ];
			m_FileList.Remove( 0 );
			delete[] file->buffer;
			delete file;
		}
	}

	CUtlVector< CFlexSceneFile * > m_FileList;
};

// Singleton manager
CFlexSceneFileManager g_FlexSceneFileManager;

//-----------------------------------------------------------------------------
// Purpose: Each CBaseFlex maintains a UtlRBTree of mappings, one for each loaded flex scene file it uses.  This is used to
//  sort the entries in the RBTree
// Input  : lhs - 
//			rhs - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseFlex::FlexSettingLessFunc( const FS_LocalToGlobal_t& lhs, const FS_LocalToGlobal_t& rhs )
{
	return lhs.m_Key < rhs.m_Key;
}

//-----------------------------------------------------------------------------
// Purpose: Since everyone shared a pSettinghdr now, we need to set up the localtoglobal mapping per entity, but 
//  we just do this in memory with an array of integers (could be shorts, I suppose)
// Input  : *pSettinghdr - 
//-----------------------------------------------------------------------------
void CBaseFlex::EnsureTranslations( const flexsettinghdr_t *pSettinghdr )
{
	Assert( pSettinghdr );

	FS_LocalToGlobal_t entry( pSettinghdr );

	unsigned short idx = m_LocalToGlobal.Find( entry );
	if ( idx != m_LocalToGlobal.InvalidIndex() )
		return;

	entry.SetCount( pSettinghdr->numkeys );

	for ( int i = 0; i < pSettinghdr->numkeys; ++i )
	{
		entry.m_Mapping[ i ] = FindFlexController( pSettinghdr->pLocalName( i ) );
	}

	m_LocalToGlobal.Insert( entry );
}

//-----------------------------------------------------------------------------
// Purpose: Look up instance specific mapping
// Input  : *pSettinghdr - 
//			key - 
// Output : int
//-----------------------------------------------------------------------------
int CBaseFlex::FlexControllerLocalToGlobal( const flexsettinghdr_t *pSettinghdr, int key )
{
	FS_LocalToGlobal_t entry( pSettinghdr );

	int idx = m_LocalToGlobal.Find( entry );
	if ( idx == m_LocalToGlobal.InvalidIndex() )
	{
		// This should never happen!!!
		Assert( 0 );
		Warning( "Unable to find mapping for flexcontroller %i, settings %p on %i/%s\n", key, pSettinghdr, entindex(), GetClassname() );
		EnsureTranslations( pSettinghdr );
		idx = m_LocalToGlobal.Find( entry );
		if ( idx == m_LocalToGlobal.InvalidIndex() )
		{
			Error( "CBaseFlex::FlexControllerLocalToGlobal failed!\n" );
		}
	}

	FS_LocalToGlobal_t& result = m_LocalToGlobal[ idx ];
	// Validate lookup
	Assert( result.m_nCount != 0 && key < result.m_nCount );
	int index = result.m_Mapping[ key ];
	return index;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *filename - 
//-----------------------------------------------------------------------------
const void *CBaseFlex::FindSceneFile( const char *filename )
{
	// Ask manager to get the globally cached scene instead.
	return g_FlexSceneFileManager.FindSceneFile( this, filename );
}

//-----------------------------------------------------------------------------
// Various methods to process facial SceneEvents: 
//-----------------------------------------------------------------------------
bool CBaseFlex::ProcessFlexAnimationSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event )
{
	if ( event->HasEndTime() )
	{
		AddFlexAnimation( info );
	}
	return true;
}

bool CBaseFlex::ProcessFlexSettingSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event )
{
	// Flexanimations have to have an end time!!!
	if ( !event->HasEndTime() )
		return true;

	// Look up the actual strings
	const char *scenefile	= event->GetParameters();
	const char *name		= event->GetParameters2();
	
	// Have to find both strings
	if ( scenefile && name )
	{
		// Find the scene file
		const flexsettinghdr_t *pExpHdr = ( const flexsettinghdr_t * )FindSceneFile( scenefile );
		if ( pExpHdr )
		{
			const flexsettinghdr_t  *pOverrideHdr = NULL;
			
			// Find overrides, if any exist
			studiohdr_t	*hdr = GetModelPtr();
			
			if ( hdr && scene_allowoverrides.GetBool() )
			{
				char overridefile[ 512 ];
				char shortname[ 128 ];
				char modelname[ 128 ];
				
				//Q_strncpy( modelname, modelinfo->GetModelName( model ) ,sizeof(modelname));
				Q_strncpy( modelname, hdr->name ,sizeof(modelname));
				
				// Fix up the name
				Q_FileBase( modelname, shortname, sizeof( shortname ) );
				
				Q_snprintf( overridefile,sizeof(overridefile), "%s/%s", shortname, scenefile );
				
				pOverrideHdr = ( const flexsettinghdr_t * )FindSceneFile( overridefile );
			}
			
			float scenetime = scene->GetTime();
			
			float scale = event->GetIntensity( scenetime );
			
			// Add the named expression
			AddFlexSetting( name, scale, pExpHdr, pOverrideHdr, !info->m_bStarted );
		}
	}

	return true;
}

bool CBaseFlex::ProcessFacingSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event )
{
	// make sure target exists
	if (info->m_hTarget == NULL)
		return false;

	// make sure we're still able to play this command
	if (!EnterSceneSequence( true ))
	{
		return false;
	}

	if (!info->m_bStarted)
	{
		info->m_flInitialYaw = GetLocalAngles().y;
	}

	// lock in place if aiming at self
	if (info->m_hTarget == this)
	{
		return true;
	}

	CAI_BaseNPC *myNpc = MyNPCPointer( );
	if (myNpc)
	{
		if (info->m_bIsMoving != IsMoving())
		{
			info->m_flInitialYaw = GetLocalAngles().y;
		}
		info->m_bIsMoving = IsMoving();

		// Msg("%f : %f - %f\n", scene->GetTime(), event->GetStartTime(), event->GetEndTime() );
		// FIXME: why are the splines ill behaved at the end?
		float intensity = event->GetIntensity( scene->GetTime() );
		if (info->m_bIsMoving)
		{
			myNpc->AddFacingTarget( info->m_hTarget, intensity, 0.2 );
		}
		else
		{
			float goalYaw = myNpc->CalcIdealYaw( info->m_hTarget->EyePosition() );

			float diff = UTIL_AngleDiff( goalYaw, info->m_flInitialYaw );

			float idealYaw = UTIL_AngleMod( info->m_flInitialYaw + diff * intensity );

			// Msg("yaw %.1f : %.1f (%.1f)\n", info->m_flInitialYaw, idealYaw, intensity ); 

			myNpc->GetMotor()->SetIdealYawAndUpdate( idealYaw );
		}

		// FIXME: what's a reasonable time to wait?
		myNpc->AddSceneLock( min( 2.0, scene->FindStopTime() - scene->GetTime() + 0.2 ) );
		return true;
	}
	return false;
}

static Activity DetermineExpressionMoveActivity( CChoreoEvent *event, CAI_BaseNPC *pNPC )
{
	Activity activity = ACT_WALK;
	const char *sParam2 = event->GetParameters2();
	if ( !sParam2 || !sParam2[0] )
		return activity;

	// Custom distance styles are appended to param2 with a space as a separator
	const char *pszAct = Q_strstr( sParam2, " " );
	if ( pszAct )
	{
		char szActName[256];
		Q_strncpy( szActName, sParam2, sizeof(szActName) );
		szActName[ (pszAct-sParam2) ] = '\0';
		pszAct = szActName;
	}
	else
	{
		pszAct = sParam2;
	}

	if ( !Q_strcmp( pszAct, "Walk" ) )
	{
		activity = ACT_WALK;
	}
	else if ( !Q_strcmp( pszAct, "Run" ) )
	{
		activity = ACT_RUN;
	}
	else if ( !Q_strcmp( pszAct, "CrouchWalk" ) )
	{
		activity = ACT_WALK_CROUCH;
	}
	else 
	{
		// Try and resolve the activity name
		activity = (Activity)ActivityList_IndexForName( pszAct );
		if ( activity == ACT_INVALID )
		{
			// Assume it's a sequence name
			pNPC->m_iszSceneCustomMoveSeq = AllocPooledString( pszAct );
			activity = ACT_SCRIPT_CUSTOM_MOVE;
		}
	}

	return activity;
}

bool CBaseFlex::ProcessMoveToSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event )
{
	// make sure target exists
	if (info->m_hTarget == NULL)
		return false;

	// FIXME: move to CBaseActor or BaseNPC
	CAI_BaseNPC *myNpc = MyNPCPointer( );
	if (!myNpc)
		return false;

	// make sure we're still able to play this command
	if (!EnterSceneSequence( true ))
	{
		return false;
	}

	// lock in place if aiming at self
	if (info->m_hTarget == this)
	{
		return true;
	}

	if (!info->m_bIsMoving && !IsMoving())
	{
		const Task_t *pCurTask = myNpc->GetTask();
		if ( pCurTask && (pCurTask->iTask == TASK_PLAY_SCENE || pCurTask->iTask == TASK_WAIT_FOR_MOVEMENT ) )
		{
			Activity moveActivity = DetermineExpressionMoveActivity( event, myNpc );
			// AI_NavGoal_t goal( info->m_hTarget->EyePosition(), moveActivity, AIN_HULL_TOLERANCE );
			myNpc->SetTarget( info->m_hTarget );

			float flDistTolerance;
			flDistTolerance = myNpc->GetHullWidth() / 2.0;
			// flDistTolerance = AIN_HULL_TOLERANCE;

			AI_NavGoal_t goal( GOALTYPE_TARGETENT, moveActivity, flDistTolerance, AIN_UPDATE_TARGET_POS );

			float flDist = (info->m_hTarget->EyePosition() - GetAbsOrigin()).Length2D();

			if (flDist > max( max( flDistTolerance, 0.1 ), event->GetDistanceToTarget()))
			{
				// Msg("flDist %.1f\n", flDist );
				int result = myNpc->GetNavigator()->SetGoal( goal, AIN_CLEAR_TARGET );
				if (result)
				{
					myNpc->GetNavigator()->SetMovementActivity( moveActivity );
					myNpc->GetNavigator()->SetArrivalDistance( event->GetDistanceToTarget() );
					info->m_bIsMoving = true;
				}
				else
				{
					// need route build failure case
					// Msg("actor %s unable to build route\n", STRING( myNpc->GetEntityName() ) );
					// Assert(0);

					if (scene_showmoveto.GetBool())
					{
						Vector vTestPoint;
						myNpc->GetMoveProbe()->FloorPoint( info->m_hTarget->EyePosition(), MASK_NPCSOLID, 0, -64, &vTestPoint );
						NDebugOverlay::HorzArrow( GetAbsOrigin() + Vector( 0, 0, 1 ), vTestPoint + Vector( 0, 0, 1 ), 4, 255, 0, 255, 0, false, 0.12 );
						NDebugOverlay::Box( vTestPoint, myNpc->GetHullMins(), myNpc->GetHullMaxs(), 255, 0, 255, 0, 0.12 );
					}
				}
			}
			else
			{
				info->m_bHasArrived = true;
			}
		}
	}
	else if (IsMoving())
	{
		// float flDist = (myNpc->GetNavigator()->GetGoalPos() - GetAbsOrigin()).Length2D();
		float flDist = (info->m_hTarget->EyePosition() - GetAbsOrigin()).Length2D();

		if (flDist <= event->GetDistanceToTarget())
		{
			myNpc->GetNavigator()->StopMoving();		// Stop moving
			info->m_bHasArrived = true;
		}
	}
	else
	{
		info->m_bIsMoving = false;
	}

	// show movement target
	if (scene_showmoveto.GetBool() && IsMoving())
	{
		Vector vecStart, vTestPoint;
		vecStart = myNpc->GetNavigator()->GetGoalPos();

		myNpc->GetMoveProbe()->FloorPoint( vecStart, MASK_NPCSOLID, 0, -64, &vTestPoint );

		int r, g, b;
		r = b = g = 0;
		if ( myNpc->GetNavigator()->CanFitAtPosition( vTestPoint, MASK_NPCSOLID ) )
		{
			if ( myNpc->GetMoveProbe()->CheckStandPosition( vTestPoint, MASK_NPCSOLID ) )
			{
				if (event->IsResumeCondition())
				{
					g = 255;
				}
				else
				{
					r = 255; g = 255;
				}
			}
			else
			{
				b = 255; g = 255;
			}
		}
		else
		{
			r = 255;
		}

		NDebugOverlay::HorzArrow( GetAbsOrigin() + Vector( 0, 0, 1 ), vTestPoint + Vector( 0, 0, 1 ), 4, r, g, b, 0, false, 0.12 );
		NDebugOverlay::Box( vTestPoint, myNpc->GetHullMins(), myNpc->GetHullMaxs(), r, g, b, 0, 0.12 );
	}
		
	myNpc->AddSceneLock( min( 2.0, event->GetEndTime() - scene->GetTime() + 0.2 ) );

	// handled in task
	return true;
}

bool CBaseFlex::ProcessLookAtSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event )
{
	CAI_BaseNPC *myNpc = MyNPCPointer( );
	if (myNpc && info->m_hTarget != NULL)
	{
		float intensity = event->GetIntensity( scene->GetTime() );
		myNpc->AddLookTarget( info->m_hTarget, intensity, 0.2 );
		if (scene_showlook.GetBool() && info->m_hTarget)
		{
			Vector tmp = info->m_hTarget->EyePosition() - myNpc->EyePosition();
			VectorNormalize( tmp );
			Vector p0 = myNpc->EyePosition();
			NDebugOverlay::VertArrow( p0, p0 + tmp * (4 + 16 * intensity ), 4, 255, 255, 255, 255, true, 0.12 );
		}
	}
	return true;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseFlex::ProcessSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event )
{
	switch ( event->GetType() )
	{
	case CChoreoEvent::FLEXANIMATION:
		return ProcessFlexAnimationSceneEvent( info, scene, event );

	case CChoreoEvent::EXPRESSION:
		return ProcessFlexSettingSceneEvent( info, scene, event );

	case CChoreoEvent::SEQUENCE:
		return ProcessSequenceSceneEvent( info, scene, event );

	case CChoreoEvent::GESTURE:
		return ProcessGestureSceneEvent( info, scene, event );

	case CChoreoEvent::FACE:
		return ProcessFacingSceneEvent( info, scene, event );

	case CChoreoEvent::MOVETO:
		return ProcessMoveToSceneEvent( info, scene, event );

	case CChoreoEvent::LOOKAT:
		return ProcessLookAtSceneEvent( info, scene, event );

	case CChoreoEvent::SPEAK:
		return true;

	default:
		{
			Msg( "unknown type %d in ProcessSceneEvent()\n", event->GetType() );
			Assert( 0 );
		}
	}

	return false;
}




flexsetting_t const *CBaseFlex::FindNamedSetting( flexsettinghdr_t const *pSettinghdr, const char *expr )
{
	int i;
	const flexsetting_t *pSetting = NULL;

	for ( i = 0; i < pSettinghdr->numflexsettings; i++ )
	{
		pSetting = pSettinghdr->pSetting( i );
		if ( !pSetting )
			continue;

		const char *name = pSetting->pszName();

		if ( !stricmp( name, expr ) )
			break;
	}

	if ( i>=pSettinghdr->numflexsettings )
	{
		return NULL;
	}

	return pSetting;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
static void NewMarkovIndex( flexsetting_t *pSetting )
{
	if ( pSetting->type != FS_MARKOV )
		return;

	int weighttotal = 0;
	int member = 0;
	for (int i = 0; i < pSetting->numsettings; i++)
	{
		flexmarkovgroup_t *group = pSetting->pMarkovGroup( i );
		if ( !group )
			continue;

		weighttotal += group->weight;
		if ( !weighttotal || random->RandomInt(0,weighttotal-1) < group->weight )
		{
			member = i;
		}
	}

	pSetting->currentindex = member;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *event - 
//-----------------------------------------------------------------------------
void CBaseFlex::AddFlexAnimation( CSceneEventInfo *info )
{
	if ( !info )
		return;

	CChoreoEvent *event = info->m_pEvent;
	if ( !event )
		return;

	CChoreoScene *scene = info->m_pScene;
	if ( !scene )
		return;

	if ( !event->GetTrackLookupSet() )
	{
		// Create lookup data
		for ( int i = 0; i < event->GetNumFlexAnimationTracks(); i++ )
		{
			CFlexAnimationTrack *track = event->GetFlexAnimationTrack( i );
			if ( !track )
				continue;

			if ( track->IsComboType() )
			{
				char name[ 512 ];
				Q_strncpy( name, "right_" ,sizeof(name));
				Q_strncat( name, track->GetFlexControllerName(),sizeof(name), COPY_ALL_CHARACTERS );

				track->SetFlexControllerIndex( 0, FindFlexController( name ), 0 );

				Q_strncpy( name, "left_" ,sizeof(name));
				Q_strncat( name, track->GetFlexControllerName(),sizeof(name), COPY_ALL_CHARACTERS );

				track->SetFlexControllerIndex( 0, FindFlexController( name ), 1 );
			}
			else
			{
				track->SetFlexControllerIndex( 0, FindFlexController( (char *)track->GetFlexControllerName() ) );
			}
		}

		event->SetTrackLookupSet( true );
	}

	float scenetime = scene->GetTime();

	float weight = event->GetIntensity( scenetime );

	// decay if this is a background scene and there's other flex animations playing
	weight = weight * info->UpdateWeight( this );

	// Compute intensity for each track in animation and apply
	// Iterate animation tracks
	for ( int i = 0; i < event->GetNumFlexAnimationTracks(); i++ )
	{
		CFlexAnimationTrack *track = event->GetFlexAnimationTrack( i );
		if ( !track )
			continue;

		// Disabled
		if ( !track->IsTrackActive() )
			continue;

		// Map track flex controller to global name
		if ( track->IsComboType() )
		{
			for ( int side = 0; side < 2; side++ )
			{
				int controller = track->GetFlexControllerIndex( side );

				// Get spline intensity for controller
				float flIntensity = track->GetIntensity( scenetime, side );
				if ( controller >= 0 )
				{
					float orig = GetFlexWeight( controller );
					SetFlexWeight( controller, orig * (1 - weight) + flIntensity * weight );
				}
			}
		}
		else
		{
			int controller = track->GetFlexControllerIndex( 0 );

			// Get spline intensity for controller
			float flIntensity = track->GetIntensity( scenetime, 0 );
			if ( controller >= 0 )
			{
				float orig = GetFlexWeight( controller );
				SetFlexWeight( controller, orig * (1 - weight) + flIntensity * weight );
			}
		}
	}

	info->m_bStarted = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *expr - 
//			scale - 
//			*pSettinghdr - 
//			*pOverrideHdr - 
//			newexpression - 
//-----------------------------------------------------------------------------
void CBaseFlex::AddFlexSetting( const char *expr, float scale, 
	const flexsettinghdr_t *pSettinghdr, const flexsettinghdr_t *pOverrideHdr, bool newexpression )
{
	int i;
	const flexsetting_t *pSetting = NULL;

	// Find the named setting in the base
	for ( i = 0; i < pSettinghdr->numflexsettings; i++ )
	{
		pSetting = pSettinghdr->pSetting( i );
		if ( !pSetting )
			continue;

		const char *name = pSetting->pszName();

		if ( !stricmp( name, expr ) )
			break;
	}

	if ( i>=pSettinghdr->numflexsettings )
	{
		return;
	}

	// Update markov chain if needed
	if ( newexpression )
	{
		if ( pSetting->type == FS_MARKOV )
		{
			NewMarkovIndex( (flexsetting_t *)pSetting );
		}
	}

	// Resolve markov chain for the returned setting
	pSetting = pSettinghdr->pTranslatedSetting( i );

	// Check for overrides
	if ( pOverrideHdr )
	{
		// Get name from setting
		const char *resolvedName = pSetting->pszName();
		if ( resolvedName )
		{
			// See if resolvedName exists in the override file
			const flexsetting_t *override = FindNamedSetting( pOverrideHdr, resolvedName );
			if ( override )
			{
				// If so, point at the override file instead
				pSettinghdr = pOverrideHdr;
				pSetting	= override;
			}
		}
	}

	flexweight_t *pWeights = NULL;
	int truecount = pSetting->psetting( (byte *)pSettinghdr, 0, &pWeights );
	if ( !pWeights )
		return;

	for (i = 0; i < truecount; i++, pWeights++)
	{
		// Translate to local flex controller
		// this is translating from the settings's local index to the models local index
		int index = FlexControllerLocalToGlobal( pSettinghdr, pWeights->key );

		// FIXME: this is supposed to blend based on pWeight->influence, but the order is wrong...
		// float value = GetFlexWeight( index ) * (1 - scale * pWeights->influence) + scale * pWeights->weight;

		// Add scaled weighting in to total
		float value = GetFlexWeight( index ) + scale * pWeights->weight;
		SetFlexWeight( index, value );
	}
}





//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *actor - 
//			*parameters - 
//-----------------------------------------------------------------------------
bool CBaseFlex::ProcessGestureSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event )
{
	if ( !info || !event || !scene )
		return false;
	
	if ( info->m_iLayer == REQUEST_DEFERRED_LAYER_ALLOCATION )
	{
		HandleStartGestureSceneEvent( info, scene, event, info->m_pActor );
	}

	if (info->m_iLayer >= 0)
	{
		// this happens after StudioFrameAdvance()
		// FIXME; this needs to be adjusted by npc offset to scene time? Known?
		// FIXME: what should this do when the scene loops?
		float duration = event->GetDuration( );
		float flEventCycle = (scene->GetTime() - event->GetStartTime()) / duration;
		float flCycle = event->GetOriginalPercentageFromPlaybackPercentage( flEventCycle );
		
		SetLayerCycle( info->m_iLayer, flCycle );

		float flWeight = event->GetIntensity( scene->GetTime() );

		/*
		if (stricmp( event->GetParameters(), "m_g_arms_crossed" ) == 0)
		{
			Msg("%.2f (%.2f) : %s : %.3f (%.3f) %.2f\n", scene->GetTime(), scene->GetTime() - event->GetStartTime(), event->GetParameters(), flCycle, flEventCycle, flWeight );
		}
		*/

		// fade out/in if npc is moving
		if (info->m_bIsPosture)
		{
			if (IsMoving())
			{
				info->m_flWeight = max( info->m_flWeight - 0.2, 0.0 );
			}
			else
			{
				info->m_flWeight = min( info->m_flWeight + 0.2, 1.0 );
			}
		}

		// 3x^2-2x^3
		float spline = 3 * info->m_flWeight * info->m_flWeight - 2 * info->m_flWeight * info->m_flWeight * info->m_flWeight;
		SetLayerWeight( info->m_iLayer, flWeight * spline );

		// update layer priority
		if (m_bUpdateLayerPriorities)
		{
			SetLayerPriority( info->m_iLayer, info->m_iPriority + GetScenePriority( scene ) );
		}

		/*
		Msg( "%d : %.2f (%.2f) : %.3f %.3f : %.3f\n",
			info->m_iLayer,
			scene->GetTime(), 
			(scene->GetTime() - event->GetStartTime()) / duration,
			flCycle,
			flNextCycle,
			rate );
		*/
	}

	return true;
}




//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *actor - 
//			*parameters - 
//-----------------------------------------------------------------------------
bool CBaseFlex::ProcessSequenceSceneEvent( CSceneEventInfo *info, CChoreoScene *scene, CChoreoEvent *event )
{
	if ( !info  || !event || !scene )
		return false;
	
	bool bNewlyAllocated = false;
	if ( info->m_iLayer == REQUEST_DEFERRED_LAYER_ALLOCATION )
	{
		bool result = HandleStartSequenceSceneEvent( info, scene, event, info->m_pActor );
		if (!result)
			return false;
		bNewlyAllocated = true;
	}

	if (info->m_iLayer >= 0)
	{
		float flWeight = event->GetIntensity( scene->GetTime() );

		// force layer to zero weight in newly allocated, fixed bug with inter-think spawned sequences blending in badly
		if (bNewlyAllocated)
			flWeight = 0.0;

		CAI_BaseNPC *myNpc = MyNPCPointer( );

		// fade out/in if npc is moving

		bool bFadeOut = IsMoving();
		if (myNpc && !(myNpc->IsCurSchedule( SCHED_SCENE_GENERIC ) || myNpc->GetActivity() == ACT_IDLE_ANGRY || myNpc->GetActivity() == ACT_IDLE) )
		{
			bFadeOut = true;
			if (info->m_flWeight == 1.0)
			{
				Warning( "%s playing CChoreoEvent::SEQUENCE but AI has forced them to do something different\n", STRING(GetEntityName()) );
			}
		}

		if (bFadeOut)
		{
			info->m_flWeight = max( info->m_flWeight - 0.2, 0.0 );
		}
		else
		{
			info->m_flWeight = min( info->m_flWeight + 0.2, 1.0 );
		}

		float spline = 3 * info->m_flWeight * info->m_flWeight - 2 * info->m_flWeight * info->m_flWeight * info->m_flWeight;
		SetLayerWeight( info->m_iLayer, flWeight * spline );

		bool looping = ((GetSequenceFlags( info->m_nSequence ) & STUDIO_LOOPING) != 0);
		if (!looping)
		{
			float dt =  scene->GetTime() - event->GetStartTime();
			float seq_duration = SequenceDuration( info->m_nSequence );
			float flCycle = dt / seq_duration;
			flCycle = clamp( flCycle, 0, 1.0 );
			SetLayerCycle( info->m_iLayer, flCycle );
		}

		if (myNpc)
		{
			myNpc->AddSceneLock( 0.2 );
		}

		// update layer priority
		if (m_bUpdateLayerPriorities)
		{
			SetLayerPriority( info->m_iLayer, info->m_iPriority + GetScenePriority( scene ) );
		}
	}

	// FIXME: clean up cycle index from restart
	return true;

}


//-----------------------------------------------------------------------------
// Purpose: Returns true if the actor is not currently in a scene OR if the actor
//  is in a scene (checked externally), but a PERMIT_RESPONSES event is active and 
//  the permit time period has enough time remaining to handle the response in full.
// Input  : response_length - 
//-----------------------------------------------------------------------------
bool CBaseFlex::PermitResponse( float response_length )
{
	// Nothing set, disallow it
	if ( m_flAllowResponsesEndTime <= 0.0f )
	{
		return false;
	}

	// If response ends before end of allow time, then that's okay
	if ( gpGlobals->curtime + response_length <= m_flAllowResponsesEndTime )
	{
		return true;
	}

	// Disallow responses for now
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Set response end time (0 to clear response blocking)
// Input  : endtime - 
//-----------------------------------------------------------------------------
void CBaseFlex::SetPermitResponse( float endtime )
{
	// Mark time after which we'll be occupied again (if in a scene)
	// Note a value of <= 0.0f means unset (always allow actor to speak if not in a scene,
	//  and always disallow if in a scene)
	m_flAllowResponsesEndTime = endtime;
}




// FIXME: move to CBaseActor
bool CBaseFlex::EnterSceneSequence( bool bRestart )
{
	CAI_BaseNPC *myNpc = MyNPCPointer( );

	if (!myNpc)
		return false;

	if (myNpc->IsCurSchedule( SCHED_SCENE_GENERIC ))
	{
		return true;
	}

	// for now, don't interrupt sequences that don't understand being interrupted
	if (myNpc->GetCurSchedule())
	{
		CAI_ScheduleBits testBits;
		myNpc->GetCurSchedule()->GetInterruptMask( &testBits );

		testBits.ClearBit( COND_PROVOKED );

		if (testBits.IsAllClear()) 
		{
			return false;
		}
	}

	if (myNpc->IsInterruptable())
	{
		if (myNpc->m_hCine)
		{
			// Assert( !(myNpc->GetFlags() & FL_FLY ) );
			myNpc->ExitScriptedSequence( );
		}

		myNpc->SetSchedule( SCHED_SCENE_GENERIC );
		return true;
	}

	return false;
}

bool CBaseFlex::ExitSceneSequence( void )
{
	return true;
}


//-----------------------------------------------------------------------------
// Purpose: keep track of last valid flex animation time and returns if the current info should play theirs
//-----------------------------------------------------------------------------

bool CBaseFlex::IsSuppressedFlexAnimation( CSceneEventInfo *info )
{
	// check for suppression if the current info is a background
	if (info->m_pScene && info->m_pScene->IsBackground())
	{
		// allow for slight jitter
		return m_flLastFlexAnimationTime > gpGlobals->curtime - GetAnimTimeInterval() * 1.5;
	}
	// keep track of last non-suppressable flex animation
	m_flLastFlexAnimationTime = gpGlobals->curtime;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: initialize weight for background events
//-----------------------------------------------------------------------------

void CSceneEventInfo::InitWeight( CBaseFlex *pActor )
{
	if (pActor->IsSuppressedFlexAnimation( this ))
	{
		m_flWeight = 0.0;
	}
	else
	{
		m_flWeight = 1.0;
	}
}

//-----------------------------------------------------------------------------
// Purpose: update weight for background events.  Only call once per think
//-----------------------------------------------------------------------------

float CSceneEventInfo::UpdateWeight( CBaseFlex *pActor )
{
	// decay if this is a background scene and there's other flex animations playing
	if (pActor->IsSuppressedFlexAnimation( this ))
	{
		m_flWeight = max( m_flWeight - 0.2, 0.0 );
	}
	else
	{
		m_flWeight = min( m_flWeight + 0.1, 1.0 );
	}
	return m_flWeight;
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

class CFlexCycler : public CBaseFlex
{
private:
	DECLARE_CLASS( CFlexCycler, CBaseFlex );
public:
	DECLARE_DATADESC();

	CFlexCycler() { m_iszSentence = NULL_STRING; m_sentence = 0; }
	void GenericCyclerSpawn(char *szModel, Vector vecMin, Vector vecMax);
	virtual int	ObjectCaps( void ) { return (BaseClass::ObjectCaps() | FCAP_IMPULSE_USE); }
	int OnTakeDamage( const CTakeDamageInfo &info );
	void Spawn( void );
	void Think( void );

	virtual void ProcessSceneEvents( void );

	// Don't treat as a live target
	virtual bool IsAlive( void ) { return FALSE; }

	float m_flextime;
	int m_flexnum;
	float m_flextarget[64];
	float m_blinktime;
	float m_looktime;
	Vector m_lookTarget;
	float m_speaktime;
	int	m_istalking;
	int	m_phoneme;

	string_t m_iszSentence;
	int m_sentence;

	void SetFlexTarget( int flexnum );
	int LookupFlex( const char *szTarget );
};

BEGIN_DATADESC( CFlexCycler )

	DEFINE_FIELD( m_flextime, FIELD_TIME ),
	DEFINE_FIELD( m_flexnum, FIELD_INTEGER ),
	DEFINE_ARRAY( m_flextarget, FIELD_FLOAT, 64 ),
	DEFINE_FIELD( m_blinktime, FIELD_TIME ),
	DEFINE_FIELD( m_looktime, FIELD_TIME ),
	DEFINE_FIELD( m_lookTarget, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_speaktime, FIELD_TIME ),
	DEFINE_FIELD( m_istalking, FIELD_INTEGER ),
	DEFINE_FIELD( m_phoneme, FIELD_INTEGER ),
	DEFINE_KEYFIELD( m_iszSentence, FIELD_STRING, "Sentence" ),
	DEFINE_FIELD( m_sentence, FIELD_INTEGER ),

END_DATADESC()


//
// we should get rid of all the other cyclers and replace them with this.
//
class CGenericFlexCycler : public CFlexCycler
{
public:
	DECLARE_CLASS( CGenericFlexCycler, CFlexCycler );

	void Spawn( void ) { GenericCyclerSpawn( (char *)STRING( GetModelName() ), Vector(-16, -16, 0), Vector(16, 16, 72) ); }
};

LINK_ENTITY_TO_CLASS( cycler_flex, CGenericFlexCycler );



ConVar	flex_expression( "flex_expression","-" );
ConVar	flex_talk( "flex_talk","0" );

// Cycler member functions

void CFlexCycler::GenericCyclerSpawn(char *szModel, Vector vecMin, Vector vecMax)
{
	if (!szModel || !*szModel)
	{
		Warning( "cycler at %.0f %.0f %0.f missing modelname\n", GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z );
		UTIL_Remove( this );
		return;
	}

	PrecacheModel( szModel );
	SetModel( szModel );

	CFlexCycler::Spawn( );

	UTIL_SetSize(this, vecMin, vecMax);

	Vector vecEyeOffset;
	GetEyePosition( GetModelPtr(), vecEyeOffset );
	SetViewOffset( vecEyeOffset );

	InitBoneControllers();

	if (GetNumFlexControllers() < 5)
		Warning( "cycler_flex used on model %s without enough flexes.\n", szModel );
}

void CFlexCycler::Spawn( )
{
	Precache();
	/*
	if ( m_spawnflags & FCYCLER_NOTSOLID )
	{
		SetSolid( SOLID_NOT );
	}
	else
	{
		SetSolid( SOLID_SLIDEBOX );
	}
	*/

	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_STANDABLE );

	SetMoveType( MOVETYPE_NONE );
	m_takedamage		= DAMAGE_YES;
	m_iHealth			= 80000;// no cycler should die
	
	m_flPlaybackRate	= 1.0f;
	m_flGroundSpeed		= 0;


	SetNextThink( gpGlobals->curtime + 1.0f );

	ResetSequenceInfo( );

	m_flCycle = random->RandomFloat( 0, 1.0 );
}

const char *predef_flexcontroller_names[] = { 
	"right_lid_raiser",
	"left_lid_raiser",
	"right_lid_tightener",
	"left_lid_tightener",
	"right_lid_droop",
	"left_lid_droop",
	"right_inner_raiser",
	"left_inner_raiser",
	"right_outer_raiser",
	"left_outer_raiser",
	"right_lowerer",
	"left_lowerer",
	"right_cheek_raiser",
	"left_cheek_raiser",
	"wrinkler",
	"right_upper_raiser",
	"left_upper_raiser",
	"right_corner_puller",
	"left_corner_puller",
	"corner_depressor",
	"chin_raiser",
	"right_puckerer",
	"left_puckerer",
	"right_funneler",
	"left_funneler",
	"tightener",
	"jaw_clencher",
	"jaw_drop",
	"right_mouth_drop",
	"left_mouth_drop", 
	NULL };

float predef_flexcontroller_values[7][30] = {
/* 0 */	{ 0.700,0.560,0.650,0.650,0.650,0.585,0.000,0.000,0.400,0.040,0.000,0.000,0.450,0.450,0.000,0.000,0.000,0.750,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.150,1.000,0.000,0.000,0.000 }, 
/* 1 */	{ 0.450,0.450,0.450,0.450,0.000,0.000,0.000,0.000,0.300,0.300,0.000,0.000,0.250,0.250,0.000,0.000,0.000,0.750,0.750,0.000,0.000,0.000,0.000,0.400,0.400,0.000,1.000,0.000,0.050,0.050 }, 
/* 2 */	{ 0.200,0.200,0.500,0.500,0.150,0.150,0.100,0.100,0.150,0.150,0.000,0.000,0.700,0.700,0.000,0.000,0.000,0.750,0.750,0.000,0.200,0.000,0.000,0.000,0.000,0.000,0.850,0.000,0.000,0.000 }, 
/* 3 */	{ 0.000,0.000,0.000,0.000,0.000,0.000,0.300,0.300,0.000,0.000,0.000,0.000,0.000,0.000,0.100,0.000,0.000,0.000,0.000,0.700,0.300,0.000,0.000,0.200,0.200,0.000,0.000,0.300,0.000,0.000 }, 
/* 4 */	{ 0.450,0.450,0.000,0.000,0.450,0.450,0.000,0.000,0.000,0.000,0.450,0.450,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.300,0.000,0.000,0.000,0.000 }, 
/* 5 */	{ 0.000,0.000,0.350,0.350,0.150,0.150,0.300,0.300,0.450,0.450,0.000,0.000,0.200,0.200,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.200,0.200,0.000,0.000,0.300,0.000,0.000,0.000,0.000 }, 
/* 6 */	{ 0.000,0.000,0.650,0.650,0.750,0.750,0.000,0.000,0.000,0.000,0.300,0.300,0.000,0.000,0.000,0.250,0.250,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000 }
};

//-----------------------------------------------------------------------------
// Purpose: Changes sequences when shot
//-----------------------------------------------------------------------------
int CFlexCycler::OnTakeDamage( const CTakeDamageInfo &info )
{
	int nSequence = GetSequence() + 1;
	if (!IsValidSequence( nSequence ))
	{
		nSequence = 0;
	}

	ResetSequence( nSequence );
	m_flCycle = 0;

	return 0;
}


void CFlexCycler::SetFlexTarget( int flexnum )
{
	m_flextarget[flexnum] = random->RandomFloat( 0.5, 1.0 );

	const char *pszType = GetFlexControllerType( flexnum );

	for (int i = 0; i < GetNumFlexControllers(); i++)
	{
		if (i != flexnum)
		{
			const char *pszOtherType = GetFlexControllerType( i );
			if (stricmp( pszType, pszOtherType ) == 0)
			{
				m_flextarget[i] = 0;
			}
		}
	}

	// HACK, for now, consider then linked is named "right_" or "left_"
	if (strncmp( "right_", GetFlexControllerName( flexnum ), 6 ) == 0)
	{
		m_flextarget[flexnum+1] = m_flextarget[flexnum];
	}
	else if (strncmp( "left_", GetFlexControllerName( flexnum ), 5 ) == 0)
	{
		m_flextarget[flexnum-1] = m_flextarget[flexnum];
	}
}


int CFlexCycler::LookupFlex( const char *szTarget  )
{
	for (int i = 0; i < GetNumFlexControllers(); i++)
	{
		const char *pszFlex = GetFlexControllerName( i );
		if (stricmp( szTarget, pszFlex ) == 0)
		{
			return i;
		}
	}
	return -1;
}


void CFlexCycler::Think( void )
{
	SetNextThink( gpGlobals->curtime + 0.1f );

	StudioFrameAdvance ( );

	if (IsSequenceFinished() && !SequenceLoops())
	{
		// ResetSequenceInfo();
		// hack to avoid reloading model every frame
		m_flAnimTime = gpGlobals->curtime;
		m_flPlaybackRate = 1.0;
		m_bSequenceFinished = false;
		m_flLastEventCheck = 0;
		m_flCycle = 0;
	}

	// only do this if they have more than eyelid movement
	if (GetNumFlexControllers() > 2)
	{
		const char *pszExpression = flex_expression.GetString();

		if (pszExpression && pszExpression[0] == '+' && pszExpression[1] != '\0')
		{
			int i;
			int j = atoi( &pszExpression[1] );
			for ( i = 0; i < GetNumFlexControllers(); i++)
			{
				m_flextarget[m_flexnum] = 0;
			}

			for (i = 0; i < 35 && predef_flexcontroller_names[i]; i++)
			{
				m_flexnum = LookupFlex( predef_flexcontroller_names[i] );
				m_flextarget[m_flexnum] = predef_flexcontroller_values[j][i];
				// Msg( "%s %.3f\n", predef_flexcontroller_names[i], predef_flexcontroller_values[j][i] );
			}
		}
		else if (pszExpression && pszExpression[0] != '\0' && strcmp(pszExpression, "+") != 0)
		{
			char szExpression[128];
			char szTemp[32];

			Q_strncpy( szExpression, pszExpression ,sizeof(szExpression));
			char *pszExpression = szExpression;

			while (*pszExpression != '\0')
			{
				if (*pszExpression == '+')
					*pszExpression = ' ';
				
				pszExpression++;
			}

			pszExpression = szExpression;

			while (*pszExpression)
			{
				if (*pszExpression != ' ')
				{
					if (*pszExpression == '-')
					{
						for (int i = 0; i < GetNumFlexControllers(); i++)
						{
							m_flextarget[i] = 0;
						}
					}
					else if (*pszExpression == '?')
					{
						for (int i = 0; i < GetNumFlexControllers(); i++)
						{
							Msg( "\"%s\" ", GetFlexControllerName( i ) );
						}
						Msg( "\n" );
						flex_expression.SetValue( "" );
					}
					else
					{
						if (sscanf( pszExpression, "%31s", szTemp ) == 1)
						{
							m_flexnum = LookupFlex( szTemp );

							if (m_flexnum != -1 && m_flextarget[m_flexnum] != 1)
							{
								m_flextarget[m_flexnum] = 1.0;
								// SetFlexTarget( m_flexnum );
							}
							pszExpression += strlen( szTemp ) - 1;
						}
					}
				}
				pszExpression++;
			}
		} 
		else if (m_flextime < gpGlobals->curtime)
		{
			// m_flextime = gpGlobals->curtime + 1.0; // RandomFloat( 0.1, 0.5 );
			m_flextime = gpGlobals->curtime + random->RandomFloat( 0.3, 0.5 ) * (30.0 / GetNumFlexControllers());
			m_flexnum = random->RandomInt( 0, GetNumFlexControllers() - 1 );

			// m_flexnum = (pflex->num + 1) % r_psubmodel->numflexes;

			if (m_flextarget[m_flexnum] == 1)
			{
				m_flextarget[m_flexnum] = 0;
				// pflex->time = cl.time + 0.1;
			}
			else if (stricmp( GetFlexControllerType( m_flexnum ), "phoneme" ) != 0)
			{
				if (strstr( GetFlexControllerName( m_flexnum ), "upper_raiser" ) == NULL)
				{
					Msg( "%s:%s\n", GetFlexControllerType( m_flexnum ), GetFlexControllerName( m_flexnum ) );
					SetFlexTarget( m_flexnum );
				}
			}

#if 0
			char szWhat[256];
			szWhat[0] = '\0';
			for (int i = 0; i < GetNumFlexControllers(); i++)
			{
				if (m_flextarget[i] == 1.0)
				{
					if (stricmp( GetFlexFacs( i ), "upper") != 0 && stricmp( GetFlexFacs( i ), "lower") != 0)
					{
						if (szWhat[0] == '\0')
							Q_strncat( szWhat, "-", sizeof( szWhat ), COPY_ALL_CHARACTERS );
						else
							Q_strncat( szWhat, "+", sizeof( szWhat ), COPY_ALL_CHARACTERS );
						Q_strncat( szWhat, GetFlexFacs( i ), sizeof( szWhat ), COPY_ALL_CHARACTERS );
					}
				}
			}
			Msg( "%s\n", szWhat );
#endif
		}

		// slide it up.
		for (int i = 0; i < GetNumFlexControllers(); i++)
		{
			float weight = GetFlexWeight( i );

			if (weight != m_flextarget[i])
			{
				weight = weight + (m_flextarget[i] - weight) / random->RandomFloat( 2.0, 4.0 );
			}
			weight = clamp( weight, 0.0f, 1.0f );
			SetFlexWeight( i, weight );
		}

#if 1
		if (flex_talk.GetInt() == -1)
		{
			m_istalking = 1;
			char pszSentence[256];
			Q_snprintf( pszSentence,sizeof(pszSentence), "%s%d", STRING(m_iszSentence), m_sentence++ );
			int sentenceIndex = engine->SentenceIndexFromName( pszSentence );
			if (sentenceIndex >= 0)
			{
				Msg( "%d : %s\n", sentenceIndex, pszSentence );
				CPASAttenuationFilter filter( this );
				CBaseEntity::EmitSentenceByIndex( filter, entindex(), CHAN_VOICE, sentenceIndex, 1, SNDLVL_TALKING, 0, PITCH_NORM );
			}
			else
			{
				m_sentence = 0;
			}

			flex_talk.SetValue( "0" );
		} 
		else if (!FStrEq( flex_talk.GetString(), "0") )
		{
			int sentenceIndex = engine->SentenceIndexFromName( flex_talk.GetString() );
			if (sentenceIndex >= 0)
			{
				CPASAttenuationFilter filter( this );
				CBaseEntity::EmitSentenceByIndex( filter, entindex(), CHAN_VOICE, sentenceIndex, 1, SNDLVL_TALKING, 0, PITCH_NORM );
			}
			flex_talk.SetValue( "0" );
		}
#else
		if (flex_talk.GetInt())
		{
			if (m_speaktime < gpGlobals->curtime)
			{
				if (m_phoneme == 0)
				{
					for (m_phoneme = 0; m_phoneme < GetNumFlexControllers(); m_phoneme++)
					{
						if (stricmp( GetFlexFacs( m_phoneme ), "27") == 0)
							break;
					}
				}
				m_istalking = !m_istalking;
				if (m_istalking)
				{
					m_looktime = gpGlobals->curtime - 1.0;
					m_speaktime = gpGlobals->curtime + random->RandomFloat( 0.5, 2.0 );
				}
				else
				{
					m_speaktime = gpGlobals->curtime + random->RandomFloat( 1.0, 3.0 );
				}
			}

			for (i = m_phoneme; i < GetNumFlexControllers(); i++)
			{
				SetFlexWeight( i, 0.0f );
			}

			if (m_istalking)
			{
				m_flextime = gpGlobals->curtime + random->RandomFloat( 0.0, 0.2 );
				m_flexWeight[random->RandomInt(m_phoneme, GetNumFlexControllers()-1)] = random->RandomFloat( 0.5, 1.0 );
				float mouth = random->RandomFloat( 0.0, 1.0 );
				float jaw = random->RandomFloat( 0.0, 1.0 );

				m_flexWeight[m_phoneme - 2] = jaw * (mouth);
				m_flexWeight[m_phoneme - 1] = jaw * (1.0 - mouth);
			}
		}
		else
		{
			m_istalking = 0;
		}
#endif

		// blink
		if (m_blinktime < gpGlobals->curtime)
		{
			Blink();
			m_blinktime = gpGlobals->curtime + random->RandomFloat( 1.5, 4.5 );
		}
	}


	Vector forward, right, up;
	GetVectors( &forward, &right, &up );

	CBaseEntity *pPlayer = (CBaseEntity *)UTIL_GetLocalPlayer();
	if (pPlayer)
	{
		if (pPlayer->GetSmoothedVelocity().Length() != 0 && DotProduct( forward, pPlayer->EyePosition() - EyePosition()) > 0.5)
		{
			m_lookTarget = pPlayer->EyePosition();
			m_looktime = gpGlobals->curtime + random->RandomFloat(2.0,4.0);
		}
		else if (m_looktime < gpGlobals->curtime)
		{
			if ((!m_istalking) && random->RandomInt( 0, 1 ) == 0)
			{
				m_lookTarget = EyePosition() + forward * 128 + right * random->RandomFloat(-64,64) + up * random->RandomFloat(-32,32);
				m_looktime = gpGlobals->curtime + random->RandomFloat(0.3,1.0);

				if (m_blinktime - 0.5 < gpGlobals->curtime)
				{
					Blink();
				}
			}
			else
			{
				m_lookTarget = pPlayer->EyePosition();
				m_looktime = gpGlobals->curtime + random->RandomFloat(1.0,4.0);
			}
		}

#if 0
		float dt = acos( DotProduct( (m_lookTarget - EyePosition()).Normalize(), (m_viewtarget - EyePosition()).Normalize() ) );

		if (dt > M_PI / 4)
		{
			dt = (M_PI / 4) * dt;
			m_viewtarget = ((1 - dt) * m_viewtarget + dt * m_lookTarget);
		}
#endif

		SetViewtarget( m_lookTarget );
	}

	// Handle any facial animation from scene playback
	// FIXME: do we still actually need flex cyclers?
	// AddSceneSceneEvents();
}


void CFlexCycler::ProcessSceneEvents( void )
{
	// Don't do anything since we handle facial stuff in Think()
}



