#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OsirisSubsystem.generated.h"

UCLASS()
class WCW_OSIRIS_API UOsirisSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	int32 GetMarkedActorCount() const;
};
