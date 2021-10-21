/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "WallPlaceAction.h"

#include "../OpenRCT2.h"
#include "../management/Finance.h"
#include "../ride/Track.h"
#include "../ride/TrackDesign.h"
#include "../world/Banner.h"
#include "../world/ConstructionClearance.h"
#include "../world/LargeScenery.h"
#include "../world/MapAnimation.h"
#include "../world/SmallScenery.h"
#include "../world/Surface.h"
#include "../world/Wall.h"

using namespace OpenRCT2::TrackMetaData;

WallPlaceAction::WallPlaceAction(
    ObjectEntryIndex wallType, const CoordsXYZ& loc, uint8_t edge, int32_t primaryColour, int32_t secondaryColour,
    int32_t tertiaryColour)
    : _wallType(wallType)
    , _loc(loc)
    , _edge(edge)
    , _primaryColour(primaryColour)
    , _secondaryColour(secondaryColour)
    , _tertiaryColour(tertiaryColour)
{
}

void WallPlaceAction::AcceptParameters(GameActionParameterVisitor& visitor)
{
    visitor.Visit(_loc);
    visitor.Visit("object", _wallType);
    visitor.Visit("edge", _edge);
    visitor.Visit("primaryColour", _primaryColour);
    visitor.Visit("secondaryColour", _secondaryColour);
    visitor.Visit("tertiaryColour", _tertiaryColour);
}

uint16_t WallPlaceAction::GetActionFlags() const
{
    return GameAction::GetActionFlags();
}

void WallPlaceAction::Serialise(DataSerialiser& stream)
{
    GameAction::Serialise(stream);

    stream << DS_TAG(_wallType) << DS_TAG(_loc) << DS_TAG(_edge) << DS_TAG(_primaryColour) << DS_TAG(_secondaryColour)
           << DS_TAG(_tertiaryColour);
}

