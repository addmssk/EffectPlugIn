// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

#define PLUGIN_API TRAVERSALGAMEPLAY_API

namespace TrvGameplayTags
{
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayState_DisableParkour);
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayState_DisableHeroLanding);
	
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_None);
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Walking);
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_NavWalking);
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Falling);
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Swimming);
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Flying);
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Custom);
	
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Custom_None);
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Custom_RootMotionOnly);
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Custom_Slide);
	PLUGIN_API	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Custom_FollowFlightPath);
};

#undef PLUGIN_API