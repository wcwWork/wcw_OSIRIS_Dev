#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OsirisSubsystem.generated.h"

class AActor;

UCLASS()
class WCW_OSIRIS_API UOsirisSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	TArray<uint8> LastActorBytes;
	TArray<FName> LastComponentNames;
	TArray<TArray<uint8>> LastComponentBytes;

	bool bHasLastSnapshot = false;

public:

	//** Counts all actors marked with OsirisSaveComponent. *
	//** OsirisSaveComponent でマークされたアクター数を数えます。*
	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	int32 GetMarkedActorCount() const;

	//** Splits marked actors into placed (loaded) and spawned (runtime-created). *
	//** マークされたアクターを配置済み(ロード)とスポーン(実行時生成)に分けます。*
	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	void GetMarkedPlacedSpawnedCount(int32& OutPlaced, int32& OutSpawned) const;

	//** Captures SaveGame-only state of the target actor and its components into subsystem memory. *
	//** ターゲット(アクター＋コンポーネント)の SaveGame のみの状態をサブシステム内メモリに保存します。*
	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	bool SaveGameSnapshot(AActor* Target, bool bIncludeComponents = true);

	//** Applies the last captured snapshot from subsystem memory back onto the target. *
	//** サブシステム内メモリの最新スナップショットをターゲットへ復元します。*
	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	bool LoadGameSnapshot(AActor* Target, bool bIncludeComponents = true) const;

	//** Returns a short debug summary of the last captured snapshot (bytes/components). *
	//** 最新スナップショットの簡易情報(バイト数/コンポ数)を返します。*
	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	FString GetLastSnapshotDebugInfo() const;

};