GameActions::Result::Ptr WallPlaceAction::Query() const
{
    auto res = MakeResult();
    res->ErrorTitle = STR_CANT_BUILD_THIS_HERE;
    res->Position = _loc;

    res->Expenditure = ExpenditureType::Landscaping;
    res->Position.x += 16;
    res->Position.y += 16;

    if (_loc.z == 0)
    {
        res->Position.z = tile_element_height(res->Position);
    }

    if (!LocationValid(_loc))
    {
        return MakeResult(GameActions::Status::NotOwned, STR_CANT_BUILD_THIS_HERE, STR_NONE);
    }

    if (!(gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) && !(GetFlags() & GAME_COMMAND_FLAG_PATH_SCENERY) && !gCheatsSandboxMode)
    {
        if (_loc.z == 0)
        {
            if (!map_is_location_in_park(_loc))
            {
                return MakeResult(GameActions::Status::NotOwned, STR_CANT_BUILD_THIS_HERE, STR_NONE);
            }
        }
        else if (!map_is_location_owned(_loc))
        {
            return MakeResult(GameActions::Status::NotOwned, STR_CANT_BUILD_THIS_HERE, STR_NONE);
        }
    }
    else if (!_trackDesignDrawingPreview && (_loc.x > GetMapSizeMaxXY() || _loc.y > GetMapSizeMaxXY()))
    {
        log_error("Invalid x/y coordinates. x = %d y = %d", _loc.x, _loc.y);
        return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_THIS_HERE, STR_NONE);
    }

    if (_edge > 3)
    {
        return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_THIS_HERE, STR_NONE);
    }

    uint8_t edgeSlope = 0;
    auto targetHeight = _loc.z;
    if (targetHeight == 0)
    {
        auto* surfaceElement = map_get_surface_element_at(_loc);
        if (surfaceElement == nullptr)
        {
            log_error("Surface element not found at %d, %d.", _loc.x, _loc.y);
            return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_THIS_HERE, STR_NONE);
        }
        targetHeight = surfaceElement->GetBaseZ();

        uint8_t slope = surfaceElement->GetSlope();
        edgeSlope = LandSlopeToWallSlope[slope][_edge & 3];
        if (edgeSlope & EDGE_SLOPE_ELEVATED)
        {
            targetHeight += 16;
            edgeSlope &= ~EDGE_SLOPE_ELEVATED;
        }
    }

    auto* surfaceElement = map_get_surface_element_at(_loc);
    if (surfaceElement == nullptr)
    {
        log_error("Surface element not found at %d, %d.", _loc.x, _loc.y);
        return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_THIS_HERE, STR_NONE);
    }

    if (surfaceElement->GetWaterHeight() > 0)
    {
        uint16_t waterHeight = surfaceElement->GetWaterHeight();

        if (targetHeight < waterHeight && !gCheatsDisableClearanceChecks)
        {
            return MakeResult(GameActions::Status::Disallowed, STR_CANT_BUILD_THIS_HERE, STR_CANT_BUILD_THIS_UNDERWATER);
        }
    }

    if (targetHeight < surfaceElement->GetBaseZ() && !gCheatsDisableClearanceChecks)
    {
        return MakeResult(GameActions::Status::Disallowed, STR_CANT_BUILD_THIS_HERE, STR_CAN_ONLY_BUILD_THIS_ABOVE_GROUND);
    }

    if (!(edgeSlope & (EDGE_SLOPE_UPWARDS | EDGE_SLOPE_DOWNWARDS)))
    {
        uint8_t newEdge = (_edge + 2) & 3;
        uint8_t newBaseHeight = surfaceElement->base_height;
        newBaseHeight += 2;
        if (surfaceElement->GetSlope() & (1 << newEdge))
        {
            if (targetHeight / 8 < newBaseHeight)
            {
                return MakeResult(
                    GameActions::Status::Disallowed, STR_CANT_BUILD_THIS_HERE, STR_CAN_ONLY_BUILD_THIS_ABOVE_GROUND);
            }

            if (surfaceElement->GetSlope() & TILE_ELEMENT_SLOPE_DOUBLE_HEIGHT)
            {
                newEdge = (newEdge - 1) & 3;

                if (surfaceElement->GetSlope() & (1 << newEdge))
                {
                    newEdge = (newEdge + 2) & 3;
                    if (surfaceElement->GetSlope() & (1 << newEdge))
                    {
                        newBaseHeight += 2;
                        if (targetHeight / 8 < newBaseHeight)
                        {
                            return MakeResult(
                                GameActions::Status::Disallowed, STR_CANT_BUILD_THIS_HERE,
                                STR_CAN_ONLY_BUILD_THIS_ABOVE_GROUND);
                        }
                        newBaseHeight -= 2;
                    }
                }
            }
        }

        newEdge = (_edge + 3) & 3;
        if (surfaceElement->GetSlope() & (1 << newEdge))
        {
            if (targetHeight / 8 < newBaseHeight)
            {
                return MakeResult(
                    GameActions::Status::Disallowed, STR_CANT_BUILD_THIS_HERE, STR_CAN_ONLY_BUILD_THIS_ABOVE_GROUND);
            }

            if (surfaceElement->GetSlope() & TILE_ELEMENT_SLOPE_DOUBLE_HEIGHT)
            {
                newEdge = (newEdge - 1) & 3;

                if (surfaceElement->GetSlope() & (1 << newEdge))
                {
                    newEdge = (newEdge + 2) & 3;
                    if (surfaceElement->GetSlope() & (1 << newEdge))
                    {
                        newBaseHeight += 2;
                        if (targetHeight / 8 < newBaseHeight)
                        {
                            return MakeResult(
                                GameActions::Status::Disallowed, STR_CANT_BUILD_THIS_HERE,
                                STR_CAN_ONLY_BUILD_THIS_ABOVE_GROUND);
                        }
                    }
                }
            }
        }
    }

    auto* wallEntry = get_wall_entry(_wallType);

    if (wallEntry == nullptr)
    {
        log_error("Wall Type not found %d", _wallType);
        return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_THIS_HERE, STR_NONE);
    }

    if (wallEntry->scrolling_mode != SCROLLING_MODE_NONE)
    {
        if (HasReachedBannerLimit())
        {
            log_error("No free banners available");
            return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_THIS_HERE, STR_TOO_MANY_BANNERS_IN_GAME);
        }
    }

    uint8_t clearanceHeight = targetHeight / 8;
    if (edgeSlope & (EDGE_SLOPE_UPWARDS | EDGE_SLOPE_DOWNWARDS))
    {
        if (wallEntry->flags & WALL_SCENERY_CANT_BUILD_ON_SLOPE)
        {
            return MakeResult(GameActions::Status::Disallowed, STR_CANT_BUILD_THIS_HERE, STR_ERR_UNABLE_TO_BUILD_THIS_ON_SLOPE);
        }
        clearanceHeight += 2;
    }
    clearanceHeight += wallEntry->height;

    bool wallAcrossTrack = false;
    if (!(GetFlags() & GAME_COMMAND_FLAG_PATH_SCENERY) && !gCheatsDisableClearanceChecks)
    {
        auto result = WallCheckObstruction(wallEntry, targetHeight / 8, clearanceHeight, &wallAcrossTrack);
        if (result->Error != GameActions::Status::Ok)
        {
            return result;
        }
    }

    if (!MapCheckCapacityAndReorganise(_loc))
    {
        return MakeResult(GameActions::Status::NoFreeElements, STR_CANT_BUILD_THIS_HERE, STR_TILE_ELEMENT_LIMIT_REACHED);
    }

    res->Cost = wallEntry->price;

    res->SetData(WallPlaceActionResult{});

    return res;
}

