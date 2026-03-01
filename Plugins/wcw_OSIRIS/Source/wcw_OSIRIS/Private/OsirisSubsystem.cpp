//© Developer Nikita Petrachkov in collaboration with WINTER CROWN WORKS, all rights reserved ©

#pragma once

#include "OsirisSubsystem.h"

#include "OsirisSaveComponent.h"
#include "OsirisSaveGame.h"

#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "Misc/Crc.h"

#include "Components/SceneComponent.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"

static const FName GId_ActorDB = TEXT("OSIRIS_ACTOR_DB");
static const FName GId_Player = TEXT("OSIRIS_PLAYER");

static const FName GTag_OsirisKeep = TEXT("OsirisKeep");
static const FName GTag_OSIRIS_KEEP = TEXT("OSIRIS_KEEP");
static const FName GTag_OsirisNoDestroy = TEXT("OsirisNoDestroy");

static const FString GOsirisDefaultProfile = TEXT("OSIRIS");
static const FString GOsirisDefaultSlot = TEXT("OSIRIS");

static constexpr int32  GOsiris_DB_Version = 2;
static constexpr int32  GMaxActorRecords = 500000;
static constexpr int32  GMaxCompPerActor = 4096;
static constexpr int32  GMaxBytes_Actor = 32 * 1024 * 1024;
static constexpr int32  GMaxBytes_Comp = 16 * 1024 * 1024;
static constexpr int32  GMaxBytes_PlayerBlob = 64 * 1024 * 1024;

static constexpr double GQuietSeconds = 1.0;
static constexpr double GLoadDeadlineSeconds = 30.0;

static constexpr uint32 GPlayerMagic = 0x4F535056;
static constexpr int32  GPlayerVersion = 2;

static FString NormalizeOrDefault(const FString& In, const FString& DefaultValue)
{
	FString S = In;
	S.TrimStartAndEndInline();
	return S.IsEmpty() ? DefaultValue : S;
}

static FString MakeToken(const FString& In, TCHAR Prefix)
{
	FString S = In;
	S.TrimStartAndEndInline();
	if (S.IsEmpty())
		S = TEXT("OSIRIS");

	bool bAllSafe = true;
	FString Out;
	Out.Reserve(S.Len());

	for (TCHAR Ch : S)
	{
		const bool bSafe =
			(Ch >= TEXT('0') && Ch <= TEXT('9')) ||
			(Ch >= TEXT('A') && Ch <= TEXT('Z')) ||
			(Ch >= TEXT('a') && Ch <= TEXT('z')) ||
			(Ch == TEXT('_'));

		if (bSafe) Out.AppendChar(Ch);
		else        bAllSafe = false;
	}

	if (bAllSafe && !Out.IsEmpty())
	{
		if (Out.Len() > 32) Out.LeftInline(32);
		return Out;
	}

	const uint32 H = FCrc::StrCrc32(*S);
	return FString::Printf(TEXT("%c%08X"), Prefix, H);
}

static FString MakeOsirisSlotName_Internal(const FString& ProfileName, const FString& SlotName)
{
	const FString P = MakeToken(ProfileName, TEXT('P'));
	const FString S = MakeToken(SlotName, TEXT('S'));
	return FString::Printf(TEXT("OSIRIS_%s_%s"), *P, *S);
}

struct FOsirisObjAr : FObjectAndNameAsStringProxyArchive
{
	FOsirisObjAr(FArchive& Inner)
		: FObjectAndNameAsStringProxyArchive(Inner, true)
	{
		ArIsSaveGame = true;
		ArNoDelta = true;
	}
};

static void WriteGuid(FArchive& Ar, const FGuid& G)
{
	uint32 A = G.A, B = G.B, C = G.C, D = G.D;
	Ar << A; Ar << B; Ar << C; Ar << D;
}

static void ReadGuid(FArchive& Ar, FGuid& G)
{
	uint32 A = 0, B = 0, C = 0, D = 0;
	Ar << A; Ar << B; Ar << C; Ar << D;
	G = FGuid(A, B, C, D);
}

static void WriteNameStr(FArchive& Ar, const FName& N)
{
	FString S = N.IsNone() ? FString() : N.ToString();
	Ar << S;
}

static void ReadNameStr(FArchive& Ar, FName& N)
{
	FString S;
	Ar << S;
	N = S.IsEmpty() ? NAME_None : FName(*S);
}

static void WriteBool(FArchive& Ar, bool b)
{
	uint8 V = b ? 1 : 0;
	Ar << V;
}

static bool ReadBool(FArchive& Ar, bool& bOut)
{
	uint8 V = 0;
	Ar << V;
	if (Ar.IsError()) return false;
	bOut = (V != 0);
	return true;
}

static void WriteBytes(FArchive& Ar, const TArray<uint8>& Bytes)
{
	int32 Size = Bytes.Num();
	Ar << Size;
	if (Size > 0) Ar.Serialize((void*)Bytes.GetData(), Size);
}

static bool ReadBytes(FArchive& Ar, TArray<uint8>& Out, int32 MaxAllowed)
{
	int32 Size = 0;
	Ar << Size;
	if (Ar.IsError()) return false;
	if (Size < 0 || Size > MaxAllowed) return false;

	Out.Reset();
	if (Size == 0) return true;

	Out.SetNumUninitialized(Size);
	Ar.Serialize(Out.GetData(), Size);
	return !Ar.IsError();
}

static bool IsDynamicSavableComponent(const UActorComponent* C)
{
	if (!C) return false;
	if (C->IsDefaultSubobject()) return false;

	const AActor* Owner = C->GetOwner();
	if (Owner)
	{
		const TArray<UActorComponent*>& IC = Owner->GetInstanceComponents();
		if (IC.Contains(C)) return true;
	}

	return (C->CreationMethod == EComponentCreationMethod::Instance) ||
		(C->CreationMethod == EComponentCreationMethod::UserConstructionScript);
}

static bool HasAnySaveGameProps(const UObject* Obj)
{
	if (!Obj) return false;
	for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		if (It->HasAnyPropertyFlags(CPF_SaveGame))
			return true;
	return false;
}

