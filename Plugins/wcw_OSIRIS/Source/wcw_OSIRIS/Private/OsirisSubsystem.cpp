#include "OsirisSubsystem.h"

#include "OsirisSaveComponent.h"
#include "OsirisSaveGame.h"

#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/EngineTypes.h"
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

#include "Misc/PackageName.h"

static const FString GOsirisSlot = TEXT("OSIRIS_SLOT");
static const FName   GOsirisPlayerId = TEXT("OSIRIS_PLAYER_0");

static TMap<FName, TArray<uint8>> GOsirisLevelData;
static TArray<uint8>              GOsirisPlayerData;
static FName                      GOsirisRootMapId = NAME_None;

static bool                       GOsirisPending = false;
static bool                       GOsirisIgnoreStreamingCapture = false;
static FName                      GOsirisPendingRootMap = NAME_None;
static TWeakObjectPtr<UWorld>     GOsirisPendingWorld;

static TWeakObjectPtr<UWorld>     GOsirisSessionWorld;

static FDelegateHandle            GOsirisPostLoadH;
static FDelegateHandle            GOsirisTickH;
static FDelegateHandle            GOsirisPostWorldInitH;
static FDelegateHandle            GOsirisLevelAddedH;
static FDelegateHandle            GOsirisLevelRemovedH;

struct FOsirisAr : FObjectAndNameAsStringProxyArchive
{
	FOsirisAr(FArchive& Inner) : FObjectAndNameAsStringProxyArchive(Inner, true)
	{
		ArIsSaveGame = true;
		ArNoDelta = true;
	}
};

static bool OsirisHasAnySaveGameProps(const UObject* Obj)
{
	if (!Obj) return false;
	for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		if (It->HasAnyPropertyFlags(CPF_SaveGame))
			return true;
	return false;
}

static bool OsirisLessGuid(const FGuid& L, const FGuid& R)
{
	if (L.A != R.A) return L.A < R.A;
	if (L.B != R.B) return L.B < R.B;
	if (L.C != R.C) return L.C < R.C;
	return L.D < R.D;
}

static bool OsirisReady(UWorld* W)
{
	if (!W || !W->HasBegunPlay()) return false;
	APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
	APawn* P = PC ? PC->GetPawn() : nullptr;
	return PC && P && P->GetController() == PC;
}

static FName OsirisGetRootMapId(UWorld* World)
{
	if (!World) return NAME_None;
	return FName(*UGameplayStatics::GetCurrentLevelName(World, true));
}

static FName OsirisGetStreamingLevelShortId(ULevelStreaming* SL)
{
	if (!SL) return NAME_None;
	const FString Pkg = SL->GetWorldAssetPackageName();
	if (Pkg.IsEmpty()) return NAME_None;
	return FName(*FPackageName::GetShortName(Pkg));
}

static FName OsirisGetLevelId(UWorld* World, ULevel* Level)
{
	if (!World || !Level) return NAME_None;
	if (Level == World->PersistentLevel) return OsirisGetRootMapId(World);

	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		if (SL->GetLoadedLevel() == Level)
		{
			const FName Id = OsirisGetStreamingLevelShortId(SL);
			if (!Id.IsNone()) return Id;
		}
	}

	const FString Pkg = Level->GetOutermost() ? Level->GetOutermost()->GetName() : FString();
	if (Pkg.IsEmpty()) return NAME_None;
	return FName(*FPackageName::GetShortName(Pkg));
}

static bool OsirisIsStreamingLevelVisible(UWorld* World, ULevel* Level)
{
	if (!World || !Level) return false;
	if (Level == World->PersistentLevel) return true;

	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		if (SL->GetLoadedLevel() == Level)
			return SL->IsLevelLoaded() && SL->IsLevelVisible();
	}

	return false;
}

