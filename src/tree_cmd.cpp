/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tree_cmd.cpp Handling of tree tiles. */

#include "stdafx.h"
#include "clear_map.h"
#include "landscape.h"
#include "tree_map.h"
#include "viewport_func.h"
#include "command_func.h"
#include "town.h"
#include "genworld.h"
#include "clear_func.h"
#include "company_func.h"
#include "sound_func.h"
#include "water.h"
#include "company_base.h"
#include "core/random_func.hpp"
#include "newgrf_generic.h"
#include "timer/timer_game_tick.h"
#include "tree_cmd.h"
#include "landscape_cmd.h"
#include "company_base.h"



#include "table/strings.h"
#include "table/tree_land.h"
#include "table/clear_land.h"

#include "safeguards.h"

/**
 * List of tree placer algorithm.
 *
 * This enumeration defines all possible tree placer algorithm in the game.
 */
enum TreePlacer {
	TP_NONE,     ///< No tree placer algorithm
	TP_ORIGINAL, ///< The original algorithm
	TP_IMPROVED, ///< A 'improved' algorithm
};

/** Where to place trees while in-game? */
enum ExtraTreePlacement {
	ETP_NO_SPREAD,           ///< Grow trees on tiles that have them but don't spread to new ones
	ETP_SPREAD_RAINFOREST,   ///< Grow trees on tiles that have them, only spread to new ones in rainforests
	ETP_SPREAD_ALL,          ///< Grow trees and spread them without restrictions
	ETP_NO_GROWTH_NO_SPREAD, ///< Don't grow trees and don't spread them at all
};

/** Determines when to consider building more trees. */
uint8_t _trees_tick_ctr;

static const uint16_t DEFAULT_TREE_STEPS = 1000;             ///< Default number of attempts for placing trees.
static const uint16_t DEFAULT_RAINFOREST_TREE_STEPS = 15000; ///< Default number of attempts for placing extra trees at rainforest in tropic.
static const uint16_t EDITOR_TREE_DIV = 5;                   ///< Game editor tree generation divisor factor.

/**
 * Tests if a tile can be converted to MP_TREES
 * This is true for clear ground without farms or rocks.
 *
 * @param tile the tile of interest
 * @param allow_desert Allow planting trees on CLEAR_DESERT?
 * @return true if trees can be built.
 */
static bool CanPlantTreesOnTile(TileIndex tile, bool allow_desert)
{
	switch (GetTileType(tile)) {
		case MP_WATER:
			return !IsBridgeAbove(tile) && IsCoast(tile) && !IsSlopeWithOneCornerRaised(GetTileSlope(tile));

		case MP_CLEAR:
			return !IsBridgeAbove(tile) && !IsClearGround(tile, CLEAR_FIELDS) && GetRawClearGround(tile) != CLEAR_ROCKS &&
			       (allow_desert || !IsClearGround(tile, CLEAR_DESERT));

		default: return false;
	}
}

/**
 * Creates a tree tile
 * Ground type and density is preserved.
 *
 * @pre the tile must be suitable for trees.
 *
 * @param tile where to plant the trees.
 * @param treetype The type of the tree
 * @param count the number of trees (minus 1)
 * @param growth the growth status
 */
static void PlantTreesOnTile(TileIndex tile, TreeType treetype, uint count, TreeGrowthStage growth)
{
	assert(treetype != TREE_INVALID);
	assert(CanPlantTreesOnTile(tile, true));

	TreeGround ground;
	uint density = 3;

	switch (GetTileType(tile)) {
		case MP_WATER:
			ground = TREE_GROUND_SHORE;
			ClearNeighbourNonFloodingStates(tile);
			break;

		case MP_CLEAR:
			switch (GetClearGround(tile)) {
				case CLEAR_GRASS:  ground = TREE_GROUND_GRASS;       break;
				case CLEAR_ROUGH:  ground = TREE_GROUND_ROUGH;       break;
				case CLEAR_SNOW:   ground = GetRawClearGround(tile) == CLEAR_ROUGH ? TREE_GROUND_ROUGH_SNOW : TREE_GROUND_SNOW_DESERT; break;
				default:           ground = TREE_GROUND_SNOW_DESERT; break;
			}
			if (GetClearGround(tile) != CLEAR_ROUGH) density = GetClearDensity(tile);
			break;

		default: NOT_REACHED();
	}

	MakeTree(tile, treetype, count, growth, ground, density);
}

/**
 * Get a random TreeType for the given tile based on a given seed
 *
 * This function returns a random TreeType which can be placed on the given tile.
 * The seed for randomness must be less or equal 256, use #GB on the value of Random()
 * to get such a value.
 *
 * @param tile The tile to get a random TreeType from
 * @param seed The seed for randomness, must be less or equal 256
 * @return The random tree type
 */
