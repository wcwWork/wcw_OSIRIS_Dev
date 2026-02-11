// Fill out your copyright notice in the Description page of Project Settings.


#include "OsirisSaveComponent.h"
#include "GameFramework/Actor.h"

void UOsirisSaveComponent::OnRegister()
{
	Super::OnRegister();
	if (OsirisGuid.IsValid()) return;

	const AActor* Owner = GetOwner();
	const FString Lvl = (Owner && Owner->GetLevel() && Owner->GetLevel()->GetOuter()) ? Owner->GetLevel()->GetOuter()->GetName() : TEXT("");
	const FString Key = (Owner ? Owner->GetFName().ToString() : TEXT("NO_OWNER")) + TEXT("|") + Lvl + TEXT("|") + GetFName().ToString();

	OsirisGuid = FGuid(GetTypeHash(Key), GetTypeHash(Key + TEXT("1")), GetTypeHash(Key + TEXT("2")), GetTypeHash(Key + TEXT("3")));
}



FString UOsirisSaveComponent::GetOsirisGuidString() const
{
	return OsirisGuid.IsValid()
		? OsirisGuid.ToString(EGuidFormats::DigitsWithHyphens)
		: TEXT("INVALID_GUID");
}


void UOsirisSaveComponent::Osiris_BroadcastPreSave() { OnOsirisPreSave.Broadcast(); }
void UOsirisSaveComponent::Osiris_BroadcastPostLoad() { OnOsirisPostLoad.Broadcast(); }