static bool ShouldSaveComponent(const UActorComponent* C)
{
	if (!C)                          return false;
	if (C->HasAnyFlags(RF_Transient)) return false;
	return HasAnySaveGameProps(C) || IsDynamicSavableComponent(C);
}

static bool IsWorldReady(UWorld* World)
{
	if (!World || !World->IsGameWorld() || !World->HasBegunPlay()) return false;
	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC) return false;
	APawn* P = PC->GetPawn();
	if (!P) return false;
	return (P->GetController() == PC);
}

static FString StripUEDPIEPrefix(FString In)
{
	auto StripInNamePart = [](const FString& NamePart) -> FString
	{
		if (!NamePart.StartsWith(TEXT("UEDPIE_"))) return NamePart;
		int32 i = 7;
		while (i < NamePart.Len() && FChar::IsDigit(NamePart[i])) ++i;
		if (i < NamePart.Len() && NamePart[i] == TEXT('_')) ++i;
		return NamePart.Mid(i);
	};

	int32 Slash = INDEX_NONE;
	if (In.FindLastChar(TEXT('/'), Slash))
	{
		const FString Path = In.Left(Slash + 1);
		const FString Name = In.Mid(Slash + 1);
		return Path + StripInNamePart(Name);
	}
	return StripInNamePart(In);
}

static FString NormalizePackageLikeName(FString In)
{
	In = StripUEDPIEPrefix(MoveTemp(In));
	UWorld::RemovePIEPrefix(In);
	return In;
}

static FName MakeNameFromStringNormalized(const FString& S)
{
	const FString N = NormalizePackageLikeName(S);
	return N.IsEmpty() ? NAME_None : FName(*N);
}

static FName GetRootMapIdCanonical(UWorld* World)
{
	if (!World) return NAME_None;

	UPackage* Pkg = nullptr;
	if (World->PersistentLevel) Pkg = World->PersistentLevel->GetOutermost();
	if (!Pkg) Pkg = World->GetOutermost();

	FString Name = Pkg ? Pkg->GetName() : UGameplayStatics::GetCurrentLevelName(World, true);
	Name = NormalizePackageLikeName(MoveTemp(Name));
	return Name.IsEmpty() ? NAME_None : FName(*Name);
}

static FName GetRootMapIdLegacyShort(UWorld* World)
{
	if (!World) return NAME_None;
	FString Short = UGameplayStatics::GetCurrentLevelName(World, true);
	Short = NormalizePackageLikeName(MoveTemp(Short));
	return Short.IsEmpty() ? NAME_None : FName(*Short);
}

static void GetRootAliases(UWorld* World, TArray<FName>& OutAliases)
{
	OutAliases.Reset();
	if (!World) return;

	const FName Canon = GetRootMapIdCanonical(World);
	if (!Canon.IsNone()) OutAliases.Add(Canon);

	const FName LegacyShort = GetRootMapIdLegacyShort(World);
	if (!LegacyShort.IsNone() && LegacyShort != Canon) OutAliases.Add(LegacyShort);

	if (!Canon.IsNone())
	{
		const FString CanonStr = Canon.ToString();
		const FString ShortPkg = FPackageName::GetShortName(CanonStr);
		if (!ShortPkg.IsEmpty())
		{
			const FName ShortName(*ShortPkg);
			if (ShortName != Canon && !OutAliases.Contains(ShortName))
				OutAliases.Add(ShortName);
		}
	}
}

static bool DoesWorldMatchPendingRoot(UWorld* World, const FName PendingRoot)
{
	if (!World || PendingRoot.IsNone()) return false;

	TArray<FName> Aliases;
	GetRootAliases(World, Aliases);

	for (const FName& A : Aliases)
		if (A == PendingRoot)
			return true;

	const FString P = NormalizePackageLikeName(PendingRoot.ToString());
	for (const FName& A : Aliases)
		if (NormalizePackageLikeName(A.ToString()) == P)
			return true;

	return false;
}

static FName GetContainerIdCanonical(UWorld* World, ULevel* Level)
{
	if (!World || !Level) return NAME_None;

	UPackage* Pkg = Level->GetOutermost();
	if (!Pkg)
	{
		if (Level == World->PersistentLevel) return GetRootMapIdCanonical(World);
		return NAME_None;
	}

	FString Name = Pkg->GetName();
	Name = NormalizePackageLikeName(MoveTemp(Name));
	return Name.IsEmpty() ? NAME_None : FName(*Name);
}

static void GetContainerAliases(UWorld* World, ULevel* Level, TArray<FName>& OutAliases)
{
	OutAliases.Reset();
	if (!World || !Level) return;

	const FName Canon = GetContainerIdCanonical(World, Level);
	if (!Canon.IsNone()) OutAliases.Add(Canon);

	if (Level == World->PersistentLevel)
	{
		const FName LegacyShort = GetRootMapIdLegacyShort(World);
		if (!LegacyShort.IsNone() && LegacyShort != Canon)
			OutAliases.Add(LegacyShort);
	}

	if (!Canon.IsNone())
	{
		const FString CanonStr = Canon.ToString();
		const FString ShortPkg = FPackageName::GetShortName(CanonStr);
		if (!ShortPkg.IsEmpty())
		{
			const FName ShortName(*ShortPkg);
			if (ShortName != Canon && !OutAliases.Contains(ShortName))
				OutAliases.Add(ShortName);
		}
	}
}

static bool ShouldProtectFromDestroy(const AActor* A)
{
	if (!A) return false;
	return A->ActorHasTag(GTag_OsirisKeep) ||
		A->ActorHasTag(GTag_OSIRIS_KEEP) ||
		A->ActorHasTag(GTag_OsirisNoDestroy);
}

static void GatherLevelActors(UWorld* World, ULevel* Level, TArray<AActor*>& OutActors)
{
	OutActors.Reset();
	if (!Level) return;

	TSet<AActor*> Unique;
	Unique.Reserve(FMath::Max(64, Level->Actors.Num()));

	for (AActor* A : Level->Actors)
		if (A) Unique.Add(A);

	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (A && A->GetLevel() == Level)
				Unique.Add(A);
		}
	}

	OutActors.Reserve(Unique.Num());
	for (AActor* A : Unique)
		OutActors.Add(A);
}

