//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
// nav.h
// Data structures and constants for the Navigation Mesh system
// Author: Michael S. Booth (mike@turtlerockstudios.com), January 2003

#ifndef _NAV_H_
#define _NAV_H_

#include "modelentities.h"		// for CFuncBrush

/**
 * Below are several constants used by the navigation system.
 * @todo Move these into TheNavMesh singleton.
 */
const float GenerationStepSize = 25.0f;			///< (30) was 20, but bots can't fit always fit
const float StepHeight = 18.0f;					///< if delta Z is greater than this, we have to jump to get up
const float JumpHeight = 41.8f;					///< if delta Z is less than this, we can jump up on it
const float JumpCrouchHeight = 58.0f;			///< (48) if delta Z is less than or equal to this, we can jumpcrouch up on it

// Strictly speaking, you CAN get up a slope of 1.643 (about 59 degrees), but you move very, very slowly
// This slope will represent the slope you can navigate without much slowdown
const float MaxSlope = 1.4f;					///< rise/run - if greater than this, we can't move up it (de_survivor canyon ramps)

// instead of MaxSlope, we are using the following max Z component of a unit normal
const float MaxUnitZSlope = 0.7f;

const float BotRadius = 10.0f;					///< circular extent that contains bot
const float DeathDrop = 200.0f;					///< (300) distance at which we will die if we fall - should be about 600, and pay attention to fall damage during pathfind

const float HalfHumanWidth = 16.0f;
const float HalfHumanHeight = 36.0f;
const float HumanHeight = 72.0f;


#define NAV_MAGIC_NUMBER 0xFEEDFACE				///< to help identify nav files


/**
 * A place is a named group of navigation areas
 */
typedef unsigned int Place;
#define UNDEFINED_PLACE 0				// ie: "no place"
#define ANY_PLACE 0xFFFF

enum NavErrorType
{
	NAV_OK,
	NAV_CANT_ACCESS_FILE,
	NAV_INVALID_FILE,
	NAV_BAD_FILE_VERSION,
	NAV_CORRUPT_DATA,
};

enum NavAttributeType
{
	NAV_MESH_CROUCH	= 0x01,				///< must crouch to use this node/area
	NAV_MESH_JUMP	= 0x02,				///< must jump to traverse this area
	NAV_MESH_PRECISE = 0x04,			///< do not adjust for obstacles, just move along area
	NAV_MESH_NO_JUMP = 0x08,			///< inhibit discontinuity jumping
};

enum NavDirType
{
	NORTH = 0,
	EAST = 1,
	SOUTH = 2,
	WEST = 3,

	NUM_DIRECTIONS
};

/**
 * Defines possible ways to move from one area to another
 */
enum NavTraverseType
{
	// NOTE: First 4 directions MUST match NavDirType
	GO_NORTH = 0,
	GO_EAST,
	GO_SOUTH,
	GO_WEST,
	GO_LADDER_UP,
	GO_LADDER_DOWN,
	GO_JUMP,

	NUM_TRAVERSE_TYPES
};

enum NavCornerType
{
	NORTH_WEST = 0,
	NORTH_EAST = 1,
	SOUTH_EAST = 2,
	SOUTH_WEST = 3,

	NUM_CORNERS
};

enum NavRelativeDirType
{
	FORWARD = 0,
	RIGHT,
	BACKWARD,
	LEFT,
	UP,
	DOWN,

	NUM_RELATIVE_DIRECTIONS
};

struct Extent
{
	Vector lo, hi;

	float SizeX( void ) const	{ return hi.x - lo.x; }
	float SizeY( void ) const	{ return hi.y - lo.y; }
	float SizeZ( void ) const	{ return hi.z - lo.z; }
	float Area( void ) const	{ return SizeX() * SizeY(); }

	/// return true if 'pos' is inside of this extent
	bool Contains( const Vector *pos ) const
	{
		return (pos->x >= lo.x && pos->x <= hi.x &&
				pos->y >= lo.y && pos->y <= hi.y &&
				pos->z >= lo.z && pos->z <= hi.z);
	}
};

struct Ray
{
	Vector from, to;
};


class CNavArea;
class CNavNode;


//--------------------------------------------------------------------------------------------------------------
inline NavDirType OppositeDirection( NavDirType dir )
{
	switch( dir )
	{
		case NORTH: return SOUTH;
		case SOUTH: return NORTH;
		case EAST:	return WEST;
		case WEST:	return EAST;
	}

	return NORTH;
}

