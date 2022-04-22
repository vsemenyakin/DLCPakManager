#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"
#include "Engine/AssetManager.h"

class FChunkDownloader;
namespace DLCPackageManagerPrivate { template<typename T> class TMultiPromise; }
namespace DLCPackageManagerPrivate { class FHackingType_ChunkDownloader; }

class FDLCPackageManager
{
public:
	static FDLCPackageManager& Get();

	TFuture<UObject*> GetLoadedPath(const FSoftObjectPtr& SoftObjectPtr);
	
	template<typename Type>
	TFuture<TSubclassOf<Type>> GetLoadedPath(const TSoftClassPtr<Type>& SoftObjectPtr)
	{
		const auto BaseSoftObjectPtr = SoftObjectPtr.IsValid() ?
			FSoftObjectPtr{ SoftObjectPtr.Get() } :
			FSoftObjectPtr{ SoftObjectPtr.ToSoftObjectPath() };
		
		return GetLoadedPath(BaseSoftObjectPtr).Next([](UObject* LoadedObject)
		{
			return LoadedObject ?
				TSubclassOf<Type>{ CastChecked<UClass>(LoadedObject) } :
				TSubclassOf<Type>{ };
		});
	}
	
	~FDLCPackageManager();

private:
	//NB: "TMultiPromise<>" is used with "Shared Ptr" to prevent
	// including "TMultiPromise<>" to public dependencies of the class
	template<typename T>
	using TMultiPromise = DLCPackageManagerPrivate::TMultiPromise<T>;
	
	// DeploymentName - path for folder in CDN
	// Example of section in "DefaultGame.ini":
	//   [/Script/Plugins.ChunkDownloader PatchingDemoLive]
	//   + CdnBaseUrls = 127.0.0.1/UnrealPatchingCDN
	FDLCPackageManager(const FString& DeploymentName, const FString& ContentBuildId);
	
	TOptional<int32> GetChunkIdForDLCChunkId(const FString& DLCChunkId) const;
	FString GetChunkDownloaderCachedManifestFilePath() const;
	
	TFuture<void> DownloadDLCChunk(const FString& DLCChunkID);

	const DLCPackageManagerPrivate::FHackingType_ChunkDownloader& GetChunkDownloaderHackedAccess() const;
	
	TSharedPtr<FChunkDownloader> ChunkDownloader{ };

	struct FChunkState_NotDownloaded { };
	struct FChunkState_DownloadingAndMounting
	{
		TUniquePtr<TMultiPromise<void>> Promise;
	};
	struct FChunkState_Mounted { };
	using FChunkState = TVariant<
		FChunkState_NotDownloaded,
		FChunkState_DownloadingAndMounting,
		FChunkState_Mounted>;

	// Unknown -> Downloading -> Mounting

	TMap<FString, TSharedPtr<FChunkState>> DLCChunkStates;
	TSharedPtr<TMultiPromise<void>> PackageManagerInitializationPromise;
};
