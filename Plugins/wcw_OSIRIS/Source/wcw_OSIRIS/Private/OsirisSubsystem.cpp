#include "OsirisSubsystem.h"

#include "OsirisSaveComponent.h"
#include "OsirisSaveGame.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Components/ActorComponent.h"

#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

static bool GOsirisPending = false;
static FName GOsirisPendingMap = NAME_None;
static TArray<uint8> GOsirisPendingLevel;
static TArray<uint8> GOsirisPendingPlayer;
static TWeakObjectPtr<UOsirisSubsystem> GOsirisPendingSubsystem;
static TWeakObjectPtr<UWorld> GOsirisPendingWorld;
static FDelegateHandle GOsirisPostLoadH;
static FDelegateHandle GOsirisTickH;

static bool OsirisReady(UWorld* W)
{
	if (!W || !W->HasBegunPlay()) return false;
	APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
	APawn* P = PC ? PC->GetPawn() : nullptr;
	return PC && P && P->GetController() == PC;
}

static bool OsirisApplyData(UWorld* World, const TArray<uint8>& In, bool bSpawn, bool bDestroy, AActor* Forced)
{
	if (!World || In.Num() == 0) return true;

	struct FOsirisWorldAr : FObjectAndNameAsStringProxyArchive
	{
		FOsirisWorldAr(FArchive& Inner) : FObjectAndNameAsStringProxyArchive(Inner, true) { ArIsSaveGame = true; ArNoDelta = true; }
	};

	const auto HasAnySaveGameProps = [](const UObject* Obj)
	{
		if (!Obj) return false;
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It) if (It->HasAnyPropertyFlags(CPF_SaveGame)) return true;
		return false;
	};

	TMap<FGuid, AActor*> Map;
	if (!Forced)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It; if (!A) continue;
			if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
				if (SC->OsirisGuid.IsValid())
					Map.Add(SC->OsirisGuid, A);
		}
	}

	FMemoryReader R(In, true);
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

		AActor* A = Forced ? Forced : Map.FindRef(Guid);

		if (!A && bSpawn)
		{
			if (UClass* Cls = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath))
			{
				FActorSpawnParameters P;
				P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				A = World->SpawnActor<AActor>(Cls, Xf, P);
				if (A)
				{
					if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>()) SC->SetOsirisGuid(Guid);
					Map.Add(Guid, A);
				}
			}
		}

		if (!A)
		{
			for (int32 c = 0; c < CCount; ++c) { FString N; TArray<uint8> B; Ar << N; Ar << B; }
			if (bSpawn) bOk = false;
			continue;
		}

		if (ABytes.Num())
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
					if (CBytes.Num())
					{
						FMemoryReader CR(CBytes, true);
						FOsirisWorldAr CAr(CR);
						Cmp->Serialize(CAr);
					}
					break;
				}
			}

			if (!bFound && bSpawn) bOk = false;
		}

		A->ReregisterAllComponents();
		A->SetActorTransform(Xf, false, nullptr, ETeleportType::TeleportPhysics);

		if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
			SC->GetOsirisPostLoadHook().Broadcast();
	}

	if (bDestroy)
	{
		TArray<AActor*> ToDestroy;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It; if (!A) continue;
			if (APawn* P = Cast<APawn>(A); P && P->IsPlayerControlled()) continue;

			UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>();
			if (!SC || !SC->OsirisGuid.IsValid()) continue;

			if (!SavedGuids.Contains(SC->OsirisGuid)) ToDestroy.Add(A);
		}
		for (AActor* A : ToDestroy) if (IsValid(A)) A->Destroy();
	}

	return bOk;
}