static FName MakeLevelNameForOpenLevel(const FName& PendingRootMap)
{
	if (PendingRootMap.IsNone()) return NAME_None;

	const FString Full = PendingRootMap.ToString();
	const FString Short = FPackageName::GetShortName(Full);

	return FName(*Short);
}

struct UOsirisSubsystem::FImpl
{
	struct FCompRecord
	{
		FString Name;
		FString ClassPath;
		bool    bDynamic = false;
		TArray<uint8> Bytes;
	};

	struct FActorRecord
	{
		FName           ContainerId = NAME_None;
		FGuid           Guid;
		FString         ClassPath;
		FTransform      Transform;
		TArray<uint8>   ActorBytes;
		TArray<FCompRecord> Comps;
	};

	FName RootMapId = NAME_None;

	TMap<FGuid, FActorRecord>     ActorDB;
	TMap<FName, TArray<FGuid>>    ContainerIndex;

	TArray<uint8> PlayerBlob;

	bool  bPendingLoad = false;
	bool  bIgnoreCapture = false;
	FName PendingRootMap = NAME_None;

	double QuietSince = -1.0;
	double LoadStartTime = 0.0;

	bool bPlayerEarlyApplied = false;
	bool bPlayerFullApplied = false;

	TSet<FName> AppliedContainersDuringLoad;

	TWeakObjectPtr<UWorld> SessionWorld;

	TSharedPtr<FStreamableHandle> ClassPreloadHandle;

	FDelegateHandle H_PostLoadMap;
	FDelegateHandle H_Tick;
	FDelegateHandle H_LevelAdded;
	FDelegateHandle H_LevelRemoved;

	void Bind()
	{
		H_PostLoadMap = FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda(
			[this](UWorld* World) { OnPostLoadMap(World); });

		H_Tick = FWorldDelegates::OnWorldTickStart.AddLambda(
			[this](UWorld* World, ELevelTick TickType, float DeltaSeconds)
			{
				OnWorldTickStart(World, TickType, DeltaSeconds);
			});

		H_LevelAdded = FWorldDelegates::LevelAddedToWorld.AddLambda(
			[this](ULevel* Level, UWorld* World) { OnLevelAdded(Level, World); });

		H_LevelRemoved = FWorldDelegates::LevelRemovedFromWorld.AddLambda(
			[this](ULevel* Level, UWorld* World) { OnLevelRemoved(Level, World); });
	}

	void Unbind()
	{
		if (H_PostLoadMap.IsValid())  FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(H_PostLoadMap);
		if (H_Tick.IsValid())         FWorldDelegates::OnWorldTickStart.Remove(H_Tick);
		if (H_LevelAdded.IsValid())   FWorldDelegates::LevelAddedToWorld.Remove(H_LevelAdded);
		if (H_LevelRemoved.IsValid()) FWorldDelegates::LevelRemovedFromWorld.Remove(H_LevelRemoved);

		H_PostLoadMap.Reset();
		H_Tick.Reset();
		H_LevelAdded.Reset();
		H_LevelRemoved.Reset();
	}

	void ResetSession(UWorld* World)
	{
		SessionWorld = World;
		RootMapId = GetRootMapIdCanonical(World);

		ActorDB.Reset();
		ContainerIndex.Reset();
		PlayerBlob.Reset();

		bPendingLoad = false;
		bIgnoreCapture = false;
		PendingRootMap = NAME_None;

		QuietSince = -1.0;
		LoadStartTime = 0.0;
		bPlayerEarlyApplied = false;
		bPlayerFullApplied = false;
		AppliedContainersDuringLoad.Reset();

		ClassPreloadHandle.Reset();
	}

	void FinishPendingLoad()
	{
		bPendingLoad = false;
		bIgnoreCapture = false;
		PendingRootMap = NAME_None;
		QuietSince = -1.0;
		LoadStartTime = 0.0;
		AppliedContainersDuringLoad.Reset();
	}

	void RebuildIndexAll()
	{
		ContainerIndex.Reset();

		for (const TPair<FGuid, FActorRecord>& Kvp : ActorDB)
		{
			const FActorRecord& R = Kvp.Value;
			if (!R.ContainerId.IsNone())
				ContainerIndex.FindOrAdd(R.ContainerId).Add(Kvp.Key);
		}

		for (TPair<FName, TArray<FGuid>>& Kvp : ContainerIndex)
		{
			Kvp.Value.Sort([](const FGuid& A, const FGuid& B)
				{
					if (A.A != B.A) return A.A < B.A;
					if (A.B != B.B) return A.B < B.B;
					if (A.C != B.C) return A.C < B.C;
					return A.D < B.D;
				});
		}
	}

	void PreloadAllClasses()
	{
		ClassPreloadHandle.Reset();

		TArray<FSoftObjectPath> Paths;
		Paths.Reserve(ActorDB.Num() * 2);

		TSet<FString> Seen;
		Seen.Reserve(ActorDB.Num() * 2);

		for (const TPair<FGuid, FActorRecord>& Kvp : ActorDB)
		{
			const FActorRecord& Rec = Kvp.Value;

			if (!Rec.ClassPath.IsEmpty() && !Seen.Contains(Rec.ClassPath))
			{
				Seen.Add(Rec.ClassPath);
				Paths.Emplace(Rec.ClassPath);
			}

			for (const FCompRecord& CR : Rec.Comps)
			{
				if (CR.bDynamic && !CR.ClassPath.IsEmpty() && !Seen.Contains(CR.ClassPath))
				{
					Seen.Add(CR.ClassPath);
					Paths.Emplace(CR.ClassPath);
				}
			}
		}

		if (Paths.Num() == 0) return;

		FStreamableManager& SM = UAssetManager::GetStreamableManager();
		ClassPreloadHandle = SM.RequestAsyncLoad(
			Paths,
			FStreamableDelegate(),
			FStreamableManager::AsyncLoadHighPriority
		);

		UE_LOG(LogTemp, Log,
			TEXT("OSIRIS: Async preload started for %d unique class path(s)."),
			Paths.Num());
	}

