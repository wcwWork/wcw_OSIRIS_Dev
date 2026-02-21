//© Developer Nikita Petrachkov in collaboration with WINTER CROWN WORKS, all rights reserved ©

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OsirisSaveComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOsirisHook);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WCW_OSIRIS_API UOsirisSaveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "OSIRIS")
		FOsirisHook OnOsirisPreSave;

	UPROPERTY(BlueprintAssignable, Category = "OSIRIS")
		FOsirisHook OnOsirisPostLoad;

	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
		void Osiris_BroadcastPreSave();

	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
		void Osiris_BroadcastPostLoad();

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "OSIRIS")
		FGuid OsirisGuid;

	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
		FString GetOsirisGuidString() const;

	FORCEINLINE const FOsirisHook& GetOsirisPreSaveHook() const { return OnOsirisPreSave; }
	FORCEINLINE const FOsirisHook& GetOsirisPostLoadHook() const { return OnOsirisPostLoad; }

	void SetOsirisGuid(const FGuid& InGuid) { OsirisGuid = InGuid; }

protected:
	virtual void OnRegister() override;
};