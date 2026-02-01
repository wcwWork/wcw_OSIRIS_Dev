// Fill out your copyright notice in the Description page of Project Settings.


#include "OsirisSubsystem.h"
#include "OsirisSaveComponent.h"
#include "EngineUtils.h"


FString UOsirisSubsystem::BuildDebugManifestString(int32 MaxLines) const
{
	UWorld* World = GetWorld();
	if (!World) return TEXT("NO_WORLD");

	FString Out;
	int32 Lines = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		UOsirisSaveComponent* SaveComp = Actor->FindComponentByClass<UOsirisSaveComponent>();
		if (!SaveComp) continue;

		const bool bPlaced = Actor->HasAnyFlags(RF_WasLoaded);
		const FVector Loc = Actor->GetActorLocation();

		const FString GuidStr = SaveComp->GetOsirisGuidString();
		const FString KindStr = bPlaced ? TEXT("PLACED") : TEXT("SPAWNED");
		const FString ClassStr = Actor->GetClass() ? Actor->GetClass()->GetName() : TEXT("NOCLASS");

		Out += FString::Printf(
			TEXT("%s | %s | %s | Loc(%.0f,%.0f,%.0f)\n"),
			*GuidStr, *KindStr, *ClassStr, Loc.X, Loc.Y, Loc.Z
		);

		++Lines;
		if (Lines >= MaxLines) break;
	}

	if (Lines == 0)
	{
		return TEXT("NO_MARKED_ACTORS");
	}

	return Out;
}