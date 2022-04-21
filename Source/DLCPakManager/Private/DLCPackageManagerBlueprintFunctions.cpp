#include "DLCPackageManagerBlueprintFunctions.h"
#include "DLCPackageManager.h"

#include "LatentActions.h"

template<typename T_FutureResult>
class TFutureWithResultPreparingLatentAction : public FPendingLatentAction
{
public:
	typedef TFunction<void(const T_FutureResult& FutureResult)> FBeforeExecResultPreparingFunction;

	TFutureWithResultPreparingLatentAction(
		const FLatentActionInfo& LatentInfo,
		const TSharedPtr<TFuture<T_FutureResult>>& InFuture,
		const FBeforeExecResultPreparingFunction& InBeforeExecResultPreparing)
		:
		ExecutionFunction(LatentInfo.ExecutionFunction),
		OutputLink(LatentInfo.Linkage),
		CallbackTarget(LatentInfo.CallbackTarget),
		Future(InFuture), BeforeExecResultPreparing(InBeforeExecResultPreparing)
	{
		check(Future.IsValid());
		check(!!BeforeExecResultPreparing);
	}

	void UpdateOperation(FLatentResponse& Response) override
	{
		check(Future.IsValid());
		if (Future->IsReady())
		{
			BeforeExecResultPreparing(Future->Get());
			Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget);
		}
	}

private:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;

	TSharedPtr<TFuture<T_FutureResult>> Future;
	FBeforeExecResultPreparingFunction BeforeExecResultPreparing;
};

template<typename T_DelayLatentAction, typename T_ConstructFunctor>
void StartLatentAction(const UObject* WCO, const FLatentActionInfo& LatentInfo, const T_ConstructFunctor& ConstructFunctor)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WCO, EGetWorldErrorMode::LogAndReturnNull))
	{
		FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
		if (!LatentActionManager.FindExistingAction<T_DelayLatentAction>(LatentInfo.CallbackTarget, LatentInfo.UUID))
		{
			T_DelayLatentAction* Action = ConstructFunctor();
			LatentActionManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, Action);
		}
	}
}

template<typename T_FutureResult, typename CallbackFuncType>
void StartFutureWithResultPreparingLatentAction(
	const UObject* WCO,
	const FLatentActionInfo& LatentInfo,
	TFuture<T_FutureResult>&& Future,
	const CallbackFuncType& CallbackFunc)
{
	auto SharedFuture = MakeShared<TFuture<T_FutureResult>>(MoveTemp(Future));

	using FLatentAction = TFutureWithResultPreparingLatentAction<T_FutureResult>;
	StartLatentAction<FLatentAction>(WCO, LatentInfo, [&LatentInfo, SharedFuture, CallbackFunc]()
	{
		return new FLatentAction(LatentInfo, SharedFuture, CallbackFunc);
	});
}

// ==============================================================================

void UDLCPackageManagerBlueprintFunctions::LoadDLCAssetPtr(const UObject* WCO, TSoftClassPtr<UObject> SoftPtr, TSubclassOf<UObject>& HardPtr, FLatentActionInfo LatentInfo)
{
	StartFutureWithResultPreparingLatentAction(WCO, LatentInfo, FDLCPackageManager::Get().GetLoadedPath(SoftPtr), [&HardPtr](const TSubclassOf<UObject>& Loaded)
	{
		HardPtr = Loaded;
	});
}