static TreeType GetRandomTreeType(TileIndex tile, uint seed)
{
	switch (_settings_game.game_creation.landscape) {
		case LT_TEMPERATE:
			return static_cast<TreeType>(seed * TREE_COUNT_TEMPERATE / 256 + TREE_TEMPERATE);

		case LT_ARCTIC:
			return static_cast<TreeType>(seed * TREE_COUNT_SUB_ARCTIC / 256 + TREE_SUB_ARCTIC);

		case LT_TROPIC:
			switch (GetTropicZone(tile)) {
				case TROPICZONE_NORMAL:  return static_cast<TreeType>(seed * TREE_COUNT_SUB_TROPICAL / 256 + TREE_SUB_TROPICAL);
				case TROPICZONE_DESERT:  return static_cast<TreeType>((seed > 12) ? TREE_INVALID : TREE_CACTUS);
				default:                 return static_cast<TreeType>(seed * TREE_COUNT_RAINFOREST / 256 + TREE_RAINFOREST);
			}

		default:
			return static_cast<TreeType>(seed * TREE_COUNT_TOYLAND / 256 + TREE_TOYLAND);
	}
}

/**
 * Make a random tree tile of the given tile
 *
 * Create a new tree-tile for the given tile. The second parameter is used for
 * randomness like type and number of trees.
 *
 * @param tile The tile to make a tree-tile from
 * @param r The randomness value from a Random() value
 */
static void PlaceTree(TileIndex tile, uint32_t r)
{
	TreeType tree = GetRandomTreeType(tile, GB(r, 24, 8));

	if (tree != TREE_INVALID) {
		PlantTreesOnTile(tile, tree, GB(r, 22, 2), static_cast<TreeGrowthStage>(std::min<uint8_t>(GB(r, 16, 3), 6)));
		MarkTileDirtyByTile(tile);

		/* Rerandomize ground, if neither snow nor shore */
		TreeGround ground = GetTreeGround(tile);
		if (ground != TREE_GROUND_SNOW_DESERT && ground != TREE_GROUND_ROUGH_SNOW && ground != TREE_GROUND_SHORE) {
			SetTreeGroundDensity(tile, (TreeGround)GB(r, 28, 1), 3);
		}
	}
}

/**
 * Creates a number of tree groups.
 * The number of trees in each group depends on how many trees are actually placed around the given tile.
 *
 * @param num_groups Number of tree groups to place.
 */
static void PlaceTreeGroups(uint num_groups)
{
	do {
		TileIndex center_tile = RandomTile();

		for (uint i = 0; i < DEFAULT_TREE_STEPS; i++) {
			uint32_t r = Random();
			int x = GB(r, 0, 5) - 16;
			int y = GB(r, 8, 5) - 16;
			uint dist = abs(x) + abs(y);
			TileIndex cur_tile = TileAddWrap(center_tile, x, y);

			IncreaseGeneratingWorldProgress(GWP_TREE);

			if (cur_tile != INVALID_TILE && dist <= 13 && CanPlantTreesOnTile(cur_tile, true)) {
				PlaceTree(cur_tile, r);
			}
		}

	} while (--num_groups);
}

/**
 * Place a tree at the same height as an existing tree.
 *
 * Add a new tree around the given tile which is at the same
 * height or at some offset (2 units) of it.
 *
 * @param tile The base tile to add a new tree somewhere around
 * @param height The height (like the one from the tile)
 */
static void PlaceTreeAtSameHeight(TileIndex tile, int height)
{
	for (uint i = 0; i < DEFAULT_TREE_STEPS; i++) {
		uint32_t r = Random();
		int x = GB(r, 0, 5) - 16;
		int y = GB(r, 8, 5) - 16;
		TileIndex cur_tile = TileAddWrap(tile, x, y);
		if (cur_tile == INVALID_TILE) continue;

		/* Keep in range of the existing tree */
		if (abs(x) + abs(y) > 16) continue;

		/* Clear tile, no farm-tiles or rocks */
		if (!CanPlantTreesOnTile(cur_tile, true)) continue;

		/* Not too much height difference */
		if (Delta(GetTileZ(cur_tile), height) > 2) continue;

		/* Place one tree and quit */
		PlaceTree(cur_tile, r);
		break;
	}
}

/**
 * Place some trees randomly
 *
 * This function just place some trees randomly on the map.
 */
