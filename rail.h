#ifndef RAIL_H
#define RAIL_H

#include "stdafx.h"
#include "openttd.h"
#include "tile.h"

/*
 * Some enums for accesing the map bytes for rail tiles
 */

/* These types are used in the map5 byte for rail tiles. Use GetRailTileType() to
 * get these values */
typedef enum RailTileTypes {
	RAIL_TYPE_NORMAL         = 0x0,
	RAIL_TYPE_SIGNALS        = 0x40,
	RAIL_TYPE_UNUSED         = 0x80, /* XXX: Maybe this could become waypoints? */
	RAIL_TYPE_DEPOT_WAYPOINT = 0xC0, /* Is really depots and waypoints... */
	RAIL_TILE_TYPE_MASK      = 0xC0,
} RailTileType;

enum { /* DEPRECATED TODO: Rewrite all uses of this */
	RAIL_TYPE_SPECIAL = 0x80, /* This used to say "If this bit is set, then it's
														 * not a regular track.", but currently, you
														 * should rather view map5[6..7] as one type,
														 * containing a value from RailTileTypes above.
														 * This value is only maintained for backwards
														 * compatibility */

	/* There used to be RAIL_BIT_* enums here, they moved to (for now) npf.c as
	 * TRACK_BIT_* */
};

/* These subtypes are used in the map5 byte when the main rail type is
 * RAIL_TYPE_DEPOT_WAYPOINT */
typedef enum {
	RAIL_SUBTYPE_DEPOT    = 0x00,
	RAIL_SUBTYPE_WAYPOINT = 0x04,
	RAIL_SUBTYPE_MASK     = 0x3C,
} RailTileSubtype;

typedef enum {
	/* Stored in _map3_hi[0..1] for MP_RAILWAY */
  SIGTYPE_NORMAL  = 0,        // normal signal
  SIGTYPE_ENTRY   = 1,        // presignal block entry
  SIGTYPE_EXIT    = 2,        // presignal block exit
  SIGTYPE_COMBO   = 3,        // presignal inter-block
	SIGTYPE_END,
	SIGTYPE_MASK    = 3,
} SignalType;

typedef enum {
	RAILTYPE_RAIL   = 0,
	RAILTYPE_MONO   = 1,
	RAILTYPE_MAGLEV = 2,
	RAILTYPE_END,
	RAILTYPE_MASK   = 0x3,
	INVALID_RAILTYPE = 0xFF,
} RailType;

enum {
	SIG_SEMAPHORE_MASK = 4,
};

/* These are used to specify a single track. Can be translated to a trackbit
 * with TrackToTrackbit */
typedef enum {
  TRACK_DIAG1 = 0,
  TRACK_DIAG2 = 1,
  TRACK_UPPER = 2,
  TRACK_LOWER = 3,
  TRACK_LEFT  = 4,
  TRACK_RIGHT = 5,
  TRACK_END,
  INVALID_TRACK = 0xFF,
} Track;

/* These are the bitfield variants of the above */
typedef enum {
  TRACK_BIT_DIAG1 = 1,  // 0
  TRACK_BIT_DIAG2 = 2,  // 1
  TRACK_BIT_UPPER = 4,  // 2
  TRACK_BIT_LOWER = 8,  // 3
  TRACK_BIT_LEFT  = 16, // 4
  TRACK_BIT_RIGHT = 32, // 5
	TRACK_BIT_MASK  = 0x3F,
} TrackBits;

/* These are a combination of tracks and directions. Values are 0-5 in one
direction (corresponding to the Track enum) and 8-13 in the other direction. */
typedef enum {
  TRACKDIR_DIAG1_NE = 0,
  TRACKDIR_DIAG2_SE = 1,
  TRACKDIR_UPPER_E  = 2,
  TRACKDIR_LOWER_E  = 3,
  TRACKDIR_LEFT_S   = 4,
  TRACKDIR_RIGHT_S  = 5,
	/* Note the two missing values here. This enables trackdir -> track conversion by doing (trackdir & 7) */
  TRACKDIR_DIAG1_SW = 8,
  TRACKDIR_DIAG2_NW = 9,
  TRACKDIR_UPPER_W  = 10,
  TRACKDIR_LOWER_W  = 11,
  TRACKDIR_LEFT_N   = 12,
  TRACKDIR_RIGHT_N  = 13,
  TRACKDIR_END,
  INVALID_TRACKDIR  = 0xFF,
} Trackdir;

