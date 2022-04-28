#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"
#include "Engine/AssetManager.h"

class FChunkDownloader;
namespace DLCPackageManagerPrivate { template<typename T> class TMultiPromise; }
namespace DLCPackageManagerPrivate { class FHackingType_ChunkDownloader; }
namespace DLCPackageManagerPrivate { class FVersion; }

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
	void Initialize_PackagesInfo();

	//NB: "TMultiPromise<>" is used with "Shared Ptr" to prevent
	// including "TMultiPromise<>" to public dependencies of the class
	template<typename T>
	using TMultiPromise = DLCPackageManagerPrivate::TMultiPromise<T>;
	
	// DeploymentName - path for folder in CDN
	// Example of section in "DefaultGame.ini":
	//   [/Script/Plugins.ChunkDownloader PatchingDemoLive]
	//   + CdnBaseUrls = 127.0.0.1/UnrealPatchingCDN
	FDLCPackageManager(const FString& DeploymentName, const FString& ContentBuildId);
	
	FString GetChunkDownloaderCachedManifestFilePath() const;
	
	TFuture<void> DownloadDLCChunk(const FString& DLCChunkID);

	const DLCPackageManagerPrivate::FHackingType_ChunkDownloader& GetChunkDownloaderHackedAccess() const;

	struct FDLCPackage;
	const FDLCPackage* FindDLCPackage(const FString& PackageName) const;
	FDLCPackage* FindDLCPackage(const FString& PackageName);

	friend struct FDLCPackageManager_Private;
	friend struct FDLCPackageManager_Debug;


	TSharedPtr<FChunkDownloader> ChunkDownloader{ };

	struct FDLCPackage
	{
		struct FVersionInfo;
		const FVersionInfo& GetLatestVersionInfo() const;

		struct FStatus_NotDownloaded { };
		struct FStatus_DownloadingAndMounting
		{
			TSharedPtr<TMultiPromise<void>> Promise;
		};
		struct FStatus_Mounted { };
		using FStatus = TVariant<
			FStatus_NotDownloaded,
			FStatus_DownloadingAndMounting,
			FStatus_Mounted>;

		FString Name;

		struct FVersionInfo
		{
			TSharedPtr<DLCPackageManagerPrivate::FVersion> Version;
			int32 ChunkId;
		};
		TArray<FVersionInfo> VersionInfos;

		FStatus Status;
	};

	//NB: Shared ptr is used for possiblity to pass FDLCPackage by reference
	TArray<TSharedPtr<FDLCPackage>> DLCPackages;
	TSharedPtr<TMultiPromise<void>> PackageManagerInitializationPromise;
};
