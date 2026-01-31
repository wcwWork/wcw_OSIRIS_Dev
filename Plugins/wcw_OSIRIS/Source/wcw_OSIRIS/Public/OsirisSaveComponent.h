// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OsirisSaveComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class WCW_OSIRIS_API UOsirisSaveComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "OSIRIS")
	FGuid OsirisGuid;

	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	FString GetOsirisGuidString() const;

	void SetOsirisGuid(const FGuid& InGuid) { OsirisGuid = InGuid; }

protected:

	//**After registration, a unique Guid is immediately created.*
	//**登録後、一意の Guid がすぐに作成されます。*
	virtual void OnRegister() override;

		
};