/* These are a combination of tracks and directions. Values are 0-5 in one
direction (corresponding to the Track enum) and 8-13 in the other direction. */
typedef enum {
  TRACKDIR_BIT_DIAG1_NE = 0x1,
  TRACKDIR_BIT_DIAG2_SE = 0x2,
  TRACKDIR_BIT_UPPER_E  = 0x4,
  TRACKDIR_BIT_LOWER_E  = 0x8,
  TRACKDIR_BIT_LEFT_S   = 0x10,
  TRACKDIR_BIT_RIGHT_S  = 0x20,
	/* Again, note the two missing values here. This enables trackdir -> track conversion by doing (trackdir & 0xFF) */
  TRACKDIR_BIT_DIAG1_SW = 0x0100,
  TRACKDIR_BIT_DIAG2_NW = 0x0200,
  TRACKDIR_BIT_UPPER_W  = 0x0400,
  TRACKDIR_BIT_LOWER_W  = 0x0800,
  TRACKDIR_BIT_LEFT_N   = 0x1000,
  TRACKDIR_BIT_RIGHT_N  = 0x2000,
	TRACKDIR_BIT_MASK			= 0x3F3F,
  INVALID_TRACKDIR_BIT  = 0xFFFF,
} TrackdirBits;

/* These are states in which a signal can be. Currently these are only two, so
 * simple boolean logic will do. But do try to compare to this enum instead of
 * normal boolean evaluation, since that will make future additions easier.
 */
typedef enum {
	SIGNALSTATE_RED = 0,
	SIGNALSTATE_GREEN = 1,
} SignalState;


/*
 * These functions check the validity of Tracks and Trackdirs. assert against
 * them when convenient.
 */
static inline bool IsValidTrack(Track track) { return track < TRACK_END; }
static inline bool IsValidTrackdir(Trackdir trackdir) { return trackdir < TRACKDIR_END; }

/*
 * Functions to map tracks to the corresponding bits in the signal
 * presence/status bytes in the map. You should not use these directly, but
 * wrapper functions below instead. XXX: Which are these?
 */

/**
 * Maps a trackdir to the bit that stores its status in the map arrays, in the
 * direction along with the trackdir.
 */
const byte _signal_along_trackdir[TRACKDIR_END];
static inline byte SignalAlongTrackdir(Trackdir trackdir) {return _signal_along_trackdir[trackdir];}

/**
 * Maps a trackdir to the bit that stores its status in the map arrays, in the
 * direction against the trackdir.
 */
const byte _signal_against_trackdir[TRACKDIR_END];
static inline byte SignalAgainstTrackdir(Trackdir trackdir) { return _signal_against_trackdir[trackdir]; }

/**
 * Maps a Track to the bits that store the status of the two signals that can
 * be present on the given track.
 */
const byte _signal_on_track[TRACK_END];
static inline byte SignalOnTrack(Track track) { return _signal_on_track[track]; }

/*
 * Some functions to query rail tiles
 */

/**
 * Returns the RailTileType of a given rail tile. (ie normal, with signals,
 * depot, etc.)
 */
static inline RailTileType GetRailTileType(TileIndex tile)
{
	assert(IsTileType(tile, MP_RAILWAY));
	return (_map5[tile] & RAIL_TILE_TYPE_MASK);
}

/**
 * Returns the rail type of the given rail tile (ie rail, mono, maglev).
 */
static inline RailType GetRailType(TileIndex tile) { return _map3_lo[tile] & RAILTYPE_MASK; }

/**
 * Checks if a rail tile has signals.
 */
static inline bool HasSignals(TileIndex tile)
{
	return GetRailTileType(tile) == RAIL_TYPE_SIGNALS;
}

/**
 * Returns the RailTileSubtype of a given rail tile with type
 * RAIL_TYPE_DEPOT_WAYPOINT
 */