static void OsirisEnsureRoot(UWorld* World)
{
	if (!World) return;
	const FName Cur = OsirisGetRootMapId(World);
	if (Cur.IsNone()) return;

	if (GOsirisRootMapId.IsNone())
	{
		GOsirisRootMapId = Cur;
		return;
	}

	if (GOsirisRootMapId != Cur)
	{
		GOsirisRootMapId = Cur;
		if (!GOsirisPending)
		{
			GOsirisLevelData.Reset();
			GOsirisPlayerData.Reset();
		}
	}
}

static void OsirisResetForNewSession(UWorld* World)
{
	if (!World || !World->IsGameWorld()) return;
	if (GOsirisPending) return;

	if (!GOsirisSessionWorld.IsValid() || GOsirisSessionWorld.Get() != World)
	{
		GOsirisSessionWorld = World;
		GOsirisRootMapId = OsirisGetRootMapId(World);
		GOsirisLevelData.Reset();
		GOsirisPlayerData.Reset();
		GOsirisIgnoreStreamingCapture = false;
		GOsirisPending = false;
		GOsirisPendingRootMap = NAME_None;
		GOsirisPendingWorld.Reset();
	}
}

static bool OsirisCapturePlayer(UWorld* World, TArray<uint8>& Out)
{
	Out.Reset();
	if (!World) return false;

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn) return true;

	UOsirisSaveComponent* SC = Pawn->FindComponentByClass<UOsirisSaveComponent>();
	if (!SC) return true;
	if (!SC->OsirisGuid.IsValid()) SC->OsirisGuid = FGuid::NewGuid();

	FMemoryWriter W(Out, true);
	FOsirisAr Ar(W);

	int32 Count = 1;
	Ar << Count;

	SC->GetOsirisPreSaveHook().Broadcast();

	FGuid Guid = SC->OsirisGuid;
	FString ClassPath = Pawn->GetClass()->GetPathName();
	FTransform Xf = Pawn->GetActorTransform();

	TArray<uint8> ABytes;
	{
		FMemoryWriter AW(ABytes, true);
		FOsirisAr AAr(AW);
		Pawn->Serialize(AAr);
	}

	TArray<UActorComponent*> AllComps; Pawn->GetComponents(AllComps);
	TArray<UActorComponent*> SavableComps; SavableComps.Reserve(AllComps.Num());

	for (UActorComponent* C : AllComps)
		if (C && !C->HasAnyFlags(RF_Transient) && OsirisHasAnySaveGameProps(C))
			SavableComps.Add(C);

	SavableComps.Sort([](const UActorComponent& L, const UActorComponent& R) { return L.GetName() < R.GetName(); });

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
			FOsirisAr CAr(CW);
			C->Serialize(CAr);
		}

		Ar << Name;
		Ar << CBytes;
	}

	return true;
}

static bool OsirisCaptureLevel(UWorld* World, ULevel* Level, TArray<uint8>& Out)
{
	Out.Reset();
	if (!World || !Level) return false;

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	TArray<AActor*> Actors;
	Actors.Reserve(Level->Actors.Num());

	for (AActor* A : Level->Actors)
	{
		if (!A || A == Pawn) continue;
		UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>();
		if (!SC) continue;
		if (!SC->OsirisGuid.IsValid()) SC->OsirisGuid = FGuid::NewGuid();
		Actors.Add(A);
	}

	Actors.Sort([](const AActor& L, const AActor& R)
		{
			const UOsirisSaveComponent* LSC = L.FindComponentByClass<UOsirisSaveComponent>();
			const UOsirisSaveComponent* RSC = R.FindComponentByClass<UOsirisSaveComponent>();
			const FGuid LG = (LSC && LSC->OsirisGuid.IsValid()) ? LSC->OsirisGuid : FGuid();
			const FGuid RG = (RSC && RSC->OsirisGuid.IsValid()) ? RSC->OsirisGuid : FGuid();
			return OsirisLessGuid(LG, RG);
		});

	FMemoryWriter W(Out, true);
	FOsirisAr Ar(W);

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
			FOsirisAr AAr(AW);
			A->Serialize(AAr);
		}

		TArray<UActorComponent*> AllComps; A->GetComponents(AllComps);
		TArray<UActorComponent*> SavableComps; SavableComps.Reserve(AllComps.Num());

		for (UActorComponent* C : AllComps)
			if (C && !C->HasAnyFlags(RF_Transient) && OsirisHasAnySaveGameProps(C))
				SavableComps.Add(C);

		SavableComps.Sort([](const UActorComponent& L, const UActorComponent& R) { return L.GetName() < R.GetName(); });

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
				FOsirisAr CAr(CW);
				C->Serialize(CAr);
			}

			Ar << Name;
			Ar << CBytes;
		}
	}

	return true;
}