	static bool IsDynamicSavableComponent(const UActorComponent* C)
	{
		return ::IsDynamicSavableComponent(C);
	}

	static UActorComponent* EnsureDynamicComponent(AActor* A, const FString& NameStr, const FString& ClassPath)
	{
		if (!A || NameStr.IsEmpty() || ClassPath.IsEmpty()) return nullptr;

		const FName Want(*NameStr);
		{
			TArray<UActorComponent*> Existing;
			A->GetComponents(Existing);
			for (UActorComponent* C : Existing)
				if (C && C->GetFName() == Want)
					return C;
		}

		UClass* Cls = StaticLoadClass(UActorComponent::StaticClass(), nullptr, *ClassPath);
		if (!Cls) return nullptr;

		UActorComponent* NewC = NewObject<UActorComponent>(A, Cls, Want);
		if (!NewC) return nullptr;

		if (USceneComponent* Scene = Cast<USceneComponent>(NewC))
		{
			if (USceneComponent* Root = A->GetRootComponent())
				Scene->SetupAttachment(Root);
			else
				A->SetRootComponent(Scene);
		}

		NewC->OnComponentCreated();
		A->AddInstanceComponent(NewC);
		NewC->RegisterComponent();

		return NewC;
	}

	void CapturePlayer(UWorld* World)
	{
		PlayerBlob.Reset();
		if (!World) return;

		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn) return;

		UOsirisSaveComponent* SC = Pawn->FindComponentByClass<UOsirisSaveComponent>();
		if (!SC) return;

		if (!SC->OsirisGuid.IsValid())
			SC->OsirisGuid = FGuid::NewGuid();

		SC->GetOsirisPreSaveHook().Broadcast();

		const FGuid Guid = SC->OsirisGuid;
		FString     ClassPath = Pawn->GetClass()->GetPathName();
		const FTransform Xf = Pawn->GetActorTransform();

		TArray<uint8> ActorBytes;
		{
			FMemoryWriter  AW(ActorBytes, true);
			FOsirisObjAr   AAr(AW);
			Pawn->Serialize(AAr);
		}

		TArray<UActorComponent*> All; Pawn->GetComponents(All);
		TArray<UActorComponent*> Savable;
		for (UActorComponent* C : All)
			if (ShouldSaveComponent(C))
				Savable.Add(C);

		Savable.Sort([](const UActorComponent& L, const UActorComponent& R)
			{
				return L.GetName() < R.GetName();
			});

		FMemoryWriter W(PlayerBlob, true);
		FArchive& Ar = W;

		{
			uint32 Magic = GPlayerMagic;
			int32  Ver = GPlayerVersion;
			Ar << Magic;
			Ar << Ver;
		}

		WriteGuid(Ar, Guid);
		Ar << ClassPath;

		FVector Loc = Xf.GetLocation();
		FQuat   Rot = Xf.GetRotation();
		FVector Scale = Xf.GetScale3D();
		Ar << Loc; Ar << Rot; Ar << Scale;

		WriteBytes(Ar, ActorBytes);

		int32 CCount = Savable.Num();
		Ar << CCount;