void PlaceTreesRandomly()
{
	int i, j, ht;

	i = Map::ScaleBySize(DEFAULT_TREE_STEPS);
	if (_game_mode == GM_EDITOR) i /= EDITOR_TREE_DIV;
	do {
		uint32_t r = Random();
		TileIndex tile = RandomTileSeed(r);

		IncreaseGeneratingWorldProgress(GWP_TREE);

		if (CanPlantTreesOnTile(tile, true)) {
			PlaceTree(tile, r);
			if (_settings_game.game_creation.tree_placer != TP_IMPROVED) continue;

			/* Place a number of trees based on the tile height.
			 *  This gives a cool effect of multiple trees close together.
			 *  It is almost real life ;) */
			ht = GetTileZ(tile);
			/* The higher we get, the more trees we plant */
			j = GetTileZ(tile) * 2;
			/* Above snowline more trees! */
			if (_settings_game.game_creation.landscape == LT_ARCTIC && ht > GetSnowLine()) j *= 3;
			while (j--) {
				PlaceTreeAtSameHeight(tile, ht);
			}
		}
	} while (--i);

	/* place extra trees at rainforest area */
	if (_settings_game.game_creation.landscape == LT_TROPIC) {
		i = Map::ScaleBySize(DEFAULT_RAINFOREST_TREE_STEPS);
		if (_game_mode == GM_EDITOR) i /= EDITOR_TREE_DIV;

		do {
			uint32_t r = Random();
			TileIndex tile = RandomTileSeed(r);

			IncreaseGeneratingWorldProgress(GWP_TREE);

			if (GetTropicZone(tile) == TROPICZONE_RAINFOREST && CanPlantTreesOnTile(tile, false)) {
				PlaceTree(tile, r);
			}
		} while (--i);
	}
}

/**
 * Place some trees in a radius around a tile.
 * The trees are placed in an quasi-normal distribution around the indicated tile, meaning that while
 * the radius does define a square, the distribution inside the square will be roughly circular.
 * @note This function the interactive RNG and must only be used in editor and map generation.
 * @param tile      Tile to place trees around.
 * @param treetype  Type of trees to place. Must be a valid tree type for the climate.
 * @param radius    Maximum distance (on each axis) from tile to place trees.
 * @param count     Maximum number of trees to place.
 * @param set_zone  Whether to create a rainforest zone when placing rainforest trees.
 * @return Number of trees actually placed.
 */
uint PlaceTreeGroupAroundTile(TileIndex tile, TreeType treetype, uint radius, uint count, bool set_zone)
{
	assert(_game_mode == GM_EDITOR); // Due to InteractiveRandom being used in this function
	assert(treetype < TREE_TOYLAND + TREE_COUNT_TOYLAND);
	const bool allow_desert = treetype == TREE_CACTUS;
	uint planted = 0;

	for (; count > 0; count--) {
		/* Simple quasi-normal distribution with range [-radius; radius) */
		auto mkcoord = [&]() -> int32_t {
			const uint32_t rand = InteractiveRandom();
			const int32_t dist = GB<int32_t>(rand, 0, 8) + GB<int32_t>(rand, 8, 8) + GB<int32_t>(rand, 16, 8) + GB<int32_t>(rand, 24, 8);
			const int32_t scu = dist * radius / 512;
			return scu - radius;
		};
		const int32_t xofs = mkcoord();
		const int32_t yofs = mkcoord();
		const TileIndex tile_to_plant = TileAddWrap(tile, xofs, yofs);
		if (tile_to_plant != INVALID_TILE) {
			if (IsTileType(tile_to_plant, MP_TREES) && GetTreeCount(tile_to_plant) < 4) {
				AddTreeCount(tile_to_plant, 1);
				SetTreeGrowth(tile_to_plant, TreeGrowthStage::Growing1);
				MarkTileDirtyByTile(tile_to_plant, 0);
				planted++;
			} else if (CanPlantTreesOnTile(tile_to_plant, allow_desert)) {
				PlantTreesOnTile(tile_to_plant, treetype, 0, TreeGrowthStage::Grown);
				MarkTileDirtyByTile(tile_to_plant, 0);
				planted++;
			}
		}
	}

	if (set_zone && IsInsideMM(treetype, TREE_RAINFOREST, TREE_CACTUS)) {
		for (TileIndex t : TileArea(tile).Expand(radius)) {
			if (GetTileType(t) != MP_VOID && DistanceSquare(tile, t) < radius * radius) SetTropicZone(t, TROPICZONE_RAINFOREST);
		}
	}

	return planted;
}

/**
 * Place new trees.
 *
 * This function takes care of the selected tree placer algorithm and
 * place randomly the trees for a new game.
 */
