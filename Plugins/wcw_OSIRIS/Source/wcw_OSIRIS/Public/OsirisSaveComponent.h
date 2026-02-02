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

	//** Returns GUID as string for easy Blueprint PrintString. *
	//** Blueprint の PrintString 用に GUID を文字列で返します。*
	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	FString GetOsirisGuidString() const;

	void SetOsirisGuid(const FGuid& InGuid) { OsirisGuid = InGuid; }

protected:

	//** Creates a unique GUID on registration if missing. *
	//** 登録時に GUID が未設定なら新規に生成します。*
	virtual void OnRegister() override;

		
};
