// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ProjectNR : ModuleRules
{
	public ProjectNR(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"DeveloperSettings",
			"NavigationSystem",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			// FCoreStyle fonts for the C++-built loadout widget.
			"SlateCore",
			// Plugins/Experimental/Mover. The player pawn's header implements
			// IMoverInputProducerInterface, so this is public. Mover re-exports
			// NetworkPrediction and MotionWarping, both already enabled.
			"Mover",
			// Plugins/Cameras/GameplayCameras. The player pawn carries a
			// UGameplayCameraComponent so it can drive the sample's own camera rigs,
			// which already cover aim, strafe, crouch and collision.
			"GameplayCameras",
			// Plugins/Animation/MotionWarping. Mover lists it as a public dependency, but
			// that only brings the headers across - the pawn owns a UMotionWarpingComponent
			// of its own, so the module has to be linked here too.
			"MotionWarping",
			// FNRMeleeRootMotionMove answers Mover's gameplay tag queries (so tag-based
			// cancels find it), which links against FGameplayTag's matching functions.
			"GameplayTags",
			// UHealthComponent lives here. The enemy's pain sensing subscribes to its
			// OnDamaged delegate, so player and enemy must share the same component
			// or the enemy's attacks land on nothing it understands. Kept in NeuralAI
			// rather than moved because two placed actors in Lvl_ThirdPerson serialize
			// it by class path, and moving would break them without a CoreRedirect.
			"NeuralAI"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"ProjectNR",
			"ProjectNR/Variant_Platforming",
			"ProjectNR/Variant_Platforming/Animation",
			"ProjectNR/Variant_Combat",
			"ProjectNR/Variant_Combat/AI",
			"ProjectNR/Variant_Combat/Animation",
			"ProjectNR/Variant_Combat/Gameplay",
			"ProjectNR/Variant_Combat/Interfaces",
			"ProjectNR/Variant_Combat/UI",
			"ProjectNR/Variant_SideScrolling",
			"ProjectNR/Variant_SideScrolling/AI",
			"ProjectNR/Variant_SideScrolling/Gameplay",
			"ProjectNR/Variant_SideScrolling/Interfaces",
			"ProjectNR/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