static void OsirisClearPending()
{
	if (GOsirisPostLoadH.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(GOsirisPostLoadH);
		GOsirisPostLoadH.Reset();
	}
	if (GOsirisTickH.IsValid())
	{
		FWorldDelegates::OnWorldTickStart.Remove(GOsirisTickH);
		GOsirisTickH.Reset();
	}
	GOsirisPending = false;
	GOsirisPendingMap = NAME_None;
	GOsirisPendingLevel.Reset();
	GOsirisPendingPlayer.Reset();
	GOsirisPendingSubsystem.Reset();
	GOsirisPendingWorld.Reset();
}

static void OsirisEnsureHandlers()
{
	if (!GOsirisPostLoadH.IsValid())
	{
		GOsirisPostLoadH = FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda([](UWorld* W)
			{
				if (!GOsirisPending || !W) return;
				if (FName(*UGameplayStatics::GetCurrentLevelName(W, true)) != GOsirisPendingMap) return;
				GOsirisPendingWorld = W;
			});
	}

	if (!GOsirisTickH.IsValid())
	{
		GOsirisTickH = FWorldDelegates::OnWorldTickStart.AddLambda([](UWorld* W, ELevelTick, float)
			{
				if (!GOsirisPending || !W) return;
				if (GOsirisPendingWorld.IsValid() && W != GOsirisPendingWorld.Get()) return;
				if (FName(*UGameplayStatics::GetCurrentLevelName(W, true)) != GOsirisPendingMap) return;
				if (!OsirisReady(W)) return;

				UOsirisSubsystem* S = GOsirisPendingSubsystem.Get();
				if (!S) { OsirisClearPending(); return; }

				bool bOk = OsirisApplyData(W, GOsirisPendingLevel, true, true, nullptr);

				if (GOsirisPendingPlayer.Num())
				{
					APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
					APawn* P = PC ? PC->GetPawn() : nullptr;
					if (P) bOk = OsirisApplyData(W, GOsirisPendingPlayer, false, false, P) && bOk;
				}

				OsirisClearPending();
			});
	}
}

bool UOsirisSubsystem::SaveGame()
{
	UWorld* World = GetWorld();
	if (!World) return false;

	static const FString GOsirisSlot = TEXT("OSIRIS_SLOT");
	static const FName PlayerId = TEXT("OSIRIS_PLAYER_0");

	struct FOsirisWorldAr : FObjectAndNameAsStringProxyArchive
	{
		FOsirisWorldAr(FArchive& Inner) : FObjectAndNameAsStringProxyArchive(Inner, true) { ArIsSaveGame = true; ArNoDelta = true; }
	};

	const auto HasAnySaveGameProps = [](const UObject* Obj)
	{
		if (!Obj) return false;
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It) if (It->HasAnyPropertyFlags(CPF_SaveGame)) return true;
		return false;
	};

	const auto LessGuid = [](const FGuid& L, const FGuid& R)
	{
		if (L.A != R.A) return L.A < R.A;
		if (L.B != R.B) return L.B < R.B;
		if (L.C != R.C) return L.C < R.C;
		return L.D < R.D;
	};

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	TArray<AActor*> LevelActors; LevelActors.Reserve(256);
	TArray<AActor*> PlayerActors; PlayerActors.Reserve(1);

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It; if (!A) continue;
		if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
		{
			if (!SC->OsirisGuid.IsValid()) SC->OsirisGuid = FGuid::NewGuid();
			(A == Pawn) ? PlayerActors.Add(A) : LevelActors.Add(A);
		}
	}

	const auto SortByGuid = [&](TArray<AActor*>& Arr)
	{
		Arr.Sort([&](const AActor& L, const AActor& R)
			{
				const UOsirisSaveComponent* LSC = L.FindComponentByClass<UOsirisSaveComponent>();
				const UOsirisSaveComponent* RSC = R.FindComponentByClass<UOsirisSaveComponent>();
				const FGuid LG = (LSC && LSC->OsirisGuid.IsValid()) ? LSC->OsirisGuid : FGuid();
				const FGuid RG = (RSC && RSC->OsirisGuid.IsValid()) ? RSC->OsirisGuid : FGuid();
				return LessGuid(LG, RG);
			});
	};

	SortByGuid(LevelActors);
	SortByGuid(PlayerActors);

	const auto WriteActors = [&](const TArray<AActor*>& Actors, TArray<uint8>& Out)
	{
		FMemoryWriter W(Out, true);
		FOsirisWorldAr Ar(W);

		int32 Count = Actors.Num();
		Ar << Count;

		for (AActor* A : Actors)
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

			TArray<UActorComponent*> AllComps; A->GetComponents(AllComps);
			TArray<UActorComponent*> SavableComps; SavableComps.Reserve(AllComps.Num());

			for (UActorComponent* C : AllComps)
				if (C && !C->HasAnyFlags(RF_Transient) && HasAnySaveGameProps(C))
					SavableComps.Add(C);

			SavableComps.Sort([](const UActorComponent& Lc, const UActorComponent& Rc) { return Lc.GetName() < Rc.GetName(); });

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
	};

	TArray<uint8> LevelBytes, PlayerBytes;
	WriteActors(LevelActors, LevelBytes);
	if (PlayerActors.Num()) WriteActors(PlayerActors, PlayerBytes);

	UOsirisSaveGame* SG = Cast<UOsirisSaveGame>(UGameplayStatics::CreateSaveGameObject(UOsirisSaveGame::StaticClass()));
	if (!SG) return false;

	SG->DataContainers.Reset(2);

	FOsirisDataContainer L;
	L.WorldId = FName(*UGameplayStatics::GetCurrentLevelName(World, true));
	L.Data = MoveTemp(LevelBytes);
	SG->DataContainers.Add(MoveTemp(L));

	if (PlayerBytes.Num())
	{
		FOsirisDataContainer P;
		P.WorldId = PlayerId;
		P.Data = MoveTemp(PlayerBytes);
		SG->DataContainers.Add(MoveTemp(P));
	}

	return UGameplayStatics::SaveGameToSlot(SG, GOsirisSlot, 0);
}

