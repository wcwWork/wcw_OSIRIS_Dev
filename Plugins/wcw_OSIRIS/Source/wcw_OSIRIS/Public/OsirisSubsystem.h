#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OsirisSubsystem.generated.h"

UCLASS()
class WCW_OSIRIS_API UOsirisSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	//**Method for checking how many objects in the game world the save component has.*
	//**保存コンポーネントがゲーム ワールド内にいくつのオブジェクトを持っているかを確認するメソッド.*
	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	int32 GetMarkedActorCount() const;

	//**A method for checking whether an object was initially present on the level or was added later at runtime.*
	//**オブジェクトがレベル上に最初から存在していたか、実行時に後で追加されたかを確認するメソッド。*
	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	void GetMarkedPlacedSpawnedCount(int32& OutPlaced, int32& OutSpawned) const;
};
