


#include "VfxBeamCoordinatorComponent.h"
#include "VfxUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VfxBeamCoordinatorComponent)

int32 UVfxBeamCoordinatorComponent::sIDGenerator = 0;

// Sets default values for this component's properties
UVfxBeamCoordinatorComponent::UVfxBeamCoordinatorComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	PrimaryComponentTick.bCanEverTick = false;

	// ...
}

void UVfxBeamCoordinatorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearTimerCallback();

	Super::EndPlay(EndPlayReason);
}

void UVfxBeamCoordinatorComponent::OnUnregister()
{
	ClearTimerCallback();

	Super::OnUnregister();
}

bool UVfxBeamCoordinatorComponent::HasBeamTarget(int32 InID)
{
	FReadLookScoped ReadLock = FReadLookScoped(MapLock);
	if (FVfxBeamTargetObject* Elem = Targets.Find(InID))
	{
		return Elem->Object.Get() != nullptr;
	}

	return QueryHistory.Contains(InID);
}

float UVfxBeamCoordinatorComponent::GetProgress(int32 InID)
{
	FReadLookScoped ReadLock = FReadLookScoped(MapLock);
	if (FVfxBeamTargetObject* Elem = Targets.Find(InID))
	{
		if (UObject* Obj = Elem->Object.Get())
		{
			return IVfxBeamTargetInterface::Execute_GetWorkingProgress(Obj);
		}
	}
	else if (FLastQuery* LastQuery = QueryHistory.Find(InID))
	{
		return LastQuery->Progress;
	}

	UE_LOG(LogVfxGeneral, Warning, TEXT("UVfxBeamCoordinatorComponent::GetProgress: Failed for ID %d"), InID);
	return 0.0f;
}

float UVfxBeamCoordinatorComponent::GetElapsedSecs(int32 InID)
{
	FReadLookScoped ReadLock = FReadLookScoped(MapLock);
	if (FVfxBeamTargetObject* Elem = Targets.Find(InID))
	{
		if (UObject* Obj = Elem->Object.Get())
		{
			return FMath::Max(0.f, Elem->Duration - Elem->RemainDuration);
		}
	}
	else if (FLastQuery* LastQuery = QueryHistory.Find(InID))
	{
		return LastQuery->ElapsedSecs;
	}

	UE_LOG(LogVfxGeneral, Warning, TEXT("UVfxBeamCoordinatorComponent::GetElapsedSecs: Failed for ID %d"), InID);
	return 0.0f;
}

float UVfxBeamCoordinatorComponent::GetDuration(int32 InID)
{
	FReadLookScoped ReadLock = FReadLookScoped(MapLock);
	if (FVfxBeamTargetObject* Elem = Targets.Find(InID))
	{
		if (UObject* Obj = Elem->Object.Get())
		{
			return Elem->Duration;
		}
	}
	else if (FLastQuery* LastQuery = QueryHistory.Find(InID))
	{
		return LastQuery->Duration;
	}

	UE_LOG(LogVfxGeneral, Warning, TEXT("UVfxBeamCoordinatorComponent::GetDuration: Failed for ID %d"), InID);
	return 0.0f;
}

FVector UVfxBeamCoordinatorComponent::GetTargetLocation(int32 InID, EAxis::Type InAxis)
{
	FReadLookScoped ReadLock = FReadLookScoped(MapLock);
	if (FVfxBeamTargetObject* Elem = Targets.Find(InID))
	{
		if (UObject* Obj = Elem->Object.Get())
		{
			return IVfxBeamTargetInterface::Execute_GetWorkingWorldPosition(Obj, InAxis);
		}
	}
	else if (FLastQuery* LastQuery = QueryHistory.Find(InID))
	{
		return LastQuery->TargetLocation[InAxis];
	}

	UE_LOG(LogVfxGeneral, Warning, TEXT("UVfxBeamCoordinatorComponent::GetTargetLocation: Failed for ID %d"), InID);
	return FVector(200, 0, 0);
}

