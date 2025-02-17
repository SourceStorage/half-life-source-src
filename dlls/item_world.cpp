//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Handling for the base world item. Most of this was moved from items.cpp.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "player.h"
#include "items.h"
#include "gamerules.h"
#include "engine/IEngineSound.h"
#include "iservervehicle.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define ITEM_PICKUP_BOX_BLOAT		24

class CWorldItem : public CBaseAnimating
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CWorldItem, CBaseAnimating );

	bool	KeyValue( const char *szKeyName, const char *szValue ); 
	void	Spawn( void );

	int		m_iType;
};

LINK_ENTITY_TO_CLASS(world_items, CWorldItem);

BEGIN_DATADESC( CWorldItem )

	DEFINE_FIELD( m_iType, FIELD_INTEGER ),

END_DATADESC()


bool CWorldItem::KeyValue( const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "type"))
	{
		m_iType = atoi(szValue);
	}
	else
		return BaseClass::KeyValue( szKeyName, szValue );

	return true;
}

void CWorldItem::Spawn( void )
{
	CBaseEntity *pEntity = NULL;

	switch (m_iType) 
	{
	case 44: // ITEM_BATTERY:
		pEntity = CBaseEntity::Create( "item_battery", GetLocalOrigin(), GetLocalAngles() );
		break;
	case 45: // ITEM_SUIT:
		pEntity = CBaseEntity::Create( "item_suit", GetLocalOrigin(), GetLocalAngles() );
		break;
	}

	if (!pEntity)
	{
		Warning("unable to create world_item %d\n", m_iType );
	}
	else
	{
		pEntity->m_target = m_target;
		pEntity->SetName( GetEntityName() );
		pEntity->ClearSpawnFlags();
		pEntity->AddSpawnFlags( m_spawnflags );
	}

	UTIL_RemoveImmediate( this );
}


BEGIN_DATADESC( CItem )

	DEFINE_FIELD( m_bActivateWhenAtRest,	 FIELD_BOOLEAN ),

	// Function Pointers
	DEFINE_ENTITYFUNC( ItemTouch ),
	DEFINE_THINKFUNC( Materialize ),
	DEFINE_THINKFUNC( ComeToRest ),

	// Outputs
	DEFINE_OUTPUT(m_OnPlayerTouch, "OnPlayerTouch"),

END_DATADESC()


//-----------------------------------------------------------------------------
// Constructor 
//-----------------------------------------------------------------------------
CItem::CItem()
{
	m_bActivateWhenAtRest = false;
}