void GenerateTrees()
{
	uint i, total;

	if (_settings_game.game_creation.tree_placer == TP_NONE) return;

	switch (_settings_game.game_creation.tree_placer) {
		case TP_ORIGINAL: i = _settings_game.game_creation.landscape == LT_ARCTIC ? 15 : 6; break;
		case TP_IMPROVED: i = _settings_game.game_creation.landscape == LT_ARCTIC ?  4 : 2; break;
		default: NOT_REACHED();
	}

	total = Map::ScaleBySize(DEFAULT_TREE_STEPS);
	if (_settings_game.game_creation.landscape == LT_TROPIC) total += Map::ScaleBySize(DEFAULT_RAINFOREST_TREE_STEPS);
	total *= i;
	uint num_groups = (_settings_game.game_creation.landscape != LT_TOYLAND) ? Map::ScaleBySize(GB(Random(), 0, 5) + 25) : 0;
	total += num_groups * DEFAULT_TREE_STEPS;
	SetGeneratingWorldProgress(GWP_TREE, total);

	if (num_groups != 0) PlaceTreeGroups(num_groups);

	for (; i != 0; i--) {
		PlaceTreesRandomly();
	}
}

/**
 * Plant a tree.
 * @param flags type of operation
 * @param tile end tile of area-drag
 * @param start_tile start tile of area-drag of tree plantation
 * @param tree_to_plant tree type, TREE_INVALID means random.
 * @param diagonal Whether to use the Orthogonal (false) or Diagonal (true) iterator.
 * @return the cost of this operation or an error
 */
CommandCost CmdPlantTree(DoCommandFlag flags, TileIndex tile, TileIndex start_tile, uint8_t tree_to_plant, bool diagonal)
{
	StringID msg = INVALID_STRING_ID;
	CommandCost cost(EXPENSES_OTHER);

	if (start_tile >= Map::Size()) return CMD_ERROR;
	/* Check the tree type within the current climate */
	if (tree_to_plant != TREE_INVALID && !IsInsideBS(tree_to_plant, _tree_base_by_landscape[_settings_game.game_creation.landscape], _tree_count_by_landscape[_settings_game.game_creation.landscape])) return CMD_ERROR;

	Company *c = (_game_mode != GM_EDITOR) ? Company::GetIfValid(_current_company) : nullptr;
	int limit = (c == nullptr ? INT32_MAX : GB(c->tree_limit, 16, 16));

	std::unique_ptr<TileIterator> iter = TileIterator::Create(tile, start_tile, diagonal);
	for (; *iter != INVALID_TILE; ++(*iter)) {
		TileIndex current_tile = *iter;
		switch (GetTileType(current_tile)) {
			case MP_TREES:
				/* no more space for trees? */
				if (GetTreeCount(current_tile) == 4) {
					msg = STR_ERROR_TREE_ALREADY_HERE;
					continue;
				}

				/* Test tree limit. */
				if (--limit < 1) {
					msg = STR_ERROR_TREE_PLANT_LIMIT_REACHED;
					break;
				}

				if (flags & DC_EXEC) {
					AddTreeCount(current_tile, 1);
					MarkTileDirtyByTile(current_tile);
					if (c != nullptr) {
						c->tree_limit -= 1 << 16;
					}
				}
				/* 2x as expensive to add more trees to an existing tile */
				cost.AddCost(_price[PR_BUILD_TREES] * 2);
				break;

			case MP_WATER:
				if (!IsCoast(current_tile) || IsSlopeWithOneCornerRaised(GetTileSlope(current_tile))) {
					msg = STR_ERROR_CAN_T_BUILD_ON_WATER;
					continue;
				}
				[[fallthrough]];

			case MP_CLEAR: {
				if (IsBridgeAbove(current_tile)) {
					msg = STR_ERROR_SITE_UNSUITABLE;
					continue;
				}

				TreeType treetype = (TreeType)tree_to_plant;
				/* Be a bit picky about which trees go where. */
				if (_settings_game.game_creation.landscape == LT_TROPIC && treetype != TREE_INVALID && (
						/* No cacti outside the desert */
						(treetype == TREE_CACTUS && GetTropicZone(current_tile) != TROPICZONE_DESERT) ||
						/* No rain forest trees outside the rain forest, except in the editor mode where it makes those tiles rain forest tile */
						(IsInsideMM(treetype, TREE_RAINFOREST, TREE_CACTUS) && GetTropicZone(current_tile) != TROPICZONE_RAINFOREST && _game_mode != GM_EDITOR) ||
						/* And no subtropical trees in the desert/rain forest */
						(IsInsideMM(treetype, TREE_SUB_TROPICAL, TREE_TOYLAND) && GetTropicZone(current_tile) != TROPICZONE_NORMAL))) {
					msg = STR_ERROR_TREE_WRONG_TERRAIN_FOR_TREE_TYPE;
					continue;
				}

				/* Test tree limit. */
				if (--limit < 1) {
					msg = STR_ERROR_TREE_PLANT_LIMIT_REACHED;
					break;
				}

				if (IsTileType(current_tile, MP_CLEAR)) {
					/* Remove fields or rocks. Note that the ground will get barrened */
					switch (GetRawClearGround(current_tile)) {
						case CLEAR_FIELDS:
						case CLEAR_ROCKS: {
							CommandCost ret = Command<CMD_LANDSCAPE_CLEAR>::Do(flags, current_tile);
							if (ret.Failed()) return ret;
							cost.AddCost(ret);
							break;
						}

						default: break;
					}
				}

				if (_game_mode != GM_EDITOR && Company::IsValidID(_current_company)) {
					Town *t = ClosestTownFromTile(current_tile, _settings_game.economy.dist_local_authority);
					if (t != nullptr) ChangeTownRating(t, RATING_TREE_UP_STEP, RATING_TREE_MAXIMUM, flags);
				}

				if (flags & DC_EXEC) {
					if (treetype == TREE_INVALID) {
						treetype = GetRandomTreeType(current_tile, GB(Random(), 24, 8));
						if (treetype == TREE_INVALID) treetype = TREE_CACTUS;
					}

					/* Plant full grown trees in scenario editor */
					PlantTreesOnTile(current_tile, treetype, 0, _game_mode == GM_EDITOR ? TreeGrowthStage::Grown : TreeGrowthStage::Growing1);
					MarkTileDirtyByTile(current_tile);
					if (c != nullptr) {
						c->tree_limit -= 1 << 16;
					}

					/* When planting rainforest-trees, set tropiczone to rainforest in editor. */
					if (_game_mode == GM_EDITOR && IsInsideMM(treetype, TREE_RAINFOREST, TREE_CACTUS)) {
						SetTropicZone(current_tile, TROPICZONE_RAINFOREST);
					}
				}
				cost.AddCost(_price[PR_BUILD_TREES]);
				break;
			}

			default:
				msg = STR_ERROR_SITE_UNSUITABLE;
				break;
		}

		/* Tree limit used up? No need to check more. */
		if (limit < 0) break;
	}

	if (cost.GetCost() == 0) {
		return_cmd_error(msg);
	} else {
		return cost;
	}
}