//--------------------------------------------------------------------------------------------------------------
inline NavDirType DirectionLeft( NavDirType dir )
{
	switch( dir )
	{
		case NORTH: return WEST;
		case SOUTH: return EAST;
		case EAST:	return NORTH;
		case WEST:	return SOUTH;
	}

	return NORTH;
}

//--------------------------------------------------------------------------------------------------------------
inline NavDirType DirectionRight( NavDirType dir )
{
	switch( dir )
	{
		case NORTH: return EAST;
		case SOUTH: return WEST;
		case EAST:	return SOUTH;
		case WEST:	return NORTH;
	}

	return NORTH;
}

//--------------------------------------------------------------------------------------------------------------
inline void AddDirectionVector( Vector *v, NavDirType dir, float amount )
{
	switch( dir )
	{
		case NORTH: v->y -= amount; return;
		case SOUTH: v->y += amount; return;
		case EAST:  v->x += amount; return;
		case WEST:  v->x -= amount; return;
	}
}

//--------------------------------------------------------------------------------------------------------------
inline float DirectionToAngle( NavDirType dir )
{
	switch( dir )
	{
		case NORTH:	return 270.0f;
		case SOUTH:	return 90.0f;
		case EAST:	return 0.0f;
		case WEST:	return 180.0f;
	}

	return 0.0f;
}

//--------------------------------------------------------------------------------------------------------------
inline NavDirType AngleToDirection( float angle )
{
	while( angle < 0.0f )
		angle += 360.0f;

	while( angle > 360.0f )
		angle -= 360.0f;

	if (angle < 45 || angle > 315)
		return EAST;

	if (angle >= 45 && angle < 135)
		return SOUTH;

	if (angle >= 135 && angle < 225)
		return WEST;

	return NORTH;
}

//--------------------------------------------------------------------------------------------------------------
inline void DirectionToVector2D( NavDirType dir, Vector2D *v )
{
	switch( dir )
	{
		case NORTH: v->x =  0.0f; v->y = -1.0f; break;
		case SOUTH: v->x =  0.0f; v->y =  1.0f; break;
		case EAST:  v->x =  1.0f; v->y =  0.0f; break;
		case WEST:  v->x = -1.0f; v->y =  0.0f; break;
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if given entity can be ignored when moving
 */
#define WALK_THRU_DOORS			0x01
#define WALK_THRU_BREAKABLES	0x02
inline bool IsEntityWalkable( CBaseEntity *entity, unsigned int flags )
{
	// if we hit a door, assume its walkable because it will open when we touch it
	if (FClassnameIs( entity, "prop_door" ) || FClassnameIs( entity, "prop_door_rotating" ))
		return (flags & WALK_THRU_DOORS) ? true : false;

	// if we hit a clip brush, ignore it if it is not BRUSHSOLID_ALWAYS
	if (FClassnameIs( entity, "func_brush" ))
	{
		CFuncBrush *brush = (CFuncBrush *)entity;
		return (brush->m_iSolidity != CFuncBrush::BRUSHSOLID_ALWAYS);
	}

	/* BOTPORT: How to determine if an entity is breakable?
	// if we hit a breakable object, assume its walkable because we will shoot it when we touch it
	if (FClassnameIs( entity, "func_breakable" ) && entity->GetHealth())
		return (flags & WALK_THRU_BREAKABLES) ? true : false;
	*/

	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Check LOS, ignoring any entities that we can walk through
 */
inline bool IsWalkableTraceLineClear( Vector &from, Vector &to, unsigned int flags = 0 )
{
	trace_t result;
	CBaseEntity *ignore = NULL;
	Vector useFrom = from;

	while(true)
	{
		UTIL_TraceLine( useFrom, to, MASK_SOLID_BRUSHONLY, ignore, COLLISION_GROUP_NONE, &result );

		// if we hit a walkable entity, try again
		if (result.fraction != 1.0f && IsEntityWalkable( result.m_pEnt, flags ))
		{
			ignore = result.m_pEnt;

			// start from just beyond where we hit to avoid infinite loops
			Vector dir = to - from;
			dir.NormalizeInPlace();
			useFrom = result.endpos + 5.0f * dir;
		}
		else
		{
			break;
		}
	}

	if (result.fraction == 1.0f)
		return true;

	return false;
}


#endif // _NAV_H_
