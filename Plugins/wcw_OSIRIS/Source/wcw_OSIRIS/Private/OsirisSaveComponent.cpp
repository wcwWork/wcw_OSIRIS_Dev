//© Developer Nikita Petrachkov in collaboration with WINTER CROWN WORKS, all rights reserved ©

#include "OsirisSaveComponent.h"
#include "GameFramework/Actor.h"

void UOsirisSaveComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITOR
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		if (!OsirisGuid.IsValid())
		{
			OsirisGuid = FGuid::NewGuid();
			Modify();
			if (AActor* A = GetOwner())
			{
				A->Modify();
				A->MarkPackageDirty();
			}
			MarkPackageDirty();
		}
		return;
	}
#endif

	if (!OsirisGuid.IsValid())
		OsirisGuid = FGuid::NewGuid();
}

FString UOsirisSaveComponent::GetOsirisGuidString() const
{
	return OsirisGuid.IsValid() ? OsirisGuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("INVALID_GUID");
}

void UOsirisSaveComponent::Osiris_BroadcastPreSave()
{
	OnOsirisPreSave.Broadcast();
}

void UOsirisSaveComponent::Osiris_BroadcastPostLoad()
{
	OnOsirisPostLoad.Broadcast();
}