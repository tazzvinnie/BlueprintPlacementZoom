using UnrealBuildTool;

public class BlueprintPlacementZoom : ModuleRules
{
	public BlueprintPlacementZoom(ReadOnlyTargetRules Target) : base(Target)
	{
		CppStandard = CppStandardVersion.Cpp20;
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bLegacyPublicIncludePaths = false;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"FactoryGame",
				"SML"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"RenderCore"
			}
		);
	}
}
