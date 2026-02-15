// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "OsirisSaveGame.generated.h"

USTRUCT()
struct FOsirisDataContainer
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FName LevelId = NAME_None;
	UPROPERTY(SaveGame) TArray<uint8> Data;
};

UCLASS()
class WCW_OSIRIS_API UOsirisSaveGame : public USaveGame
{
	GENERATED_BODY()

public:

	UPROPERTY(SaveGame) FName RootMapId = NAME_None;
	UPROPERTY(SaveGame) TArray<FOsirisDataContainer> DataContainers;
	
};