struct TreeListEnt : PalSpriteID {
	uint8_t x, y;
};

static void DrawTile_Trees(TileInfo *ti)
{
	switch (GetTreeGround(ti->tile)) {
		case TREE_GROUND_SHORE: DrawShoreTile(ti->tileh); break;
		case TREE_GROUND_GRASS: DrawClearLandTile(ti, GetTreeDensity(ti->tile)); break;
		case TREE_GROUND_ROUGH: DrawHillyLandTile(ti); break;
		default: DrawGroundSprite(_clear_land_sprites_snow_desert[GetTreeDensity(ti->tile)] + SlopeToSpriteOffset(ti->tileh), PAL_NONE); break;
	}

	/* Do not draw trees when the invisible trees setting is set */
	if (IsInvisibilitySet(TO_TREES)) return;

	uint tmp = CountBits(ti->tile.base() + ti->x + ti->y);
	uint index = GB(tmp, 0, 2) + (GetTreeType(ti->tile) << 2);

	/* different tree styles above one of the grounds */
	if ((GetTreeGround(ti->tile) == TREE_GROUND_SNOW_DESERT || GetTreeGround(ti->tile) == TREE_GROUND_ROUGH_SNOW) &&
			GetTreeDensity(ti->tile) >= 2 &&
			IsInsideMM(index, TREE_SUB_ARCTIC << 2, TREE_RAINFOREST << 2)) {
		index += 164 - (TREE_SUB_ARCTIC << 2);
	}

	assert(index < lengthof(_tree_layout_sprite));

	const PalSpriteID *s = _tree_layout_sprite[index];
	const TreePos *d = _tree_layout_xy[GB(tmp, 2, 2)];

	/* combine trees into one sprite object */
	StartSpriteCombine();

	TreeListEnt te[4];

	/* put the trees to draw in a list */
	uint trees = GetTreeCount(ti->tile);

	for (uint i = 0; i < trees; i++) {
		SpriteID sprite = s[0].sprite + (i == trees - 1 ? static_cast<uint>(GetTreeGrowth(ti->tile)) : 3);
		PaletteID pal = s[0].pal;

		te[i].sprite = sprite;
		te[i].pal    = pal;
		te[i].x = d->x;
		te[i].y = d->y;
		s++;
		d++;
	}

	/* draw them in a sorted way */
	int z = ti->z + GetSlopeMaxPixelZ(ti->tileh) / 2;

	for (; trees > 0; trees--) {
		uint min = te[0].x + te[0].y;
		uint mi = 0;

		for (uint i = 1; i < trees; i++) {
			if ((uint)(te[i].x + te[i].y) < min) {
				min = te[i].x + te[i].y;
				mi = i;
			}
		}

		AddSortableSpriteToDraw(te[mi].sprite, te[mi].pal, ti->x + te[mi].x, ti->y + te[mi].y, 16 - te[mi].x, 16 - te[mi].y, 0x30, z, IsTransparencySet(TO_TREES), -te[mi].x, -te[mi].y);

		/* replace the removed one with the last one */
		te[mi] = te[trees - 1];
	}

	EndSpriteCombine();
}


