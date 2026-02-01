#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OsirisSubsystem.generated.h"

UCLASS()
class WCW_OSIRIS_API UOsirisSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	
	//**A method that displays complete information about the object to be saved, such as(GUID, whether it was spawned, class, and world position). *
	//**保存するオブジェクトに関する完全な情報 (GUID、生成されたかどうか、クラス、ワールド位置など) を表示するメソッド。*
	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	FString BuildDebugManifestString(int32 MaxLines = 50) const;

};
