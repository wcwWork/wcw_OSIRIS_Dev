// Fill out your copyright notice in the Description page of Project Settings.

#include "OsirisSubsystem.h"

#include "OsirisSaveComponent.h"
#include "OsirisSaveGame.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

#include "UObject/UnrealType.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

bool UOsirisSubsystem::SaveGame()
{
	UWorld* World = GetWorld();
	if (!World) return false;

	static const FString GOsirisSlot = TEXT("OSIRIS_SLOT");

	struct FOsirisWorldAr : FObjectAndNameAsStringProxyArchive
	{
		FOsirisWorldAr(FArchive& Inner)
			: FObjectAndNameAsStringProxyArchive(Inner, true)
		{
			ArIsSaveGame = true;
			ArNoDelta = true;
		}
	};

	const auto HasAnySaveGameProps = [](const UObject* Obj) -> bool
	{
		if (!Obj) return false;
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_SaveGame))
				return true;
		}
		return false;
	};

	const auto LessGuid = [](const FGuid& L, const FGuid& R) -> bool
	{
		if (L.A != R.A) return L.A < R.A;
		if (L.B != R.B) return L.B < R.B;
		if (L.C != R.C) return L.C < R.C;
		return L.D < R.D;
	};

	TArray<AActor*> Marked;
	Marked.Reserve(256);

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;

		if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
		{
			if (!SC->OsirisGuid.IsValid())
				SC->OsirisGuid = FGuid::NewGuid();

			Marked.Add(A);
		}
	}

	Marked.Sort([&](const AActor& L, const AActor& R)
		{
			const UOsirisSaveComponent* LSC = L.FindComponentByClass<UOsirisSaveComponent>();
			const UOsirisSaveComponent* RSC = R.FindComponentByClass<UOsirisSaveComponent>();
			const FGuid LG = (LSC && LSC->OsirisGuid.IsValid()) ? LSC->OsirisGuid : FGuid();
			const FGuid RG = (RSC && RSC->OsirisGuid.IsValid()) ? RSC->OsirisGuid : FGuid();
			return LessGuid(LG, RG);
		});

	TArray<uint8> Bytes;
	FMemoryWriter W(Bytes, true);
	FOsirisWorldAr Ar(W);

	int32 Count = Marked.Num();
	Ar << Count;

	for (AActor* A : Marked)
	{
		if (!A) continue;

		UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>();
		if (!SC || !SC->OsirisGuid.IsValid()) continue;

		SC->GetOsirisPreSaveHook().Broadcast();

		FGuid Guid = SC->OsirisGuid;
		FString ClassPath = A->GetClass()->GetPathName();
		FTransform Xf = A->GetActorTransform();

		TArray<uint8> ABytes;
		{
			FMemoryWriter AW(ABytes, true);
			FOsirisWorldAr AAr(AW);
			A->Serialize(AAr);
		}

		TArray<UActorComponent*> AllComps;
		A->GetComponents(AllComps);

		TArray<UActorComponent*> SavableComps;
		SavableComps.Reserve(AllComps.Num());

		for (UActorComponent* C : AllComps)
		{
			if (!C) continue;
			if (C->HasAnyFlags(RF_Transient)) continue;
			if (!HasAnySaveGameProps(C)) continue;
			SavableComps.Add(C);
		}

		SavableComps.Sort([](const UActorComponent& Lc, const UActorComponent& Rc)
			{
				return Lc.GetName() < Rc.GetName();
			});

		int32 CCount = SavableComps.Num();

		Ar << Guid;
		Ar << ClassPath;
		Ar << Xf;
		Ar << ABytes;
		Ar << CCount;

		for (UActorComponent* C : SavableComps)
		{
			FString Name = C->GetFName().ToString();

			TArray<uint8> CBytes;
			{
				FMemoryWriter CW(CBytes, true);
				FOsirisWorldAr CAr(CW);
				C->Serialize(CAr);
			}

			Ar << Name;
			Ar << CBytes;
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

	static const FString GOsirisSlot = TEXT("OSIRIS_SLOT");

	UOsirisSaveGame* SG = Cast<UOsirisSaveGame>(UGameplayStatics::LoadGameFromSlot(GOsirisSlot, 0));
	if (!SG || SG->Data.Num() == 0) return false;

	struct FOsirisWorldAr : FObjectAndNameAsStringProxyArchive
	{
		FOsirisWorldAr(FArchive& Inner)
			: FObjectAndNameAsStringProxyArchive(Inner, true)
		{
			ArIsSaveGame = true;
			ArNoDelta = true;
		}
	};

	TMap<FGuid, AActor*> Map;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;

		if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
		{
			if (SC->OsirisGuid.IsValid())
				Map.Add(SC->OsirisGuid, A);
		}
	}

	FMemoryReader R(SG->Data, true);
	FOsirisWorldAr Ar(R);

	int32 Count = 0;
	Ar << Count;

	TSet<FGuid> SavedGuids;
	SavedGuids.Reserve(Count);

	bool bOk = true;

	for (int32 i = 0; i < Count; ++i)
	{
		FGuid Guid;
		FString ClassPath;
		FTransform Xf;
		TArray<uint8> ABytes;
		int32 CCount = 0;

		Ar << Guid;
		Ar << ClassPath;
		Ar << Xf;
		Ar << ABytes;
		Ar << CCount;

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

		if (!A)
		{
			for (int32 c = 0; c < CCount; ++c)
			{
				FString N; TArray<uint8> B;
				Ar << N; Ar << B;
			}
			bOk = false;
			continue;
		}

		if (ABytes.Num() > 0)
		{
			FMemoryReader AR(ABytes, true);
			FOsirisWorldAr AAr(AR);
			A->Serialize(AAr);
		}

		TArray<UActorComponent*> Comps;
		A->GetComponents(Comps);

		for (int32 c = 0; c < CCount; ++c)
		{
			FString Name;
			TArray<uint8> CBytes;

			Ar << Name;
			Ar << CBytes;

			const FName Want(*Name);
			bool bFound = false;

			for (UActorComponent* Cmp : Comps)
			{
				if (Cmp && Cmp->GetFName() == Want)
				{
					bFound = true;

					if (CBytes.Num() > 0)
					{
						FMemoryReader CR(CBytes, true);
						FOsirisWorldAr CAr(CR);
						Cmp->Serialize(CAr);
					}
					break;
				}
			}

			if (!bFound)
				bOk = false;
		}

		A->ReregisterAllComponents();
		A->SetActorTransform(Xf, false, nullptr, ETeleportType::TeleportPhysics);

		if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
			SC->GetOsirisPostLoadHook().Broadcast();
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
	{
		if (IsValid(A))
			A->Destroy();
	}

	return bOk;
}

