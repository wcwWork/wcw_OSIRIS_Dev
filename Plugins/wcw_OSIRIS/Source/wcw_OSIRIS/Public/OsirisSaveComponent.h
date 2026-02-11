// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OsirisSaveComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOsirisHook);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class WCW_OSIRIS_API UOsirisSaveComponent : public UActorComponent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintAssignable, Category = "OSIRIS")
	FOsirisHook OnOsirisPreSave;

	UPROPERTY(BlueprintAssignable, Category = "OSIRIS")
	FOsirisHook OnOsirisPostLoad;

	UFUNCTION(BlueprintCallable, meta = (AllowPrivateAccess = "true"), Category = "OSIRIS")
	void Osiris_BroadcastPreSave();

	UFUNCTION(BlueprintCallable, meta = (AllowPrivateAccess = "true"), Category = "OSIRIS")
	void Osiris_BroadcastPostLoad();

public:

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "OSIRIS")
	FGuid OsirisGuid;

	//** Returns GUID as string for easy Blueprint PrintString. *
	//** Blueprint の PrintString 用に GUID を文字列で返します。*
	UFUNCTION(BlueprintCallable, Category = "OSIRIS")
	FString GetOsirisGuidString() const;

	FORCEINLINE const FOsirisHook& GetOsirisPreSaveHook() const { return OnOsirisPreSave; }
	FORCEINLINE const FOsirisHook& GetOsirisPostLoadHook() const { return OnOsirisPostLoad; }

	void SetOsirisGuid(const FGuid& InGuid) { OsirisGuid = InGuid; }

protected:

	//** Creates a unique GUID on registration if missing. *
	//** 登録時に GUID が未設定なら新規に生成します。*
	virtual void OnRegister() override;	
};