GameActions::Result::Ptr WallPlaceAction::Execute() const
{
    auto res = MakeResult();
    res->ErrorTitle = STR_CANT_BUILD_THIS_HERE;
    res->Position = _loc;

    res->Expenditure = ExpenditureType::Landscaping;
    res->Position.x += 16;
    res->Position.y += 16;

    if (res->Position.z == 0)
    {
        res->Position.z = tile_element_height(res->Position);
    }

    uint8_t edgeSlope = 0;
    auto targetHeight = _loc.z;
    if (targetHeight == 0)
    {
        auto* surfaceElement = map_get_surface_element_at(_loc);
        if (surfaceElement == nullptr)
        {
            log_error("Surface element not found at %d, %d.", _loc.x, _loc.y);
            return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_THIS_HERE, STR_NONE);
        }
        targetHeight = surfaceElement->GetBaseZ();

        uint8_t slope = surfaceElement->GetSlope();
        edgeSlope = LandSlopeToWallSlope[slope][_edge & 3];
        if (edgeSlope & EDGE_SLOPE_ELEVATED)
        {
            targetHeight += 16;
            edgeSlope &= ~EDGE_SLOPE_ELEVATED;
        }
    }
    auto targetLoc = CoordsXYZ(_loc, targetHeight);

    auto* wallEntry = get_wall_entry(_wallType);

    if (wallEntry == nullptr)
    {
        log_error("Wall Type not found %d", _wallType);
        return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_THIS_HERE, STR_NONE);
    }

    uint8_t clearanceHeight = targetHeight / COORDS_Z_STEP;
    if (edgeSlope & (EDGE_SLOPE_UPWARDS | EDGE_SLOPE_DOWNWARDS))
    {
        clearanceHeight += 2;
    }
    clearanceHeight += wallEntry->height;

    bool wallAcrossTrack = false;
    if (!(GetFlags() & GAME_COMMAND_FLAG_PATH_SCENERY) && !gCheatsDisableClearanceChecks)
    {
        auto result = WallCheckObstruction(wallEntry, targetHeight / COORDS_Z_STEP, clearanceHeight, &wallAcrossTrack);
        if (result->Error != GameActions::Status::Ok)
        {
            return result;
        }
    }

    Banner* banner = nullptr;
    if (wallEntry->scrolling_mode != SCROLLING_MODE_NONE)
    {
        banner = CreateBanner();
        if (banner == nullptr)
        {
            log_error("No free banners available");
            return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_THIS_HERE, STR_TOO_MANY_BANNERS_IN_GAME);
        }

        banner->text = {};
        banner->colour = COLOUR_WHITE;
        banner->text_colour = COLOUR_WHITE;
        banner->flags = BANNER_FLAG_IS_WALL;
        banner->type = 0; // Banner must be deleted after this point in an early return
        banner->position = TileCoordsXY(_loc);

        ride_id_t rideIndex = banner_get_closest_ride_index(targetLoc);
        if (rideIndex != RIDE_ID_NULL)
        {
            banner->ride_index = rideIndex;
            banner->flags |= BANNER_FLAG_LINKED_TO_RIDE;
        }
    }

    auto* wallElement = TileElementInsert<WallElement>(targetLoc, 0b0000);
    if (wallElement == nullptr)
    {
        return MakeResult(GameActions::Status::NoFreeElements, STR_CANT_POSITION_THIS_HERE, STR_TILE_ELEMENT_LIMIT_REACHED);
    }

    wallElement->clearance_height = clearanceHeight;
    wallElement->SetDirection(_edge);
    wallElement->SetSlope(edgeSlope);

    wallElement->SetPrimaryColour(_primaryColour);
    wallElement->SetSecondaryColour(_secondaryColour);
    wallElement->SetAcrossTrack(wallAcrossTrack);

    wallElement->SetEntryIndex(_wallType);
    wallElement->SetBannerIndex(banner != nullptr ? banner->id : BANNER_INDEX_NULL);

    if (wallEntry->flags & WALL_SCENERY_HAS_TERNARY_COLOUR)
    {
        wallElement->SetTertiaryColour(_tertiaryColour);
    }

    wallElement->SetGhost(GetFlags() & GAME_COMMAND_FLAG_GHOST);

    map_animation_create(MAP_ANIMATION_TYPE_WALL, targetLoc);
    map_invalidate_tile_zoom1({ _loc, wallElement->GetBaseZ(), wallElement->GetBaseZ() + 72 });

    res->Cost = wallEntry->price;

    const auto bannerId = banner != nullptr ? banner->id : BANNER_INDEX_NULL;
    res->SetData(WallPlaceActionResult{ wallElement->GetBaseZ(), bannerId });

    return res;
}