FVector UVfxBeamCoordinatorComponent::GetTargetMoveDirection(int32 InID, EAxis::Type InAxis)
{
	FReadLookScoped ReadLock = FReadLookScoped(MapLock);
	if (FVfxBeamTargetObject* Elem = Targets.Find(InID))
	{
		if (UObject* Obj = Elem->Object.Get())
		{
			return IVfxBeamTargetInterface::Execute_GetWorkingWorldDirection(Obj, InAxis);
		}
	}
	else if (FLastQuery* LastQuery = QueryHistory.Find(InID))
	{
		return LastQuery->TargetMoveDirection[InAxis];
	}

	UE_LOG(LogVfxGeneral, Warning, TEXT("UVfxBeamCoordinatorComponent::GetTargetMoveDirection: Failed for ID %d"), InID);
	return FVector::ForwardVector;
}

float UVfxBeamCoordinatorComponent::GetTargetWidth(int32 InID, EAxis::Type InAxis)
{
	FReadLookScoped ReadLock = FReadLookScoped(MapLock);
	if (FVfxBeamTargetObject* Elem = Targets.Find(InID))
	{
		if (UObject* Obj = Elem->Object.Get())
		{
			return IVfxBeamTargetInterface::Execute_GetWorkingWidth(Obj, InAxis);
		}
	}
	else if (FLastQuery* LastQuery = QueryHistory.Find(InID))
	{
		return LastQuery->TargetWidth[InAxis];
	}

	UE_LOG(LogVfxGeneral, Warning, TEXT("UVfxBeamCoordinatorComponent::GetTargetWidth: Failed for ID %d"), InID);
	return 100;
}

int32 UVfxBeamCoordinatorComponent::StartInteracting(UObject* InObj, EVfxBeamTargetInteractionType InType, float InDelay, float InDuration)
{
	IVfxBeamTargetInterface* Interface = Cast<IVfxBeamTargetInterface>(InObj);
	if (Interface == nullptr)
	{
		return INDEX_NONE;
	}

	MapLock.WriteLock();
	const int32 NewID = ++sIDGenerator;
	FVfxBeamTargetObject& NewElem = Targets.Add(NewID);
	MapLock.WriteUnlock();

	NewElem.Object = InObj;
	NewElem.Type = InType;
	NewElem.RemainDelay = 0.f;
	NewElem.bStarted = false;
	NewElem.Duration = FMath::Max(0.1f, InDuration);
	NewElem.RemainDuration = NewElem.Duration;
	if (InDelay > FLT_EPSILON)
	{
		NewElem.RemainDelay = InDelay;
	}
	else
	{
		IVfxBeamTargetInterface::Execute_OnBeamTargetInteractionStart(InObj, this, NewID, NewElem.Type);
		OnInteractionStart.ExecuteIfBound(InObj, NewElem.Type, NewID, NewElem.Duration);
		NewElem.bStarted = true;
	}


	EnsureTimerCallback();

	return true;
}

void UVfxBeamCoordinatorComponent::ClearTimerCallback()
{
	if (CallbackHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(CallbackHandle);
		}
		else
		{
			CallbackHandle.Invalidate();
		}
	}
}

void UVfxBeamCoordinatorComponent::EnsureTimerCallback()
{
	if (CallbackHandle.IsValid())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		FTimerManagerTimerParameters Params;
		Params.bMaxOncePerFrame = true;
		Params.bLoop = true;
		Params.FirstDelay = 0.f;
		World->GetTimerManager().SetTimer(
			CallbackHandle,
			this, &UVfxBeamCoordinatorComponent::OnTimerCallback,
			0.001f,
			Params
		);
	}
}

