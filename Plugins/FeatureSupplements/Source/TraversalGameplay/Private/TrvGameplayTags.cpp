// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrvGameplayTags.h"

#include "Engine/EngineTypes.h"
#include "GameplayTagsManager.h"

namespace TrvGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayState_DisableParkour, "Gameplay.State.Appearance.Disable.Parkour", "");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayState_DisableHeroLanding, "Gameplay.State.Appearance.Disable.HeroLanding", "");
	
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Mode_Walking, "Movement.Mode.Walking", "Default Character movement tag");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Mode_NavWalking, "Movement.Mode.NavWalking", "Default Character movement tag");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Mode_Falling, "Movement.Mode.Falling", "Default Character movement tag");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Mode_Swimming, "Movement.Mode.Swimming", "Default Character movement tag");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Mode_Flying, "Movement.Mode.Flying", "Default Character movement tag");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Mode_Custom, "Movement.Mode.Custom", "This is invalid and should be replaced with custom tags");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Mode_Custom_None, "Movement.Mode.Custom.None", "");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Mode_Custom_RootMotionOnly, "Movement.Mode.Custom.RootMotionOnly", "");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Mode_Custom_Slide, "Movement.Mode.Custom.Slide", "");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Mode_Custom_FollowFlightPath, "Movement.Mode.Custom.FollowFlightPath", "");
}