bool UOsirisSubsystem::LoadGame()
{
	UWorld* World = GetWorld();
	if (!World) return false;

	static const FString GOsirisSlot = TEXT("OSIRIS_SLOT");
	static const FName PlayerId = TEXT("OSIRIS_PLAYER_0");

	UOsirisSaveGame* SG = Cast<UOsirisSaveGame>(UGameplayStatics::LoadGameFromSlot(GOsirisSlot, 0));
	if (!SG) return false;

	const FOsirisDataContainer* LevelC = SG->DataContainers.FindByPredicate([&](const FOsirisDataContainer& C) { return C.WorldId != PlayerId && C.Data.Num(); });
	const FOsirisDataContainer* PlayerC = SG->DataContainers.FindByPredicate([&](const FOsirisDataContainer& C) { return C.WorldId == PlayerId && C.Data.Num(); });
	if (!LevelC) return false;

	const FName Target = LevelC->WorldId;
	const FName Cur = FName(*UGameplayStatics::GetCurrentLevelName(World, true));

	GOsirisPending = true;
	GOsirisPendingSubsystem = this;
	GOsirisPendingMap = Target;
	GOsirisPendingLevel = LevelC->Data;
	GOsirisPendingPlayer = PlayerC ? PlayerC->Data : TArray<uint8>();
	GOsirisPendingWorld = (Cur == Target) ? World : nullptr;

	OsirisEnsureHandlers();

	if (Cur != Target)
	{
		UGameplayStatics::OpenLevel(this, Target);
		return true;
	}

	if (OsirisReady(World))
	{
		bool bOk = OsirisApplyData(World, GOsirisPendingLevel, true, true, nullptr);
		if (GOsirisPendingPlayer.Num())
		{
			APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
			APawn* P = PC ? PC->GetPawn() : nullptr;
			if (P) bOk = OsirisApplyData(World, GOsirisPendingPlayer, false, false, P) && bOk;
		}
		OsirisClearPending();
		return bOk;
	}

	return true;
}