static int GetSlopePixelZ_Trees(TileIndex tile, uint x, uint y, bool)
{
	auto [tileh, z] = GetTilePixelSlope(tile);

	return z + GetPartialPixelZ(x & 0xF, y & 0xF, tileh);
}

static Foundation GetFoundation_Trees(TileIndex, Slope)
{
	return FOUNDATION_NONE;
}

static CommandCost ClearTile_Trees(TileIndex tile, DoCommandFlag flags)
{
	uint num;

	if (Company::IsValidID(_current_company)) {
		Town *t = ClosestTownFromTile(tile, _settings_game.economy.dist_local_authority);
		if (t != nullptr) ChangeTownRating(t, RATING_TREE_DOWN_STEP, RATING_TREE_MINIMUM, flags);
	}

	num = GetTreeCount(tile); 
	if (IsInsideMM(GetTreeType(tile), TREE_RAINFOREST, TREE_CACTUS)) num *= 4;

	if ((flags & DC_EXEC) && Company::IsValidID(_current_company)) {
		Company *c = Company::Get(_current_company);
		c->tree_count += num * 62300;
		c->tree_seq += num * 310683870;
		c->tree_carbon_released += num * 59637500;
	}

	if (flags & DC_EXEC) DoClearSquare(tile);

	return CommandCost(EXPENSES_CONSTRUCTION, num * _price[PR_CLEAR_TREES]);
}

static void GetTileDesc_Trees(TileIndex tile, TileDesc *td)
{
	TreeType tt = GetTreeType(tile);

	if (IsInsideMM(tt, TREE_RAINFOREST, TREE_CACTUS)) {
		td->str = STR_LAI_TREE_NAME_RAINFOREST;
	} else {
		td->str = tt == TREE_CACTUS ? STR_LAI_TREE_NAME_CACTUS_PLANTS : STR_LAI_TREE_NAME_TREES;
	}

	td->owner[0] = GetTileOwner(tile);
}

static void TileLoopTreesDesert(TileIndex tile)
{
	switch (GetTropicZone(tile)) {
		case TROPICZONE_DESERT:
			if (GetTreeGround(tile) != TREE_GROUND_SNOW_DESERT) {
				SetTreeGroundDensity(tile, TREE_GROUND_SNOW_DESERT, 3);
				MarkTileDirtyByTile(tile);
			}
			break;

		case TROPICZONE_RAINFOREST: {
			static const SoundFx forest_sounds[] = {
				SND_42_RAINFOREST_1,
				SND_43_RAINFOREST_2,
				SND_44_RAINFOREST_3,
				SND_48_RAINFOREST_4
			};
			uint32_t r = Random();

			if (Chance16I(1, 200, r) && _settings_client.sound.ambient) SndPlayTileFx(forest_sounds[GB(r, 16, 2)], tile);
			break;
		}

		default: break;
	}
}

static void TileLoopTreesAlps(TileIndex tile)
{
	int k = GetTileZ(tile) - GetSnowLine() + 1;

	if (k < 0) {
		switch (GetTreeGround(tile)) {
			case TREE_GROUND_SNOW_DESERT: SetTreeGroundDensity(tile, TREE_GROUND_GRASS, 3); break;
			case TREE_GROUND_ROUGH_SNOW:  SetTreeGroundDensity(tile, TREE_GROUND_ROUGH, 3); break;
			default: return;
		}
	} else {
		uint density = std::min<uint>(k, 3);

		if (GetTreeGround(tile) != TREE_GROUND_SNOW_DESERT && GetTreeGround(tile) != TREE_GROUND_ROUGH_SNOW) {
			TreeGround tg = GetTreeGround(tile) == TREE_GROUND_ROUGH ? TREE_GROUND_ROUGH_SNOW : TREE_GROUND_SNOW_DESERT;
			SetTreeGroundDensity(tile, tg, density);
		} else if (GetTreeDensity(tile) != density) {
			SetTreeGroundDensity(tile, GetTreeGround(tile), density);
		} else {
			if (GetTreeDensity(tile) == 3) {
				uint32_t r = Random();
				if (Chance16I(1, 200, r) && _settings_client.sound.ambient) {
					SndPlayTileFx((r & 0x80000000) ? SND_39_ARCTIC_SNOW_2 : SND_34_ARCTIC_SNOW_1, tile);
				}
			}
			return;
		}
	}
	MarkTileDirtyByTile(tile);
}