static bool OsirisApplyToLevel(UWorld* World, ULevel* Level, const TArray<uint8>& In, bool bSpawn, bool bDestroy, AActor* Forced)
{
	if (!World || !Level) return false;
	if (In.Num() == 0) return true;

	TMap<FGuid, AActor*> Map;
	if (!Forced)
	{
		for (AActor* A : Level->Actors)
		{
			if (!A) continue;
			if (APawn* P = Cast<APawn>(A); P && P->IsPlayerControlled()) continue;

			if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
				if (SC->OsirisGuid.IsValid())
					Map.Add(SC->OsirisGuid, A);
		}
	}

	FMemoryReader R(In, true);
	FOsirisAr Ar(R);

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
				P.OverrideLevel = Level;

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
			if (bSpawn) bOk = false;
			continue;
		}

		if (ABytes.Num())
		{
			FMemoryReader AR(ABytes, true);
			FOsirisAr AAr(AR);
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
						FOsirisAr CAr(CR);
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

	if (bDestroy && !Forced)
	{
		TArray<AActor*> ToDestroy;

		for (AActor* A : Level->Actors)
		{
			if (!A) continue;
			if (APawn* P = Cast<APawn>(A); P && P->IsPlayerControlled()) continue;

			UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>();
			if (!SC || !SC->OsirisGuid.IsValid()) continue;

			if (!SavedGuids.Contains(SC->OsirisGuid))
				ToDestroy.Add(A);
		}

		for (AActor* A : ToDestroy)
			if (IsValid(A))
				A->Destroy();
	}

	return bOk;
}

static void OsirisClearPending()
{
	GOsirisPending = false;
	GOsirisPendingRootMap = NAME_None;
	GOsirisPendingWorld.Reset();
	GOsirisIgnoreStreamingCapture = false;
}

