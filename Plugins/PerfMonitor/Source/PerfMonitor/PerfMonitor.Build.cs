// PerfMonitor plugin module rules.

using UnrealBuildTool;

public class PerfMonitor : ModuleRules
{
	public PerfMonitor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			// UPerfMonitorSubsystem (UCLASS) in the public headers.
			"CoreUObject",
			// IPerformanceDataConsumer (ChartCreation.h) and UGameInstanceSubsystem.
			"Engine",
		});

		// FPlatformApplicationMisc::IsThisApplicationForeground (desktop foreground gate).
		// Guarded by WITH_APPLICATION_CORE in code; servers/programs build without it.
		if (Target.bCompileAgainstApplicationCore)
		{
			PrivateDependencyModuleNames.Add("ApplicationCore");
		}
	}
}
