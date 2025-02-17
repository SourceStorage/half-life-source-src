//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ITEMS_H
#define ITEMS_H

#ifdef _WIN32
#pragma once
#endif

#include "entityoutput.h"
#include "player_pickup.h"

// Armor given by a battery
#define MAX_NORMAL_BATTERY	100

// Ammo counts given by ammo items
#define SIZE_AMMO_PISTOL			20
#define SIZE_AMMO_PISTOL_LARGE		100
#define SIZE_AMMO_SMG1				45
#define SIZE_AMMO_SMG1_LARGE		225
#define SIZE_AMMO_AR2				20
#define SIZE_AMMO_AR2_LARGE			100
#define SIZE_AMMO_RPG_ROUND			1
#define SIZE_AMMO_SMG1_GRENADE		1
#define SIZE_AMMO_BUCKSHOT			20
#define SIZE_AMMO_357				6
#define SIZE_AMMO_357_LARGE			20
#define SIZE_AMMO_CROSSBOW			6
#define	SIZE_AMMO_AR2_ALTFIRE		1

class CItem : public CBaseAnimating, public CDefaultPlayerPickupVPhysics
{
public:
	DECLARE_CLASS( CItem, CBaseAnimating );

	CItem();

	virtual void Spawn( void );
	virtual void Precache();

	CBaseEntity* Respawn( void );
	void ItemTouch( CBaseEntity *pOther );
	void Materialize( void );
	virtual bool MyTouch( CBasePlayer *pPlayer ) { return false; };

	// Become touchable when we are at rest
	virtual void OnEntityEvent( EntityEvent_t event, void *pEventData );

	// Activate when at rest, but don't allow pickup until then
	void ActivateWhenAtRest();

	// IPlayerPickupVPhysics
	virtual void OnPhysGunPickup( CBasePlayer *pPhysGunUser, PhysGunPickup_t reason = PICKED_UP_BY_CANNON );
	virtual void OnPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t reason );

	virtual int	ObjectCaps() { return BaseClass::ObjectCaps() | FCAP_IMPULSE_USE; };
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	Vector	GetOriginalSpawnSpot( void ) { return m_vOriginalSpawnSpot;	}
	bool	CreateItemVPhysicsObject( void );

	DECLARE_DATADESC();
private:
	void ComeToRest( void );

private:
	bool		m_bActivateWhenAtRest;
	COutputEvent m_OnPlayerTouch;
	
	Vector		m_vOriginalSpawnSpot;

};

#endif // ITEMS_H
