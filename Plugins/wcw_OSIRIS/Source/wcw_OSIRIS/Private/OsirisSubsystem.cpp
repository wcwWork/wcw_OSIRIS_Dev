#include "OsirisSubsystem.h"

#include "OsirisSaveComponent.h"
#include "OsirisSaveGame.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

static const FString GOsirisSlot = TEXT("OSIRIS_SLOT");

struct FOsirisWorldAr : FObjectAndNameAsStringProxyArchive
{
	FOsirisWorldAr(FArchive& Inner) : FObjectAndNameAsStringProxyArchive(Inner, true) { ArNoDelta = true; } // без SaveGame-фильтра
};

static void Ser(UObject* O, TArray<uint8>& Out)
{
	Out.Reset();
	FMemoryWriter W(Out, true);
	FOsirisWorldAr Ar(W);
	O->Serialize(Ar);
}

static void Des(UObject* O, const TArray<uint8>& In)
{
	if (!O || In.Num() <= 0) return;
	FMemoryReader R(In, true);
	FOsirisWorldAr Ar(R);
	O->Serialize(Ar);
}

bool UOsirisSubsystem::SaveGame()
{
	UWorld* World = GetWorld();
	if (!World) return false;

	TArray<AActor*> Marked;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (It->FindComponentByClass<UOsirisSaveComponent>())
			Marked.Add(*It);

	TArray<uint8> Bytes;
	FMemoryWriter W(Bytes, true);
	FOsirisWorldAr Ar(W);

	int32 Count = Marked.Num(); Ar << Count;

	for (AActor* A : Marked)
	{
		UOsirisSaveComponent* SC = A ? A->FindComponentByClass<UOsirisSaveComponent>() : nullptr;
		if (!A || !SC) continue;

		if (!SC->OsirisGuid.IsValid()) SC->OsirisGuid = FGuid::NewGuid();

		FGuid Guid = SC->OsirisGuid;
		FString ClassPath = A->GetClass()->GetPathName();
		FTransform Xf = A->GetActorTransform();

		TArray<uint8> ABytes; Ser(A, ABytes);

		TArray<UActorComponent*> Comps; A->GetComponents(Comps);
		int32 CCount = Comps.Num();

		Ar << Guid; Ar << ClassPath; Ar << Xf; Ar << ABytes; Ar << CCount;

		for (UActorComponent* C : Comps)
		{
			FString Name = C ? C->GetFName().ToString() : FString();
			TArray<uint8> CBytes; if (C) Ser(C, CBytes);
			Ar << Name; Ar << CBytes;
		}
	}

	UOsirisSaveGame* SG = Cast<UOsirisSaveGame>(UGameplayStatics::CreateSaveGameObject(UOsirisSaveGame::StaticClass()));
	if (!SG) return false;

	SG->Data = MoveTemp(Bytes);
	return UGameplayStatics::SaveGameToSlot(SG, GOsirisSlot, 0);
}

bool UOsirisSubsystem::LoadGame()
{
	UWorld* World = GetWorld();
	if (!World) return false;

	UOsirisSaveGame* SG = Cast<UOsirisSaveGame>(UGameplayStatics::LoadGameFromSlot(GOsirisSlot, 0));
	if (!SG || SG->Data.Num() == 0) return false;

	TMap<FGuid, AActor*> Map;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (UOsirisSaveComponent* SC = It->FindComponentByClass<UOsirisSaveComponent>())
			if (SC->OsirisGuid.IsValid())
				Map.Add(SC->OsirisGuid, *It);

	FMemoryReader R(SG->Data, true);
	FOsirisWorldAr Ar(R);

	int32 Count = 0;
	Ar << Count;

	TSet<FGuid> SavedGuids;
	SavedGuids.Reserve(Count);

	bool bOk = true;

	for (int32 i = 0; i < Count; ++i)
	{
		FGuid Guid; FString ClassPath; FTransform Xf; TArray<uint8> ABytes; int32 CCount = 0;
		Ar << Guid; Ar << ClassPath; Ar << Xf; Ar << ABytes; Ar << CCount;

		SavedGuids.Add(Guid);

		AActor* A = Map.FindRef(Guid);

		if (!A)
		{
			if (UClass* Cls = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath))
			{
				FActorSpawnParameters P;
				P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

				A = World->SpawnActor<AActor>(Cls, Xf, P);

				if (A)
				{
					if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
						SC->SetOsirisGuid(Guid);

					Map.Add(Guid, A);
				}
			}
		}

		if (A)
		{
			Des(A, ABytes);

			TArray<UActorComponent*> Comps;
			A->GetComponents(Comps);

			for (int32 c = 0; c < CCount; ++c)
			{
				FString Name; TArray<uint8> CBytes;
				Ar << Name; Ar << CBytes;

				const FName Want(*Name);
				for (UActorComponent* Cmp : Comps)
					if (Cmp && Cmp->GetFName() == Want) { Des(Cmp, CBytes); break; }
			}

			A->ReregisterAllComponents();
			A->SetActorTransform(Xf, false, nullptr, ETeleportType::TeleportPhysics);
		}
		else
		{
			for (int32 c = 0; c < CCount; ++c) { FString N; TArray<uint8> B; Ar << N; Ar << B; }
			bOk = false;
		}
	}

	TArray<AActor*> ToDestroy;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;

		UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>();
		if (!SC || !SC->OsirisGuid.IsValid()) continue;

		if (!SavedGuids.Contains(SC->OsirisGuid))
			ToDestroy.Add(A);
	}

	for (AActor* A : ToDestroy)
		if (IsValid(A)) A->Destroy();

	return bOk;
}
