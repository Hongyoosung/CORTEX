// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * MOC Types - Compatibility Header
 *
 * This header has been split into focused type files for better organization:
 * - StrategyTypes.h: Tactical strategies and plays
 * - ObservationTypes.h: Agent observations and tactical parameters
 * - GameStateTypes.h: Game entities and stats
 * - EventTypes.h: Critical event types
 *
 * This file is kept for backwards compatibility.
 * New code should include the specific type headers directly.
 */

#include "Types/StrategyTypes.h"
#include "Types/ObservationTypes.h"
#include "Types/GameStateTypes.h"
#include "Types/EventTypes.h"