static void OsirisEnsureHandlers()
{
	if (!GOsirisPostLoadH.IsValid())
	{
		GOsirisPostLoadH = FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda([](UWorld* W)
			{
				if (!W || !W->IsGameWorld()) return;

				const FName CurRoot = OsirisGetRootMapId(W);

				if (GOsirisPending)
				{
					if (CurRoot == GOsirisPendingRootMap)
						GOsirisPendingWorld = W;
					return;
				}

				OsirisEnsureRoot(W);
			});
	}

	if (!GOsirisPostWorldInitH.IsValid())
	{
		GOsirisPostWorldInitH = FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* W, const UWorld::InitializationValues)
			{
				OsirisResetForNewSession(W);
			});
	}

	if (!GOsirisTickH.IsValid())
	{
		GOsirisTickH = FWorldDelegates::OnWorldTickStart.AddLambda([](UWorld* W, ELevelTick, float)
			{
				if (!GOsirisPending || !W || !W->IsGameWorld()) return;
				if (GOsirisPendingWorld.IsValid() && W != GOsirisPendingWorld.Get()) return;
				if (OsirisGetRootMapId(W) != GOsirisPendingRootMap) return;
				if (!OsirisReady(W)) return;

				OsirisEnsureRoot(W);

				if (W->PersistentLevel)
				{
					const FName Pid = OsirisGetLevelId(W, W->PersistentLevel);
					if (const TArray<uint8>* Bytes = GOsirisLevelData.Find(Pid))
						OsirisApplyToLevel(W, W->PersistentLevel, *Bytes, true, true, nullptr);
				}

				for (ULevelStreaming* SL : W->GetStreamingLevels())
				{
					if (!SL || !SL->IsLevelLoaded() || !SL->IsLevelVisible()) continue;
					ULevel* Lvl = SL->GetLoadedLevel();
					if (!Lvl) continue;

					const FName Lid = OsirisGetLevelId(W, Lvl);
					if (const TArray<uint8>* Bytes = GOsirisLevelData.Find(Lid))
						OsirisApplyToLevel(W, Lvl, *Bytes, true, true, nullptr);
				}

				if (GOsirisPlayerData.Num())
				{
					APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
					APawn* Pawn = PC ? PC->GetPawn() : nullptr;
					if (Pawn)
					{
						ULevel* Lvl = Pawn->GetLevel() ? Pawn->GetLevel() : W->PersistentLevel;
						if (Lvl) OsirisApplyToLevel(W, Lvl, GOsirisPlayerData, false, false, Pawn);
					}
				}

				OsirisClearPending();
			});
	}

	if (!GOsirisLevelAddedH.IsValid())
	{
		GOsirisLevelAddedH = FWorldDelegates::LevelAddedToWorld.AddLambda([](ULevel* Level, UWorld* World)
			{
				if (!World || !World->IsGameWorld() || !World->HasBegunPlay()) return;
				if (!Level) return;
				if (GOsirisPending) return;

				OsirisEnsureRoot(World);

				if (Level == World->PersistentLevel) return;
				if (!OsirisIsStreamingLevelVisible(World, Level)) return;

				const FName Lid = OsirisGetLevelId(World, Level);
				if (const TArray<uint8>* Bytes = GOsirisLevelData.Find(Lid))
					OsirisApplyToLevel(World, Level, *Bytes, true, true, nullptr);
			});
	}

	if (!GOsirisLevelRemovedH.IsValid())
	{
		GOsirisLevelRemovedH = FWorldDelegates::LevelRemovedFromWorld.AddLambda([](ULevel* Level, UWorld* World)
			{
				if (!World || !World->IsGameWorld() || !World->HasBegunPlay()) return;
				if (!Level) return;
				if (GOsirisPending || GOsirisIgnoreStreamingCapture) return;
				if (Level == World->PersistentLevel) return;

				OsirisEnsureRoot(World);

				TArray<uint8> Bytes;
				if (OsirisCaptureLevel(World, Level, Bytes))
					GOsirisLevelData.FindOrAdd(OsirisGetLevelId(World, Level)) = MoveTemp(Bytes);
			});
	}
}

struct FOsirisAutoInit
{
	FOsirisAutoInit() { OsirisEnsureHandlers(); }
};

static FOsirisAutoInit GOsirisAutoInit;