		for (UActorComponent* C : Savable)
		{
			FString NameStr = C->GetFName().ToString();
			FString CompClassPath = C->GetClass()->GetPathName();
			const bool bDyn = IsDynamicSavableComponent(C);

			TArray<uint8> B;
			{
				FMemoryWriter CW(B, true);
				FOsirisObjAr  CAr(CW);
				C->Serialize(CAr);
			}

			Ar << NameStr;
			Ar << CompClassPath;
			WriteBool(Ar, bDyn);
			WriteBytes(Ar, B);
		}
	}

	static bool ReadPlayerHeader(const TArray<uint8>& Blob, int32& OutStartOffset, int32& OutVer)
	{
		OutStartOffset = 0;
		OutVer = 0;
		if (Blob.Num() < 8) return false;

		FMemoryReader R(Blob, true);
		FArchive& Ar = R;

		uint32 Magic = 0;
		int32  Ver = 0;
		Ar << Magic;
		Ar << Ver;

		if (Ar.IsError())        return false;
		if (Magic != GPlayerMagic) return false;
		if (Ver != GPlayerVersion) return false;

		OutStartOffset = 8;
		OutVer = Ver;
		return true;
	}

	void ApplyPlayerEarlyTransform(UWorld* World)
	{
		if (!World || PlayerBlob.Num() == 0) return;

		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn) return;

		int32 StartOffset = 0, Ver = 0;
		if (!ReadPlayerHeader(PlayerBlob, StartOffset, Ver)) return;

		FMemoryReader R(PlayerBlob, true);
		R.Seek(StartOffset);
		FArchive& Ar = R;

		FGuid Guid; ReadGuid(Ar, Guid);

		FString ClassPath;
		Ar << ClassPath;

		FVector Loc; FQuat Rot; FVector Scale;
		Ar << Loc; Ar << Rot; Ar << Scale;

		Pawn->SetActorTransform(FTransform(Rot, Loc, Scale), false, nullptr, ETeleportType::TeleportPhysics);
	}

	void ApplyPlayerFull(UWorld* World)
	{
		if (!World || PlayerBlob.Num() == 0) return;

		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn) return;

		UOsirisSaveComponent* SC = Pawn->FindComponentByClass<UOsirisSaveComponent>();
		if (!SC) return;

		int32 StartOffset = 0, Ver = 0;
		if (!ReadPlayerHeader(PlayerBlob, StartOffset, Ver)) return;

		FMemoryReader R(PlayerBlob, true);
		R.Seek(StartOffset);
		FArchive& Ar = R;

		FGuid Guid; ReadGuid(Ar, Guid);

		FString ClassPath;
		Ar << ClassPath;

		FVector Loc; FQuat Rot; FVector Scale;
		Ar << Loc; Ar << Rot; Ar << Scale;

		TArray<uint8> ABytes;
		if (!ReadBytes(Ar, ABytes, GMaxBytes_PlayerBlob)) return;

		int32 CCount = 0;
		Ar << CCount;
		if (Ar.IsError()) return;
		if (CCount < 0 || CCount > GMaxCompPerActor) return;

		struct FTempComp
		{
			FString Name;
			FString ClassPath;
			bool    bDynamic = false;
			TArray<uint8> Bytes;
		};

		TArray<FTempComp> Temp;
		Temp.Reserve(CCount);

		for (int32 i = 0; i < CCount; ++i)
		{
			FTempComp T;
			Ar << T.Name;
			Ar << T.ClassPath;
			if (!ReadBool(Ar, T.bDynamic)) return;
			if (!ReadBytes(Ar, T.Bytes, GMaxBytes_Comp)) return;
			Temp.Add(MoveTemp(T));
		}

		for (const FTempComp& T : Temp)
			if (T.bDynamic)
				EnsureDynamicComponent(Pawn, T.Name, T.ClassPath);

		if (ABytes.Num() > 0)
		{
			FMemoryReader AR(ABytes, true);
			FOsirisObjAr  AAr(AR);
			Pawn->Serialize(AAr);
		}

		TArray<UActorComponent*> Comps;
		Pawn->GetComponents(Comps);

		for (const FTempComp& T : Temp)
		{
			if (T.Name.IsEmpty() || T.Bytes.Num() == 0) continue;

			const FName Want(*T.Name);
			for (UActorComponent* C : Comps)
			{
				if (C && C->GetFName() == Want)
				{
					FMemoryReader CR(T.Bytes, true);
					FOsirisObjAr  CAr(CR);
					C->Serialize(CAr);
					break;
				}
			}
		}

		Pawn->ReregisterAllComponents();
		Pawn->SetActorTransform(FTransform(Rot, Loc, Scale), false, nullptr, ETeleportType::TeleportPhysics);

		SC->GetOsirisPostLoadHook().Broadcast();
	}

	void RemoveRecordsForContainerAliases(const TArray<FName>& Aliases)
	{
		if (Aliases.Num() == 0) return;

		for (const FName& Cid : Aliases)
		{
			if (const TArray<FGuid>* Old = ContainerIndex.Find(Cid))
			{
				for (const FGuid& G : *Old)
				{
					const FActorRecord* R = ActorDB.Find(G);
					if (R && Aliases.Contains(R->ContainerId))
						ActorDB.Remove(G);
				}
			}
		}

		for (auto It = ActorDB.CreateIterator(); It; ++It)
			if (Aliases.Contains(It.Value().ContainerId))
				It.RemoveCurrent();

		for (const FName& Cid : Aliases)
			ContainerIndex.Remove(Cid);
	}

	void CaptureLevel(UWorld* World, ULevel* Level)
	{
		if (!World || !Level) return;

		TArray<FName> Aliases;
		GetContainerAliases(World, Level, Aliases);

		const FName Canon = GetContainerIdCanonical(World, Level);
		if (Canon.IsNone()) return;

		RemoveRecordsForContainerAliases(Aliases);

		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;

		TArray<FGuid>  NewGuids;
		TArray<AActor*> Candidates;
		GatherLevelActors(World, Level, Candidates);

		for (AActor* A : Candidates)
		{
			if (!A || !IsValid(A))             continue;
			if (A == Pawn)                      continue;
			if (A->HasAnyFlags(RF_Transient))   continue;

			UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>();
			if (!SC) continue;

			if (!SC->OsirisGuid.IsValid())
				SC->OsirisGuid = FGuid::NewGuid();

			SC->GetOsirisPreSaveHook().Broadcast();

			FActorRecord Rec;
			Rec.ContainerId = Canon;
			Rec.Guid = SC->OsirisGuid;
			Rec.ClassPath = A->GetClass()->GetPathName();
			Rec.Transform = A->GetActorTransform();

			{
				FMemoryWriter W(Rec.ActorBytes, true);
				FOsirisObjAr  ObjAr(W);
				A->Serialize(ObjAr);
			}

			TArray<UActorComponent*> All; A->GetComponents(All);
			TArray<UActorComponent*> Savable;
			for (UActorComponent* C : All)
				if (ShouldSaveComponent(C))
					Savable.Add(C);

			Savable.Sort([](const UActorComponent& L, const UActorComponent& R)
				{
					return L.GetName() < R.GetName();
				});

			Rec.Comps.Reset();
			Rec.Comps.Reserve(Savable.Num());

			for (UActorComponent* C : Savable)
			{
				FCompRecord CR;
				CR.Name = C->GetFName().ToString();
				CR.ClassPath = C->GetClass()->GetPathName();
				CR.bDynamic = IsDynamicSavableComponent(C);

				{
					FMemoryWriter CW(CR.Bytes, true);
					FOsirisObjAr  CAr(CW);
					C->Serialize(CAr);
				}

				Rec.Comps.Add(MoveTemp(CR));
			}

			ActorDB.Add(Rec.Guid, MoveTemp(Rec));
			NewGuids.Add(SC->OsirisGuid);
		}

		NewGuids.Sort([](const FGuid& A, const FGuid& B)
			{
				if (A.A != B.A) return A.A < B.A;
				if (A.B != B.B) return A.B < B.B;
				if (A.C != B.C) return A.C < B.C;
				return A.D < B.D;
			});

		if (NewGuids.Num() > 0)
			ContainerIndex.Add(Canon, MoveTemp(NewGuids));
	}

	void CaptureAllLoaded(UWorld* World)
	{
		if (!World) return;
		const TArray<ULevel*>& Levels = World->GetLevels();
		for (ULevel* L : Levels)
			if (L) CaptureLevel(World, L);
	}

	bool IsContainerApplied(UWorld* World, ULevel* Level) const
	{
		TArray<FName> Aliases;
		GetContainerAliases(World, Level, Aliases);
		for (const FName& Id : Aliases)
			if (AppliedContainersDuringLoad.Contains(Id))
				return true;
		return false;
	}

	void MarkContainerApplied(UWorld* World, ULevel* Level)
	{
		TArray<FName> Aliases;
		GetContainerAliases(World, Level, Aliases);
		for (const FName& Id : Aliases)
			AppliedContainersDuringLoad.Add(Id);
	}

	const TArray<FGuid>* FindGuidsByContainerAliases(UWorld* World, ULevel* Level, FName& OutUsedKey) const
	{
		TArray<FName> Aliases;
		GetContainerAliases(World, Level, Aliases);

		for (const FName& Cid : Aliases)
		{
			if (const TArray<FGuid>* Found = ContainerIndex.Find(Cid))
			{
				OutUsedKey = Cid;
				return Found;
			}
		}

		OutUsedKey = NAME_None;
		return nullptr;
	}

	static UOsirisSaveComponent* FindOrCreateSaveComponent(AActor* A)
	{
		if (!A) return nullptr;
		if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
			return SC;

		UOsirisSaveComponent* SC = NewObject<UOsirisSaveComponent>(
			A, UOsirisSaveComponent::StaticClass(), TEXT("OsirisSaveComponent"));
		if (!SC) return nullptr;

		SC->OnComponentCreated();
		A->AddInstanceComponent(SC);
		SC->RegisterComponent();
		return SC;
	}

	void EnsureDynamicComponentsForRecord(AActor* A, const FActorRecord& Rec)
	{
		if (!A) return;
		for (const FCompRecord& CR : Rec.Comps)
			if (CR.bDynamic)
				EnsureDynamicComponent(A, CR.Name, CR.ClassPath);
	}

	void ApplyActorAndComponents(AActor* A, const FActorRecord& Rec)
	{
		if (!A || !IsValid(A)) return;

		EnsureDynamicComponentsForRecord(A, Rec);

		if (Rec.ActorBytes.Num() > 0)
		{
			FMemoryReader RR(Rec.ActorBytes, true);
			FOsirisObjAr  ObjAr(RR);
			A->Serialize(ObjAr);
		}

		TArray<UActorComponent*> Comps;
		A->GetComponents(Comps);

		for (const FCompRecord& CR : Rec.Comps)
		{
			if (CR.Name.IsEmpty() || CR.Bytes.Num() == 0) continue;

			const FName Want(*CR.Name);
			for (UActorComponent* C : Comps)
			{
				if (C && C->GetFName() == Want)
				{
					FMemoryReader R(CR.Bytes, true);
					FOsirisObjAr  CAr(R);
					C->Serialize(CAr);
					break;
				}
			}
		}

		A->ReregisterAllComponents();
		A->SetActorTransform(Rec.Transform, false, nullptr, ETeleportType::TeleportPhysics);

		if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
			SC->GetOsirisPostLoadHook().Broadcast();
	}

	void ApplyLevel(UWorld* World, ULevel* Level)
	{
		if (!World || !Level) return;

		TArray<TWeakObjectPtr<AActor>> PreExistingSavables;
		if (bPendingLoad)
		{
			TArray<AActor*> ExistingActors;
			GatherLevelActors(World, Level, ExistingActors);

			for (AActor* A : ExistingActors)
			{
				if (!A || !IsValid(A)) continue;
				if (APawn* P = Cast<APawn>(A); P && P->IsPlayerControlled()) continue;

				UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>();
				if (!SC || !SC->OsirisGuid.IsValid()) continue;

				PreExistingSavables.Add(A);
			}
		}

		FName UsedKey = NAME_None;
		const TArray<FGuid>* GuidsPtr = FindGuidsByContainerAliases(World, Level, UsedKey);
		if (!GuidsPtr) return;

		const TArray<FGuid>& Guids = *GuidsPtr;

		TMap<FGuid, AActor*> Existing;
		Existing.Reserve(256);

		{
			TArray<AActor*> ExistingActors;
			GatherLevelActors(World, Level, ExistingActors);

			for (AActor* A : ExistingActors)
			{
				if (!A || !IsValid(A)) continue;
				if (APawn* P = Cast<APawn>(A); P && P->IsPlayerControlled()) continue;

				if (UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>())
					if (SC->OsirisGuid.IsValid())
						Existing.Add(SC->OsirisGuid, A);
			}
		}

		for (const FGuid& G : Guids)
		{
			const FActorRecord* Rec = ActorDB.Find(G);
			if (!Rec) continue;
			if (Existing.FindRef(G)) continue;

			UClass* Cls = StaticLoadClass(AActor::StaticClass(), nullptr, *Rec->ClassPath);
			if (!Cls) continue;

			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			P.OverrideLevel = Level;
			P.bDeferConstruction = true;

			AActor* A = World->SpawnActor<AActor>(Cls, Rec->Transform, P);
			if (!A) continue;

			A->FinishSpawning(Rec->Transform);

			UOsirisSaveComponent* SC = FindOrCreateSaveComponent(A);
			if (SC) SC->SetOsirisGuid(Rec->Guid);

			Existing.Add(G, A);
		}

		for (const FGuid& G : Guids)
		{
			const FActorRecord* Rec = ActorDB.Find(G);
			if (!Rec) continue;

			AActor* A = Existing.FindRef(G);
			if (!A)  continue;

			ApplyActorAndComponents(A, *Rec);
		}

		{
			TSet<FGuid> Keep;
			Keep.Reserve(Guids.Num());
			for (const FGuid& K : Guids) Keep.Add(K);

			TArray<AActor*> ToDestroy;

			if (bPendingLoad)
			{
				for (const TWeakObjectPtr<AActor>& WeakA : PreExistingSavables)
				{
					AActor* A = WeakA.Get();
					if (!A || !IsValid(A))         continue;
					if (ShouldProtectFromDestroy(A)) continue;

					UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>();
					if (!SC || !SC->OsirisGuid.IsValid()) continue;

					if (!Keep.Contains(SC->OsirisGuid))
						ToDestroy.Add(A);
				}
			}
			else
			{
				TArray<AActor*> ExistingActors;
				GatherLevelActors(World, Level, ExistingActors);

				for (AActor* A : ExistingActors)
				{
					if (!A || !IsValid(A))         continue;
					if (APawn* P = Cast<APawn>(A); P && P->IsPlayerControlled()) continue;
					if (ShouldProtectFromDestroy(A)) continue;

					UOsirisSaveComponent* SC = A->FindComponentByClass<UOsirisSaveComponent>();
					if (!SC || !SC->OsirisGuid.IsValid()) continue;

					if (!Keep.Contains(SC->OsirisGuid))
						ToDestroy.Add(A);
				}
			}

			for (AActor* A : ToDestroy)
				if (IsValid(A)) A->Destroy();
		}
	}

	static void WriteActorDB(const TMap<FGuid, FActorRecord>& InDB, TArray<uint8>& Out)
	{
		Out.Reset();
		FMemoryWriter W(Out, true);
		FArchive& Ar = W;

		int32 Version = GOsiris_DB_Version;
		int32 Count = InDB.Num();

		Ar << Version;
		Ar << Count;

		TArray<FGuid> Keys;
		Keys.Reserve(Count);
		for (const auto& Kvp : InDB) Keys.Add(Kvp.Key);

		Keys.Sort([](const FGuid& A, const FGuid& B)
			{
				if (A.A != B.A) return A.A < B.A;
				if (A.B != B.B) return A.B < B.B;
				if (A.C != B.C) return A.C < B.C;
				return A.D < B.D;
			});

		for (const FGuid& K : Keys)
		{
			const FActorRecord& R = InDB.FindChecked(K);

			WriteNameStr(Ar, R.ContainerId);
			WriteGuid(Ar, R.Guid);

			FString ClassPath = R.ClassPath;
			Ar << ClassPath;

			const FTransform& Xf = R.Transform;
			FVector           Loc = Xf.GetLocation();
			FQuat             Rot = Xf.GetRotation();
			FVector           Scale = Xf.GetScale3D();
			Ar << Loc; Ar << Rot; Ar << Scale;

			WriteBytes(Ar, R.ActorBytes);

			int32 CCount = R.Comps.Num();
			Ar << CCount;

			for (const FCompRecord& CR : R.Comps)
			{
				FString NameStr = CR.Name;
				FString CompClassPath = CR.ClassPath;
				Ar << NameStr;
				Ar << CompClassPath;
				WriteBool(Ar, CR.bDynamic);
				WriteBytes(Ar, CR.Bytes);
			}
		}
	}

	static bool ReadActorDB(const TArray<uint8>& In, TMap<FGuid, FActorRecord>& OutDB)
	{
		OutDB.Reset();
		if (In.Num() == 0) return true;

		FMemoryReader R(In, true);
		FArchive& Ar = R;

		int32 Version = 0, Count = 0;
		Ar << Version;
		Ar << Count;

		if (Ar.IsError())                  return false;
		if (Version != GOsiris_DB_Version) return false;
		if (Count < 0 || Count > GMaxActorRecords) return false;

		for (int32 i = 0; i < Count; ++i)
		{
			FActorRecord Rec;

			ReadNameStr(Ar, Rec.ContainerId);
			ReadGuid(Ar, Rec.Guid);

			Ar << Rec.ClassPath;

			FVector Loc; FQuat Rot; FVector Scale;
			Ar << Loc; Ar << Rot; Ar << Scale;
			Rec.Transform = FTransform(Rot, Loc, Scale);

			if (!ReadBytes(Ar, Rec.ActorBytes, GMaxBytes_Actor)) return false;

			int32 CCount = 0;
			Ar << CCount;
			if (Ar.IsError()) return false;
			if (CCount < 0 || CCount > GMaxCompPerActor) return false;

			Rec.Comps.Reset();
			Rec.Comps.Reserve(CCount);

			for (int32 c = 0; c < CCount; ++c)
			{
				FCompRecord CR;
				Ar << CR.Name;
				Ar << CR.ClassPath;
				if (!ReadBool(Ar, CR.bDynamic))              return false;
				if (!ReadBytes(Ar, CR.Bytes, GMaxBytes_Comp)) return false;
				Rec.Comps.Add(MoveTemp(CR));
			}

			if (Ar.IsError()) return false;

			if (Rec.Guid.IsValid())
				OutDB.Add(Rec.Guid, MoveTemp(Rec));
		}

		return !Ar.IsError();
	}

	void OnPostLoadMap(UWorld* World)
	{
		if (!World || !World->IsGameWorld()) return;

		if (!SessionWorld.IsValid() || SessionWorld.Get() != World)
		{
			if (!bPendingLoad) ResetSession(World);
			else               SessionWorld = World;
		}

		RootMapId = GetRootMapIdCanonical(World);
	}

	void OnWorldTickStart(UWorld* World, ELevelTick, float)
	{
		if (!bPendingLoad) return;
		if (!World || !World->IsGameWorld()) return;
		if (!DoesWorldMatchPendingRoot(World, PendingRootMap)) return;
		if (!IsWorldReady(World)) return;

		const double Now = FPlatformTime::Seconds();

		if (Now - LoadStartTime > GLoadDeadlineSeconds)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("OSIRIS: Pending load exceeded deadline (%.1f s). Finishing now."),
				GLoadDeadlineSeconds);
			FinishPendingLoad();
			return;
		}

		bool bAppliedAnyLevelThisTick = false;

		if (!bPlayerEarlyApplied)
		{
			ApplyPlayerEarlyTransform(World);
			bPlayerEarlyApplied = true;
		}

		const TArray<ULevel*>& Levels = World->GetLevels();
		for (ULevel* L : Levels)
		{
			if (!L) continue;

			if (!IsContainerApplied(World, L))
			{
				FName UsedKey = NAME_None;
				const TArray<FGuid>* GuidsPtr = FindGuidsByContainerAliases(World, L, UsedKey);
				if (GuidsPtr) ApplyLevel(World, L);

				MarkContainerApplied(World, L);
				bAppliedAnyLevelThisTick = true;
			}
		}

		if (!bPlayerFullApplied)
		{
			ApplyPlayerFull(World);
			bPlayerFullApplied = true;
		}

		if (bAppliedAnyLevelThisTick)
		{
			QuietSince = -1.0;
		}
		else
		{
			if (QuietSince < 0.0)
				QuietSince = Now;

			if (Now - QuietSince >= GQuietSeconds)
				FinishPendingLoad();
		}
	}

	void OnLevelAdded(ULevel* Level, UWorld* World)
	{
		if (!World || !World->IsGameWorld() || !World->HasBegunPlay()) return;
		if (!Level) return;

		if (bPendingLoad)
		{
			if (!DoesWorldMatchPendingRoot(World, PendingRootMap)) return;
			if (!IsWorldReady(World)) return;

			if (!IsContainerApplied(World, Level))
			{
				FName UsedKey = NAME_None;
				const TArray<FGuid>* GuidsPtr = FindGuidsByContainerAliases(World, Level, UsedKey);
				if (GuidsPtr) ApplyLevel(World, Level);

				MarkContainerApplied(World, Level);
			}

			QuietSince = -1.0;
			return;
		}

		ApplyLevel(World, Level);
	}

	void OnLevelRemoved(ULevel* Level, UWorld* World)
	{
		if (!World || !World->IsGameWorld() || !World->HasBegunPlay()) return;
		if (!Level) return;
		if (bIgnoreCapture) return;

		CaptureLevel(World, Level);
	}

	bool SaveGame(UOsirisSubsystem* Owner, const FString& ProfileName, const FString& SlotName)
	{
		if (!Owner) return false;

		if (bPendingLoad)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("OSIRIS SaveGame: Rejected — a load is still in progress. "
					"Wait for the load to complete before saving."));
			return false;
		}

		const FString PName = NormalizeOrDefault(ProfileName, GOsirisDefaultProfile);
		const FString SName = NormalizeOrDefault(SlotName, GOsirisDefaultSlot);

		UWorld* World = Owner->GetWorld();
		if (!World || !World->IsGameWorld()) return false;

		RootMapId = GetRootMapIdCanonical(World);

		CaptureAllLoaded(World);
		CapturePlayer(World);

		UOsirisSaveGame* SG = Cast<UOsirisSaveGame>(
			UGameplayStatics::CreateSaveGameObject(UOsirisSaveGame::StaticClass()));
		if (!SG) return false;

		SG->ProfileName = PName;
		SG->SlotName = SName;
		SG->SavedAtUtc = FDateTime::UtcNow();
		SG->RootMapId = RootMapId;
		SG->DataContainers.Reset();

		{
			FOsirisDataContainer C;
			C.LevelId = GId_ActorDB;
			WriteActorDB(ActorDB, C.Data);
			SG->DataContainers.Add(MoveTemp(C));
		}

		{
			FOsirisDataContainer C;
			C.LevelId = GId_Player;
			C.Data = PlayerBlob;
			SG->DataContainers.Add(MoveTemp(C));
		}

		const FString FinalSlot = MakeOsirisSlotName_Internal(PName, SName);
		return UGameplayStatics::SaveGameToSlot(SG, FinalSlot, 0);
	}

	bool LoadGame(UOsirisSubsystem* Owner, const FString& ProfileName, const FString& SlotName)
	{
		if (!Owner) return false;

		const FString PName = NormalizeOrDefault(ProfileName, GOsirisDefaultProfile);
		const FString SName = NormalizeOrDefault(SlotName, GOsirisDefaultSlot);

		UWorld* World = Owner->GetWorld();
		if (!World || !World->IsGameWorld()) return false;

		const FString FinalSlot = MakeOsirisSlotName_Internal(PName, SName);

		UOsirisSaveGame* SG = Cast<UOsirisSaveGame>(
			UGameplayStatics::LoadGameFromSlot(FinalSlot, 0));

		if (!SG || SG->RootMapId.IsNone())       return false;
		if (!SG->ProfileName.IsEmpty() && SG->ProfileName != PName) return false;
		if (!SG->SlotName.IsEmpty() && SG->SlotName != SName) return false;

		ActorDB.Reset();
		ContainerIndex.Reset();
		PlayerBlob.Reset();

		TArray<uint8> ActorDBBlob;
		for (const FOsirisDataContainer& C : SG->DataContainers)
		{
			if (C.LevelId == GId_ActorDB) ActorDBBlob = C.Data;
			else if (C.LevelId == GId_Player)  PlayerBlob = C.Data;
		}

		if (!ReadActorDB(ActorDBBlob, ActorDB)) return false;

		RebuildIndexAll();
		PreloadAllClasses();

		bPendingLoad = true;
		bIgnoreCapture = true;

		PendingRootMap = MakeNameFromStringNormalized(SG->RootMapId.ToString());
		QuietSince = -1.0;
		LoadStartTime = FPlatformTime::Seconds();
		bPlayerEarlyApplied = false;
		bPlayerFullApplied = false;
		AppliedContainersDuringLoad.Reset();

		if (!DoesWorldMatchPendingRoot(World, PendingRootMap))
		{
			const FName LevelName = MakeLevelNameForOpenLevel(PendingRootMap);

			if (!LevelName.IsNone())
			{
				UGameplayStatics::OpenLevel(Owner, LevelName);
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("OSIRIS LoadGame: Could not derive a valid level name from '%s'. Load aborted."),
					*PendingRootMap.ToString());

				FinishPendingLoad();
				return false;
			}
		}

		return true;
	}
};

void UOsirisSubsystem::FImplDeleter::operator()(FImpl* Ptr) const
{
	delete Ptr;
}

void UOsirisSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Impl.Reset(new FImpl());
	Impl->Bind();
}

void UOsirisSubsystem::Deinitialize()
{
	if (Impl)
	{
		Impl->Unbind();
		Impl.Reset();
	}
	Super::Deinitialize();
}

bool UOsirisSubsystem::SaveGame(const FString& ProfileName, const FString& SlotName)
{
	return Impl ? Impl->SaveGame(this, ProfileName, SlotName) : false;
}

bool UOsirisSubsystem::LoadGame(const FString& ProfileName, const FString& SlotName)
{
	return Impl ? Impl->LoadGame(this, ProfileName, SlotName) : false;
}