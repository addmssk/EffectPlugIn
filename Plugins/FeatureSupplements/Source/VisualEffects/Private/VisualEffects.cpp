// Copyright Epic Games, Inc. All Rights Reserved.

#include "VisualEffects.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FVisualEffectsModule"

void FVisualEffectsModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	// Maps virtual shader source directory /Plugin/FX/Niagara to the plugin's actual Shaders directory.
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("FeatureSupplements"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/FeatureSupplements"), PluginShaderDir);
}

void FVisualEffectsModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FVisualEffectsModule, VisualEffects)