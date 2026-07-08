// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraversalGameplay.h"
#include "GameplayTagsManager.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FTraversalGameplayModule"

void FTraversalGameplayModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	UGameplayTagsManager::Get().AddTagIniSearchPath(FPaths::ProjectPluginsDir() / TEXT("FeatureSupplements/Config/Tags"));
}

void FTraversalGameplayModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTraversalGameplayModule, TraversalGameplay)