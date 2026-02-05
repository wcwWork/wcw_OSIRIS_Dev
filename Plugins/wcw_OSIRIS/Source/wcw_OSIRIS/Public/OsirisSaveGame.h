// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "OsirisSaveGame.generated.h"


UCLASS()
class WCW_OSIRIS_API UOsirisSaveGame : public USaveGame
{
	GENERATED_BODY()

public:

	UPROPERTY() TArray<uint8> Data;
	
};