bool CItem::CreateItemVPhysicsObject( void )
{
	// Create the object in the physics system
	int nSolidFlags = GetSolidFlags() | FSOLID_NOT_STANDABLE;
	if ( !m_bActivateWhenAtRest )
	{
		nSolidFlags |= FSOLID_TRIGGER;
	}

	if ( VPhysicsInitNormal( SOLID_VPHYSICS, nSolidFlags, false ) == NULL )
	{
		SetSolid( SOLID_BBOX );
		AddSolidFlags( nSolidFlags );

		// If it's not physical, drop it to the floor
		if (UTIL_DropToFloor(this, MASK_SOLID) == 0)
		{
			Warning( "Item %s fell out of level at %f,%f,%f\n", GetClassname(), GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z);
			UTIL_Remove( this );
			return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CItem::Spawn( void )
{
	SetMoveType( MOVETYPE_FLYGRAVITY );
	SetSolid( SOLID_BBOX );
	SetBlocksLOS( false );
	AddEFlags( EFL_NO_ROTORWASH_PUSH );
	
	// This will make them not collide with the player, but will collide
	// against other items + weapons
	SetCollisionGroup( COLLISION_GROUP_WEAPON );
	CollisionProp()->UseTriggerBounds( true, ITEM_PICKUP_BOX_BLOAT );
	SetTouch(&CItem::ItemTouch);

	if ( CreateItemVPhysicsObject() == false )
		 return;
	
	m_vOriginalSpawnSpot = GetAbsOrigin();

	m_takedamage = DAMAGE_EVENTS_ONLY;
}

void CItem::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CBasePlayer *pPlayer = ToBasePlayer( pActivator );
	
	if ( pPlayer )
	{
		pPlayer->PickupObject( this );
	}
}

extern int gEvilImpulse101;


//-----------------------------------------------------------------------------
// Activate when at rest, but don't allow pickup until then
//-----------------------------------------------------------------------------
void CItem::ActivateWhenAtRest()
{
	RemoveSolidFlags( FSOLID_TRIGGER );
	m_bActivateWhenAtRest = true;
	SetThink( &CItem::ComeToRest );
	SetNextThink( gpGlobals->curtime + 0.5f );
}


//-----------------------------------------------------------------------------
// Become touchable when we are at rest
//-----------------------------------------------------------------------------
void CItem::OnEntityEvent( EntityEvent_t event, void *pEventData )
{
	BaseClass::OnEntityEvent( event, pEventData );

	switch( event )
	{
	case ENTITY_EVENT_WATER_TOUCH:
		ComeToRest();
		break;
	}
}


//-----------------------------------------------------------------------------
// Become touchable when we are at rest
//-----------------------------------------------------------------------------
void CItem::ComeToRest( void )
{
	if ( m_bActivateWhenAtRest )
	{
		m_bActivateWhenAtRest = false;
		AddSolidFlags( FSOLID_TRIGGER );
		SetThink( NULL );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : pOther - 
//-----------------------------------------------------------------------------
void CItem::ItemTouch( CBaseEntity *pOther )
{
	// Vehicles can touch items + pick them up
	if ( pOther->GetServerVehicle() )
	{
		pOther = pOther->GetServerVehicle()->GetPassenger();
		if ( !pOther )
			return;
	}

	// if it's not a player, ignore
	if ( !pOther->IsPlayer() )
		return;

	// Can I even pick stuff up?
	if ( pOther->IsEFlagSet( EFL_NO_WEAPON_PICKUP ) )
		return;

	CBasePlayer *pPlayer = (CBasePlayer *)pOther;

	// ok, a player is touching this item, but can he have it?
	if ( !g_pGameRules->CanHaveItem( pPlayer, this ) )
	{
		// no? Ignore the touch.
		return;
	}

	if (MyTouch( pPlayer ))
	{
		m_OnPlayerTouch.FireOutput(pOther, this);

		SetTouch( NULL );
		SetThink( NULL );
		
		// player grabbed the item. 
		g_pGameRules->PlayerGotItem( pPlayer, this );
		if ( g_pGameRules->ItemShouldRespawn( this ) == GR_ITEM_RESPAWN_YES )
		{
			Respawn(); 
		}
		else
		{
			UTIL_Remove( this );
		}
	}
	else if (gEvilImpulse101)
	{
		UTIL_Remove( this );
	}
}

CBaseEntity* CItem::Respawn( void )
{
	SetTouch( NULL );
	AddEffects( EF_NODRAW );

	VPhysicsDestroyObject();

	SetMoveType( MOVETYPE_NONE );
	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_TRIGGER );

	UTIL_SetOrigin( this, g_pGameRules->VecItemRespawnSpot( this ) );// blip to whereever you should respawn.

	UTIL_DropToFloor( this, MASK_SOLID );
	

	SetThink ( &CItem::Materialize );
	SetNextThink( gpGlobals->curtime + g_pGameRules->FlItemRespawnTime( this ) );
	return this;
}

void CItem::Materialize( void )
{
	CreateItemVPhysicsObject();

	if ( IsEffectActive( EF_NODRAW ) )
	{
		// changing from invisible state to visible.

#ifdef HL2MP
		EmitSound( "AlyxEmp.Charge" );
#else
		EmitSound( "Item.Materialize" );
#endif
		RemoveEffects( EF_NODRAW );
		DoMuzzleFlash();
	}

	SetTouch( &CItem::ItemTouch );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CItem::Precache()
{
	BaseClass::Precache();

	PrecacheScriptSound( "Item.Materialize" );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPhysGunUser - 
//			PICKED_UP_BY_CANNON - 
//-----------------------------------------------------------------------------
void CItem::OnPhysGunPickup( CBasePlayer *pPhysGunUser, PhysGunPickup_t reason )
{
	if ( reason == PICKED_UP_BY_CANNON )
	{
		// Expand the pickup box
		CollisionProp()->UseTriggerBounds( true, ITEM_PICKUP_BOX_BLOAT * 2 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPhysGunUser - 
//			reason - 
//-----------------------------------------------------------------------------
void CItem::OnPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t reason )
{
	// Restore the pickup box to the original
	CollisionProp()->UseTriggerBounds( true, ITEM_PICKUP_BOX_BLOAT );
}