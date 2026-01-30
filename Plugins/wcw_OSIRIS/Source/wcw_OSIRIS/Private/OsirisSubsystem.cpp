// Fill out your copyright notice in the Description page of Project Settings.


#include "OsirisSubsystem.h"
#include "OsirisSaveComponent.h"
#include "EngineUtils.h"

int32 UOsirisSubsystem::GetMarkedActorCount() const
{
	UWorld* World = GetWorld();
	if (!World) return 0;

	int32 Count = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->FindComponentByClass<UOsirisSaveComponent>())
		{
			++Count;
		}
	}
	return Count;
}