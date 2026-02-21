//© Developer Nikita Petrachkov in collaboration with WINTER CROWN WORKS, all rights reserved ©

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OsirisSubsystem.generated.h"

UCLASS()
class WCW_OSIRIS_API UOsirisSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "OSIRIS") bool SaveGame();
	UFUNCTION(BlueprintCallable, Category = "OSIRIS") bool LoadGame();

private:
	struct FImpl;
	struct FImplDeleter { void operator()(FImpl* Ptr) const; };
	TUniquePtr<FImpl, FImplDeleter> Impl;
};