static inline RailTileSubtype GetRailTileSubtype(TileIndex tile)
{
	assert(GetRailTileType(tile) == RAIL_TYPE_DEPOT_WAYPOINT);
	return _map5[tile] & RAIL_SUBTYPE_MASK;
}

/**
 * Returns whether this is plain rails, with or without signals. Iow, if this
 * tiles RailTileType is RAIL_TYPE_NORMAL or RAIL_TYPE_SIGNALS.
 */
static inline bool IsPlainRailTile(TileIndex tile)
{
	RailTileType rtt = GetRailTileType(tile);
	return rtt == RAIL_TYPE_NORMAL || rtt == RAIL_TYPE_SIGNALS;
}

/**
 * Returns the tracks present on the given plain rail tile (IsPlainRailTile())
 */
static inline TrackBits GetTrackBits(TileIndex tile)
{
	assert(GetRailTileType(tile) == RAIL_TYPE_NORMAL || GetRailTileType(tile) == RAIL_TYPE_SIGNALS);
	return _map5[tile] & TRACK_BIT_MASK;
}

/**
 * Returns whether the given track is present on the given tile. Tile must be
 * a plain rail tile (IsPlainRailTile()).
 */
static inline bool HasTrack(TileIndex tile, Track track)
{
	assert(IsValidTrack(track));
	return HASBIT(GetTrackBits(tile), track);
}

/*
 * Functions describing logical relations between Tracks, TrackBits, Trackdirs
 * TrackdirBits, Direction and DiagDirections.
 *
 * TODO: Add #unndefs or something similar to remove the arrays used below
 * from the global scope and expose direct uses of them.
 */

/**
 * Maps a trackdir to the trackdir that you will end up on if you go straight
 * ahead. This will be the same trackdir for diagonal trackdirs, but a
 * different (alternating) one for straight trackdirs
 */
const Trackdir _next_trackdir[TRACKDIR_END];
static inline Trackdir NextTrackdir(Trackdir trackdir) { return _next_trackdir[trackdir]; }

/**
 * Maps a track to all tracks that make 90 deg turns with it.
 */
const TrackBits _track_crosses_tracks[TRACK_END];
static inline TrackBits TrackCrossesTracks(Track track) { return _track_crosses_tracks[track]; }

/**
 * Maps a trackdir to the (4-way) direction the tile is exited when following
 * that trackdir.
 */
const DiagDirection _trackdir_to_exitdir[TRACKDIR_END];
static inline DiagDirection TrackdirToExitdir(Trackdir trackdir) { return _trackdir_to_exitdir[trackdir]; }

/**
 * Maps a track and an (4-way) dir to the trackdir that represents the track
 * with the exit in the given direction.
 */
const Trackdir _track_exitdir_to_trackdir[TRACK_END][DIAGDIR_END];
static inline Trackdir TrackExitdirToTrackdir(Track track, DiagDirection diagdir) { return _track_exitdir_to_trackdir[track][diagdir]; }

/**
 * Maps a track and a full (8-way) direction to the trackdir that represents
 * the track running in the given direction.
 */
const Trackdir _track_direction_to_trackdir[TRACK_END][DIR_END];
static inline Trackdir TrackDirectionToTrackdir(Track track, Direction dir) { return _track_direction_to_trackdir[track][dir]; }

/**
 * Maps a (4-way) direction to the diagonal trackdir that runs in that
 * direction.
 */
const Trackdir _dir_to_diag_trackdir[DIAGDIR_END];
static inline Trackdir DiagdirToDiagTrackdir(DiagDirection diagdir) { return _dir_to_diag_trackdir[diagdir]; }

/**
 * Maps a trackdir to the trackdirs that can be reached from it (ie, when
 * entering the next tile. This
 */
const TrackdirBits _exitdir_reaches_trackdirs[DIAGDIR_END];
/* Note that there is no direct table for this function (there used to be),
 * but it uses two simpeler tables to achieve the result */
static inline TrackdirBits TrackdirReachesTrackdirs(Trackdir trackdir) { return _exitdir_reaches_trackdirs[TrackdirToExitdir(trackdir)]; }

