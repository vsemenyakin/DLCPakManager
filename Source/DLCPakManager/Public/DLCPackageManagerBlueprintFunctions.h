#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "DLCPackageManagerBlueprintFunctions.generated.h"

UCLASS()
class DLCPAKMANAGER_API UDLCPackageManagerBlueprintFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(
		BlueprintCallable,
		Category = "Utilities",
		Meta = (
			Latent,
			LatentInfo = "LatentInfo",
			DeterminesOutputType = "SoftPtr",
			DynamicOutputParam = "HardPtr",
			WorldContext = "WCO"
		)
	)
	static void LoadDLCAssetPtr(const UObject* WCO, TSoftClassPtr<UObject> SoftPtr, TSubclassOf<UObject>& HardPtr, FLatentActionInfo LatentInfo);
};