bool UOsirisSubsystem::SaveGame()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return false;

	OsirisEnsureHandlers();
	OsirisEnsureRoot(World);

	if (World->PersistentLevel)
	{
		TArray<uint8> Bytes;
		if (OsirisCaptureLevel(World, World->PersistentLevel, Bytes))
			GOsirisLevelData.FindOrAdd(OsirisGetLevelId(World, World->PersistentLevel)) = MoveTemp(Bytes);
	}

	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL || !SL->IsLevelLoaded() || !SL->IsLevelVisible()) continue;
		ULevel* Lvl = SL->GetLoadedLevel();
		if (!Lvl) continue;

		TArray<uint8> Bytes;
		if (OsirisCaptureLevel(World, Lvl, Bytes))
			GOsirisLevelData.FindOrAdd(OsirisGetLevelId(World, Lvl)) = MoveTemp(Bytes);
	}

	{
		TArray<uint8> PBytes;
		if (OsirisCapturePlayer(World, PBytes))
			GOsirisPlayerData = MoveTemp(PBytes);
	}

	UOsirisSaveGame* SG = Cast<UOsirisSaveGame>(UGameplayStatics::CreateSaveGameObject(UOsirisSaveGame::StaticClass()));
	if (!SG) return false;

	SG->RootMapId = OsirisGetRootMapId(World);
	SG->DataContainers.Reset(GOsirisLevelData.Num() + 1);

	TArray<FName> Keys;
	Keys.Reserve(GOsirisLevelData.Num());
	for (const TPair<FName, TArray<uint8>>& Kvp : GOsirisLevelData) Keys.Add(Kvp.Key);
	Keys.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });

	for (const FName& K : Keys)
	{
		FOsirisDataContainer C;
		C.LevelId = K;
		C.Data = GOsirisLevelData.FindChecked(K);
		SG->DataContainers.Add(MoveTemp(C));
	}

	{
		FOsirisDataContainer P;
		P.LevelId = GOsirisPlayerId;
		P.Data = GOsirisPlayerData;
		SG->DataContainers.Add(MoveTemp(P));
	}

	return UGameplayStatics::SaveGameToSlot(SG, GOsirisSlot, 0);
}

bool UOsirisSubsystem::LoadGame()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return false;

	OsirisEnsureHandlers();

	UOsirisSaveGame* SG = Cast<UOsirisSaveGame>(UGameplayStatics::LoadGameFromSlot(GOsirisSlot, 0));
	if (!SG || SG->RootMapId.IsNone()) return false;

	GOsirisRootMapId = SG->RootMapId;

	GOsirisLevelData.Reset();
	GOsirisPlayerData.Reset();

	for (const FOsirisDataContainer& C : SG->DataContainers)
	{
		if (C.LevelId == GOsirisPlayerId)
		{
			GOsirisPlayerData = C.Data;
		}
		else if (!C.LevelId.IsNone())
		{
			GOsirisLevelData.FindOrAdd(C.LevelId) = C.Data;
		}
	}

	GOsirisPending = true;
	GOsirisIgnoreStreamingCapture = true;
	GOsirisPendingRootMap = SG->RootMapId;
	GOsirisPendingWorld = (OsirisGetRootMapId(World) == GOsirisPendingRootMap) ? World : nullptr;

	if (OsirisGetRootMapId(World) != GOsirisPendingRootMap)
	{
		UGameplayStatics::OpenLevel(this, GOsirisPendingRootMap);
		return true;
	}

	if (OsirisReady(World))
	{
		bool bOk = true;

		OsirisEnsureRoot(World);

		if (World->PersistentLevel)
		{
			const FName Pid = OsirisGetLevelId(World, World->PersistentLevel);
			if (const TArray<uint8>* Bytes = GOsirisLevelData.Find(Pid))
				bOk = OsirisApplyToLevel(World, World->PersistentLevel, *Bytes, true, true, nullptr) && bOk;
		}

		for (ULevelStreaming* SL : World->GetStreamingLevels())
		{
			if (!SL || !SL->IsLevelLoaded() || !SL->IsLevelVisible()) continue;
			ULevel* Lvl = SL->GetLoadedLevel();
			if (!Lvl) continue;

			const FName Lid = OsirisGetLevelId(World, Lvl);
			if (const TArray<uint8>* Bytes = GOsirisLevelData.Find(Lid))
				bOk = OsirisApplyToLevel(World, Lvl, *Bytes, true, true, nullptr) && bOk;
		}

		if (GOsirisPlayerData.Num())
		{
			APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (Pawn)
			{
				ULevel* Lvl = Pawn->GetLevel() ? Pawn->GetLevel() : World->PersistentLevel;
				if (Lvl) bOk = OsirisApplyToLevel(World, Lvl, GOsirisPlayerData, false, false, Pawn) && bOk;
			}
		}

		OsirisClearPending();
		return bOk;
	}

	return true;
}