/**
 * Maps a trackdir to all trackdirs that make 90 deg turns with it.
 */
const TrackdirBits _trackdir_crosses_trackdirs[TRACKDIR_END];
static inline TrackdirBits TrackdirCrossesTrackdirs(Trackdir trackdir) { return _trackdir_crosses_trackdirs[trackdir]; }

/**
 * Maps a (4-way) direction to the reverse.
 */
const DiagDirection _reverse_diagdir[DIAGDIR_END];
static inline DiagDirection ReverseDiagdir(DiagDirection diagdir) { return _reverse_diagdir[diagdir]; }

/**
 * Maps a trackdir to the reverse trackdir.
 */
const Trackdir _reverse_trackdir[TRACKDIR_END];
static inline Trackdir ReverseTrackdir(Trackdir trackdir) { return _reverse_trackdir[trackdir]; }

/**
 * Maps a Trackdir to the corresponding TrackdirBits value
 */
static inline TrackdirBits TrackdirToTrackdirBits(Trackdir trackdir) { return 1 << trackdir; }

/*
 * Maps a Track to the corresponding TrackBits value
 */
static inline TrackBits TrackToTrackBits(Track track) { return 1 << track; }

/* Returns the Track that a given Trackdir represents */
static inline Track TrackdirToTrack(Trackdir trackdir) { return trackdir & 0x7; }

/* Returns a Trackdir for the given Track. Since every Track corresponds to
 * two Trackdirs, we choose the one which points between N and SE.
 * Note that the actual implementation is quite futile, but this might change
 * in the future.
 */
static inline Trackdir TrackToTrackdir(Track track) { return track; }

/* Checks if a given Track is diagonal */
static inline bool IsDiagonalTrack(Track track) { return track == TRACK_DIAG1 || track == TRACK_DIAG2; }

/* Checks if a given Trackdir is diagonal. */
static inline bool IsDiagonalTrackdir(Trackdir trackdir) { return IsDiagonalTrack(TrackdirToTrack(trackdir)); }

/*
 * Functions quering signals on tiles.
 */

/**
 * Checks for the presence of signals on the given track on the given tile
 */
static inline bool HasSignalOnTrack(TileIndex tile, Track track)
{
	assert(IsValidTrack(track));
	return (GetRailTileType(tile) == RAIL_TYPE_SIGNALS && (_map3_lo[tile] & SignalOnTrack(track)));
}

/**
 * Gets the state of the signal along the given trackdir.
 *
 * Along meaning if you are currently driving on the given trackdir, this is
 * the signal that is facing us (for which we stop when it's red).
 */
static inline SignalState GetSignalState(TileIndex tile, Trackdir trackdir)
{
	assert(IsValidTrackdir(trackdir));
	assert(HasSignalOnTrack(tile, TrackdirToTrack(trackdir)));
	return ((_map2[tile] & SignalAlongTrackdir(trackdir))?SIGNALSTATE_GREEN:SIGNALSTATE_RED);
}

/**
 * Gets the type of signal on a given track on a given rail tile with signals.
 *
 * Note that currently, the track argument is not used, since
 * signal types cannot be mixed. This function is trying to be
 * future-compatible, though.
 */
static inline SignalType GetSignalType(TileIndex tile, Track track)
{
	assert(IsValidTrack(track));
	assert(GetRailTileType(tile) == RAIL_TYPE_SIGNALS);
	return _map3_hi[tile] & SIGTYPE_MASK;
}

/**
 * Checks if this tile contains semaphores (returns true) or normal signals
 * (returns false) on the given track. Does not check if there are actually
 * signals on the track, you should use HasSignalsOnTrack() for that.
 *
 * Note that currently, the track argument is not used, since
 * semaphores/electric signals cannot be mixed. This function is trying to be
 * future-compatible, though.
 */
static inline bool HasSemaphores(TileIndex tile, Track track)
{
	assert(IsValidTrack(track));
	return _map3_hi[tile] & SIG_SEMAPHORE_MASK;
}

#endif // RAIL_H
