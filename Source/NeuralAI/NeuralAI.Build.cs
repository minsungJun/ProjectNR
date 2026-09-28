// Neural Enemy AI prototype module.

using UnrealBuildTool;

public class NeuralAI : ModuleRules
{
	public NeuralAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Engine-provided runtime modules verified against UE 5.8.2:
		//   AIModule / NavigationSystem / GameplayTasks -> Engine/Source/Runtime
		//   StateTreeModule                             -> Plugins/Runtime/StateTree
		//   GameplayStateTreeModule                     -> Plugins/Runtime/GameplayStateTree
		// GameplayStateTreeModule already re-exports most of these, but they are
		// listed explicitly so this module does not rely on transitive exports.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AIModule",
			"NavigationSystem",
			"GameplayTasks",
			"GameplayTags",
			"StateTreeModule",
			"GameplayStateTreeModule",
			// Public: the test player's header exposes UInputAction / UInputMappingContext.
			"InputCore",
			"EnhancedInput"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