/**
 *
 *  rct2: 0x006E5CBA
 */
bool WallPlaceAction::WallCheckObstructionWithTrack(
    WallSceneryEntry* wall, int32_t z0, TrackElement* trackElement, bool* wallAcrossTrack) const
{
    track_type_t trackType = trackElement->GetTrackType();

    using namespace OpenRCT2::TrackMetaData;
    const auto& ted = GetTrackElementDescriptor(trackType);
    int32_t sequence = trackElement->GetSequenceIndex();
    int32_t direction = (_edge - trackElement->GetDirection()) & TILE_ELEMENT_DIRECTION_MASK;
    auto ride = get_ride(trackElement->GetRideIndex());
    if (ride == nullptr)
    {
        return false;
    }

    if (TrackIsAllowedWallEdges(ride->type, trackType, sequence, direction))
    {
        return true;
    }

    if (!(wall->flags & WALL_SCENERY_IS_DOOR))
    {
        return false;
    }

    if (!ride->GetRideTypeDescriptor().HasFlag(RIDE_TYPE_FLAG_ALLOW_DOORS_ON_TRACK))
    {
        return false;
    }

    *wallAcrossTrack = true;
    if (z0 & 1)
    {
        return false;
    }

    int32_t z;
    if (sequence == 0)
    {
        if (ted.SequenceProperties[0] & TRACK_SEQUENCE_FLAG_DISALLOW_DOORS)
        {
            return false;
        }

        if (ted.Definition.bank_start == 0)
        {
            if (!(ted.Coordinates.rotation_begin & 4))
            {
                direction = direction_reverse(trackElement->GetDirection());
                if (direction == _edge)
                {
                    const rct_preview_track* trackBlock = &ted.Block[sequence];
                    z = ted.Coordinates.z_begin;
                    z = trackElement->base_height + ((z - trackBlock->z) * 8);
                    if (z == z0)
                    {
                        return true;
                    }
                }
            }
        }
    }

    const rct_preview_track* trackBlock = &ted.Block[sequence + 1];
    if (trackBlock->index != 0xFF)
    {
        return false;
    }

    if (ted.Definition.bank_end != 0)
    {
        return false;
    }

    direction = ted.Coordinates.rotation_end;
    if (direction & 4)
    {
        return false;
    }

    direction = (trackElement->GetDirection() + ted.Coordinates.rotation_end) & TILE_ELEMENT_DIRECTION_MASK;
    if (direction != _edge)
    {
        return false;
    }

    trackBlock = &ted.Block[sequence];
    z = ted.Coordinates.z_end;
    z = trackElement->base_height + ((z - trackBlock->z) * 8);
    return z == z0;
}

