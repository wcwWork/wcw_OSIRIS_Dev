// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class wcw_OSIRIS_DevTarget : TargetRules
{
	public wcw_OSIRIS_DevTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V2;
		ExtraModuleNames.AddRange( new string[] { "wcw_OSIRIS_Dev" } );
	}
}