/** To prevent update during TickGroup */
void UVfxBeamCoordinatorComponent::OnTimerCallback()
{
	if (UWorld* World = GetWorld())
	{
		TArray<int32, TInlineAllocator<6>> ToBeRemoved;
		for (TPair<int32, FVfxBeamTargetObject>& Curr : Targets)
		{
			FVfxBeamTargetObject& CurrElem = Curr.Value;
			if (UObject* Obj = CurrElem.Object.Get())
			{
				float DeltaSec = World->DeltaTimeSeconds;
				if (CurrElem.bStarted == false)
				{
					CurrElem.RemainDelay -= DeltaSec;
					if (CurrElem.RemainDelay <= 0.f)
					{
						CurrElem.RemainDuration += CurrElem.RemainDelay;
						IVfxBeamTargetInterface::Execute_OnBeamTargetInteractionStart(Obj, this, Curr.Key, CurrElem.Type);
						OnInteractionStart.ExecuteIfBound(Obj, CurrElem.Type, Curr.Key, CurrElem.RemainDuration);
						CurrElem.bStarted = true;
					}
					else
					{
						return;
					}
				}
				else
				{
					CurrElem.RemainDuration -= DeltaSec;
				}

				const float Ratio = FMath::Clamp(1.f - (CurrElem.RemainDuration / CurrElem.Duration), 0.f, 1.f);
				IVfxBeamTargetInterface::Execute_OnUpdateProgress(Obj, Ratio);

				if (Ratio >= 0.99f)
				{
					ToBeRemoved.Add(Curr.Key);
				}
			}
			else
			{
				ToBeRemoved.Add(Curr.Key);
			}
		}

		if (ToBeRemoved.Num())
		{
			MapLock.WriteLock();
			for (int32 ID : ToBeRemoved)
			{
				FVfxBeamTargetObject CurrElem;
				if (Targets.RemoveAndCopyValue(ID, CurrElem))
				{
					if (UObject* Obj = CurrElem.Object.Get())
					{
						FLastQuery& Query = QueryHistory.FindOrAdd(ID);
						Query.WorldTime = World->TimeSeconds;
						Query.ElapsedSecs = CurrElem.Duration;
						Query.Duration = CurrElem.Duration;
						Query.Progress = IVfxBeamTargetInterface::Execute_GetWorkingProgress(Obj);
						Query.TargetWidth[EAxis::X] = IVfxBeamTargetInterface::Execute_GetWorkingWidth(Obj, EAxis::X);
						Query.TargetWidth[EAxis::Y] = IVfxBeamTargetInterface::Execute_GetWorkingWidth(Obj, EAxis::Y);
						Query.TargetWidth[EAxis::Z] = IVfxBeamTargetInterface::Execute_GetWorkingWidth(Obj, EAxis::Z);
						Query.TargetWidth[EAxis::None] = Query.TargetWidth[EAxis::X];
						Query.TargetLocation[EAxis::X] = IVfxBeamTargetInterface::Execute_GetWorkingWorldPosition(Obj, EAxis::X);
						Query.TargetLocation[EAxis::Y] = IVfxBeamTargetInterface::Execute_GetWorkingWorldPosition(Obj, EAxis::Y);
						Query.TargetLocation[EAxis::Z] = IVfxBeamTargetInterface::Execute_GetWorkingWorldPosition(Obj, EAxis::Z);
						Query.TargetLocation[EAxis::None] = Query.TargetLocation[EAxis::X];
						Query.TargetMoveDirection[EAxis::X] = IVfxBeamTargetInterface::Execute_GetWorkingWorldDirection(Obj, EAxis::X);
						Query.TargetMoveDirection[EAxis::Y] = IVfxBeamTargetInterface::Execute_GetWorkingWorldDirection(Obj, EAxis::Y);
						Query.TargetMoveDirection[EAxis::Z] = IVfxBeamTargetInterface::Execute_GetWorkingWorldDirection(Obj, EAxis::Z);
						Query.TargetMoveDirection[EAxis::None] = Query.TargetMoveDirection[EAxis::X];
						IVfxBeamTargetInterface::Execute_OnBeamTargetInteractionEnd(Obj, this);
						OnInteractionEnd.ExecuteIfBound(CurrElem.Type, ID);
					}
				}
			}

			for (auto It = QueryHistory.CreateIterator(); It; ++It)
			{
				const float Delta = World->TimeSeconds - It.Value().WorldTime;
				if (Delta > 7.f)
				{
					It.RemoveCurrent();
				}
			}
			MapLock.WriteUnlock();

			if (Targets.IsEmpty())
			{
				World->GetTimerManager().ClearTimer(CallbackHandle);
			}
		}
	}
}