/**
 *
 *  rct2: 0x006E5C1A
 */
GameActions::Result::Ptr WallPlaceAction::WallCheckObstruction(
    WallSceneryEntry* wall, int32_t z0, int32_t z1, bool* wallAcrossTrack) const
{
    *wallAcrossTrack = false;
    if (map_is_location_at_edge(_loc))
    {
        return MakeResult(GameActions::Status::InvalidParameters, STR_OFF_EDGE_OF_MAP);
    }

    TileElement* tileElement = map_get_first_element_at(_loc);
    do
    {
        if (tileElement == nullptr)
            break;
        int32_t elementType = tileElement->GetType();
        if (elementType == TILE_ELEMENT_TYPE_SURFACE)
            continue;
        if (tileElement->IsGhost())
            continue;
        if (z0 >= tileElement->clearance_height)
            continue;
        if (z1 <= tileElement->base_height)
            continue;
        if (elementType == TILE_ELEMENT_TYPE_WALL)
        {
            int32_t direction = tileElement->GetDirection();
            if (_edge == direction)
            {
                auto res = MakeResult(GameActions::Status::NoClearance, STR_NONE);
                map_obstruction_set_error_text(tileElement, *res);
                return res;
            }
            continue;
        }
        if (tileElement->GetOccupiedQuadrants() == 0)
            continue;
        auto res = MakeResult(GameActions::Status::NoClearance, STR_NONE);
        switch (elementType)
        {
            case TILE_ELEMENT_TYPE_ENTRANCE:
                map_obstruction_set_error_text(tileElement, *res);
                return res;
            case TILE_ELEMENT_TYPE_PATH:
                if (tileElement->AsPath()->GetEdges() & (1 << _edge))
                {
                    map_obstruction_set_error_text(tileElement, *res);
                    return res;
                }
                break;
            case TILE_ELEMENT_TYPE_LARGE_SCENERY:
            {
                const auto* largeSceneryElement = tileElement->AsLargeScenery();
                const auto* sceneryEntry = largeSceneryElement->GetEntry();

                // If there is no entry, assume the object is not in the way.
                if (sceneryEntry == nullptr)
                    break;

                auto sequence = largeSceneryElement->GetSequenceIndex();
                const rct_large_scenery_tile& tile = sceneryEntry->tiles[sequence];

                int32_t direction = ((_edge - tileElement->GetDirection()) & TILE_ELEMENT_DIRECTION_MASK) + 8;
                if (!(tile.flags & (1 << direction)))
                {
                    map_obstruction_set_error_text(tileElement, *res);
                    return res;
                }
                break;
            }
            case TILE_ELEMENT_TYPE_SMALL_SCENERY:
            {
                auto sceneryEntry = tileElement->AsSmallScenery()->GetEntry();
                if (sceneryEntry != nullptr && sceneryEntry->HasFlag(SMALL_SCENERY_FLAG_NO_WALLS))
                {
                    map_obstruction_set_error_text(tileElement, *res);
                    return res;
                }
                break;
            }
            case TILE_ELEMENT_TYPE_TRACK:
                if (!WallCheckObstructionWithTrack(wall, z0, tileElement->AsTrack(), wallAcrossTrack))
                {
                    return res;
                }
                break;
        }
    } while (!(tileElement++)->IsLastForTile());

    return MakeResult();
}

bool WallPlaceAction::TrackIsAllowedWallEdges(
    uint8_t rideType, track_type_t trackType, uint8_t trackSequence, uint8_t direction)
{
    if (!GetRideTypeDescriptor(rideType).HasFlag(RIDE_TYPE_FLAG_TRACK_NO_WALLS))
    {
        const auto& ted = GetTrackElementDescriptor(trackType);
        if (ted.SequenceElementAllowedWallEdges[trackSequence] & (1 << direction))
        {
            return true;
        }
    }
    return false;
}