static bool CanPlantExtraTrees(TileIndex tile)
{
	return ((_settings_game.game_creation.landscape == LT_TROPIC && GetTropicZone(tile) == TROPICZONE_RAINFOREST) ?
		(_settings_game.construction.extra_tree_placement == ETP_SPREAD_ALL || _settings_game.construction.extra_tree_placement == ETP_SPREAD_RAINFOREST) :
		_settings_game.construction.extra_tree_placement == ETP_SPREAD_ALL);
}

static void TileLoop_Trees(TileIndex tile)
{
	if (GetTreeGround(tile) == TREE_GROUND_SHORE) {
		TileLoop_Water(tile);
	} else {
		switch (_settings_game.game_creation.landscape) {
			case LT_TROPIC: TileLoopTreesDesert(tile); break;
			case LT_ARCTIC: TileLoopTreesAlps(tile);   break;
		}
	}

	AmbientSoundEffect(tile);

	/* TimerGameTick::counter is incremented by 256 between each call, so ignore lower 8 bits.
	 * Also, we use a simple hash to spread the updates evenly over the map.
	 * 11 and 9 are just some co-prime numbers for better spread.
	 */
	uint32_t cycle = 11 * TileX(tile) + 9 * TileY(tile) + (TimerGameTick::counter >> 8);

	/* Handle growth of grass (under trees/on MP_TREES tiles) at every 8th processings, like it's done for grass on MP_CLEAR tiles. */
	if ((cycle & 7) == 7 && GetTreeGround(tile) == TREE_GROUND_GRASS) {
		uint density = GetTreeDensity(tile);
		if (density < 3) {
			SetTreeGroundDensity(tile, TREE_GROUND_GRASS, density + 1);
			MarkTileDirtyByTile(tile);
		}
	}

	if (_settings_game.construction.extra_tree_placement == ETP_NO_GROWTH_NO_SPREAD) return;

	static const uint32_t TREE_UPDATE_FREQUENCY = 16;  // How many tile updates happen for one tree update
	if (cycle % TREE_UPDATE_FREQUENCY != TREE_UPDATE_FREQUENCY - 1) return;

	switch (GetTreeGrowth(tile)) {
		case TreeGrowthStage::Grown: // regular sized tree
			if (_settings_game.game_creation.landscape == LT_TROPIC &&
					GetTreeType(tile) != TREE_CACTUS &&
					GetTropicZone(tile) == TROPICZONE_DESERT) {
				AddTreeGrowth(tile, 1);
			} else {
				switch (GB(Random(), 0, 3)) {
					case 0: // start destructing
						AddTreeGrowth(tile, 1);
						break;

					case 1: // add a tree
						if (GetTreeCount(tile) < 4 && CanPlantExtraTrees(tile)) {
							AddTreeCount(tile, 1);
							SetTreeGrowth(tile, TreeGrowthStage::Growing1);
							break;
						}
						[[fallthrough]];

					case 2: { // add a neighbouring tree
						if (!CanPlantExtraTrees(tile)) break;

						TreeType treetype = GetTreeType(tile);

						tile += TileOffsByDir(static_cast<Direction>(Random() % DIR_END));

						/* Cacti don't spread */
						if (!CanPlantTreesOnTile(tile, false)) return;

						/* Don't plant trees, if ground was freshly cleared */
						if (IsTileType(tile, MP_CLEAR) && GetClearGround(tile) == CLEAR_GRASS && GetClearDensity(tile) != 3) return;

						PlantTreesOnTile(tile, treetype, 0, TreeGrowthStage::Growing1);

						break;
					}

					default:
						return;
				}
			}
			break;

		case TreeGrowthStage::Dead: // final stage of tree destruction
			if (!CanPlantExtraTrees(tile)) {
				/* if trees can't spread just plant a new one to prevent deforestation */
				SetTreeGrowth(tile, TreeGrowthStage::Growing1);
			} else if (GetTreeCount(tile) > 1) {
				/* more than one tree, delete it */
				AddTreeCount(tile, -1);
				SetTreeGrowth(tile, TreeGrowthStage::Grown);
			} else {
				/* just one tree, change type into MP_CLEAR */
				switch (GetTreeGround(tile)) {
					case TREE_GROUND_SHORE: MakeShore(tile); break;
					case TREE_GROUND_GRASS: MakeClear(tile, CLEAR_GRASS, GetTreeDensity(tile)); break;
					case TREE_GROUND_ROUGH: MakeClear(tile, CLEAR_ROUGH, 3); break;
					case TREE_GROUND_ROUGH_SNOW: {
						uint density = GetTreeDensity(tile);
						MakeClear(tile, CLEAR_ROUGH, 3);
						MakeSnow(tile, density);
						break;
					}
					default: // snow or desert
						if (_settings_game.game_creation.landscape == LT_TROPIC) {
							MakeClear(tile, CLEAR_DESERT, GetTreeDensity(tile));
						} else {
							uint density = GetTreeDensity(tile);
							MakeClear(tile, CLEAR_GRASS, 3);
							MakeSnow(tile, density);
						}
						break;
				}
			}
			break;

		default:
			AddTreeGrowth(tile, 1);
			break;
	}

	MarkTileDirtyByTile(tile);
}

