#include "OsirisSubsystem.h"

#include "OsirisSaveComponent.h"
#include "Components/ActorComponent.h"
#include "EngineUtils.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"


struct FOsirisSaveGameArchive : public FObjectAndNameAsStringProxyArchive
{
	FOsirisSaveGameArchive(FArchive& Inner)
		: FObjectAndNameAsStringProxyArchive(Inner, true)
	{
		ArIsSaveGame = true;
		ArNoDelta = true;
	}
};

static void Osiris_SerializeSaveGame(UObject* Obj, TArray<uint8>& OutBytes)
{
	OutBytes.Reset();

	FMemoryWriter Writer(OutBytes, true);
	FOsirisSaveGameArchive Ar(Writer);
	Obj->Serialize(Ar);
}

static bool Osiris_DeserializeSaveGame(UObject* Obj, const TArray<uint8>& InBytes)
{
	if (InBytes.Num() <= 0) return false;

	FMemoryReader Reader(InBytes,true);
	FOsirisSaveGameArchive Ar(Reader);
	Obj->Serialize(Ar);
	return true;
}


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


void UOsirisSubsystem::GetMarkedPlacedSpawnedCount(int32& OutPlaced, int32& OutSpawned) const
{
	OutPlaced = 0;
	OutSpawned = 0;

	UWorld* World = GetWorld();
	if (!World) return;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		if (!Actor->FindComponentByClass<UOsirisSaveComponent>())
			continue;

		const bool bPlaced = Actor->HasAnyFlags(RF_WasLoaded);
		if (bPlaced) ++OutPlaced;
		else ++OutSpawned;
	}
}


bool UOsirisSubsystem::SaveGameSnapshot(AActor* Target, bool bIncludeComponents)
{
	if (!Target) return false;

	if (!Target->FindComponentByClass<UOsirisSaveComponent>())
		return false;

	LastActorBytes.Reset();
	LastComponentNames.Reset();
	LastComponentBytes.Reset();

	Osiris_SerializeSaveGame(Target, LastActorBytes);

	if (bIncludeComponents)
	{
		TArray<UActorComponent*> Comps;
		Target->GetComponents(Comps);

		for (UActorComponent* C : Comps)
		{
			if (!C) continue;

			TArray<uint8> Bytes;
			Osiris_SerializeSaveGame(C, Bytes);

			LastComponentNames.Add(C->GetFName());
			LastComponentBytes.Add(MoveTemp(Bytes));
		}
	}

	bHasLastSnapshot = true;
	return true;
}

bool UOsirisSubsystem::LoadGameSnapshot(AActor* Target, bool bIncludeComponents) const
{
	if (!Target) return false;
	if (!bHasLastSnapshot) return false;

	if (!Osiris_DeserializeSaveGame(Target, LastActorBytes))
		return false;

	if (bIncludeComponents)
	{
		const int32 N = FMath::Min(LastComponentNames.Num(), LastComponentBytes.Num());

		TArray<UActorComponent*> Comps;
		Target->GetComponents(Comps);

		for (int32 i = 0; i < N; ++i)
		{
			const FName WantName = LastComponentNames[i];

			UActorComponent* Found = nullptr;
			for (UActorComponent* C : Comps)
			{
				if (C && C->GetFName() == WantName)
				{
					Found = C;
					break;
				}
			}

			if (!Found)
				continue;

			if (!Osiris_DeserializeSaveGame(Found, LastComponentBytes[i]))
				return false;
		}
	}

	return true;
}

FString UOsirisSubsystem::GetLastSnapshotDebugInfo() const
{
	if (!bHasLastSnapshot)
	{
		return TEXT("OSIRIS: NO SNAPSHOT");
	}

	return FString::Printf(
		TEXT("OSIRIS: Snapshot ActorBytes=%d Components=%d"),
		LastActorBytes.Num(),
		LastComponentNames.Num()
	);
}
