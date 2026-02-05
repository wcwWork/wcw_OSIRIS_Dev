// Fill out your copyright notice in the Description page of Project Settings.


#include "OsirisSaveComponent.h"


void UOsirisSaveComponent::OnRegister()
{
	Super::OnRegister();
	if (OsirisGuid.IsValid()) return;

#if WITH_EDITOR
	if (GIsEditor)
		if (UWorld* W = GetWorld())
			if (!W->IsGameWorld())
			{
				if (AActor* Owner = GetOwner()) Owner->Modify();
				OsirisGuid = FGuid::NewGuid();
				return;
			}
#endif

	OsirisGuid = FGuid::NewGuid();
}


FString UOsirisSaveComponent::GetOsirisGuidString() const
{
	return OsirisGuid.IsValid()
		? OsirisGuid.ToString(EGuidFormats::DigitsWithHyphens)
		: TEXT("INVALID_GUID");
}