/**
 * Decrement the tree tick counter.
 * The interval is scaled by map size to allow for the same density regardless of size.
 * Adjustment for map sizes below the standard 256 * 256 are handled earlier.
 * @return true if the counter was decremented past zero
 */
bool DecrementTreeCounter()
{
	/* Ensure _trees_tick_ctr can be decremented past zero only once for the largest map size. */
	static_assert(2 * (MAX_MAP_SIZE_BITS - MIN_MAP_SIZE_BITS) - 4 <= std::numeric_limits<uint8_t>::digits);

	/* byte underflow */
	uint8_t old_trees_tick_ctr = _trees_tick_ctr;
	_trees_tick_ctr -= Map::ScaleBySize(1);
	return old_trees_tick_ctr <= _trees_tick_ctr;
}

void OnTick_Trees()
{
	/* Don't spread trees if that's not allowed */
	if (_settings_game.construction.extra_tree_placement == ETP_NO_SPREAD || _settings_game.construction.extra_tree_placement == ETP_NO_GROWTH_NO_SPREAD) return;

	uint32_t r;
	TileIndex tile;
	TreeType tree;

	/* Skip some tree ticks for map sizes below 256 * 256. 64 * 64 is 16 times smaller, so
	 * this is the maximum number of ticks that are skipped. Number of ticks to skip is
	 * inversely proportional to map size, so that is handled to create a mask. */
	int skip = Map::ScaleBySize(16);
	if (skip < 16 && (TimerGameTick::counter & (16 / skip - 1)) != 0) return;

	/* place a tree at a random rainforest spot */
	if (_settings_game.game_creation.landscape == LT_TROPIC) {
		for (uint c = Map::ScaleBySize(1); c > 0; c--) {
			if ((r = Random(), tile = RandomTileSeed(r), GetTropicZone(tile) == TROPICZONE_RAINFOREST) &&
				CanPlantTreesOnTile(tile, false) &&
				(tree = GetRandomTreeType(tile, GB(r, 24, 8))) != TREE_INVALID) {
				PlantTreesOnTile(tile, tree, 0, TreeGrowthStage::Growing1);
			}
		}
	}

	if (!DecrementTreeCounter() || _settings_game.construction.extra_tree_placement == ETP_SPREAD_RAINFOREST) return;

	/* place a tree at a random spot */
	r = Random();
	tile = RandomTileSeed(r);
	if (CanPlantTreesOnTile(tile, false) && (tree = GetRandomTreeType(tile, GB(r, 24, 8))) != TREE_INVALID) {
		PlantTreesOnTile(tile, tree, 0, TreeGrowthStage::Growing1);
	}
}

static TrackStatus GetTileTrackStatus_Trees(TileIndex, TransportType, uint, DiagDirection)
{
	return 0;
}

static void ChangeTileOwner_Trees(TileIndex, Owner, Owner)
{
	/* not used */
}

void InitializeTrees()
{
	_trees_tick_ctr = 0;
}

static CommandCost TerraformTile_Trees(TileIndex tile, DoCommandFlag flags, int, Slope)
{
	return Command<CMD_LANDSCAPE_CLEAR>::Do(flags, tile);
}


extern const TileTypeProcs _tile_type_trees_procs = {
	DrawTile_Trees,           // draw_tile_proc
	GetSlopePixelZ_Trees,     // get_slope_z_proc
	ClearTile_Trees,          // clear_tile_proc
	nullptr,                     // add_accepted_cargo_proc
	GetTileDesc_Trees,        // get_tile_desc_proc
	GetTileTrackStatus_Trees, // get_tile_track_status_proc
	nullptr,                     // click_tile_proc
	nullptr,                     // animate_tile_proc
	TileLoop_Trees,           // tile_loop_proc
	ChangeTileOwner_Trees,    // change_tile_owner_proc
	nullptr,                     // add_produced_cargo_proc
	nullptr,                     // vehicle_enter_tile_proc
	GetFoundation_Trees,      // get_foundation_proc
	TerraformTile_Trees,      // terraform_tile_proc
};
