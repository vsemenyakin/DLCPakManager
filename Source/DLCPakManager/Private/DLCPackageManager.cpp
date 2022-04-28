#include "DLCPackageManager.h"
#include "Async.h"
#include "PrivateHacking.h"
#include "PrivateHacking_ChunkDownloader.h"
#include "Version.h"
#include "DLCPackageManager_Private.h"
#include "DLCPackageManager_Debug.h"

#include "ChunkDownloader.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Algo/MaxElement.h"

FDLCPackageManager::FDLCPackageManager(const FString& DeploymentName, const FString& ContentBuildId)
{
	using EPrintType = FDLCPackageManager_Debug::EPrintType;
	FDLCPackageManager_Debug::FLogging_Initialization Logging{ };

	Logging.PrintLog(EPrintType::Status, TEXT("Started"));

	ChunkDownloader = FChunkDownloader::GetOrCreate();
	PackageManagerInitializationPromise = MakeShared<TMultiPromise<void>>();
	
	//NB: Cache was used despite changes in CDN Manifest
	//TODO: Find why CDN manifest was not reloaded
	// load the cached build ID
	ChunkDownloader->Initialize("Windows", 8);

	//NB: We remove build manifest cache to force uploading of all actual DLC files list
	struct FChunkDownloaderCacheMovingState
	{
		bool bMovedCacheExist;
		FString OriginalCachePath;
		FString MovedCachePath;
	};

	FChunkDownloaderCacheMovingState CacheMovingState;
	CacheMovingState.OriginalCachePath = GetChunkDownloaderCachedManifestFilePath();
	CacheMovingState.MovedCachePath = CacheMovingState.OriginalCachePath + FDLCPackageManager_Private::MovedFilePrefix;
	CacheMovingState.bMovedCacheExist = IPlatformFile::GetPlatformPhysical().MoveFile(*CacheMovingState.OriginalCachePath, *CacheMovingState.MovedCachePath);

	if (!CacheMovingState.bMovedCacheExist)
	{
		Logging.PrintLog(EPrintType::StatusImportant, TEXT("There where no manifrst file cache"));
	}

	ChunkDownloader->UpdateBuild(DeploymentName, ContentBuildId, [this, CacheMovingState = MoveTemp(CacheMovingState), DeploymentName, Logging](bool bSuccess)
	{
		Logging.PrintLog(EPrintType::Status, TEXT("Build updated %s"), bSuccess ? TEXT("successful") : TEXT("unsuccessful"));

		if (CacheMovingState.bMovedCacheExist)
		{
			if (bSuccess)
			{
				IPlatformFile::GetPlatformPhysical().DeleteFile(*GetChunkDownloaderCachedManifestFilePath());
			}
			else
			{
				IPlatformFile::GetPlatformPhysical().MoveFile(*CacheMovingState.MovedCachePath, *CacheMovingState.OriginalCachePath);
				ChunkDownloader->LoadCachedBuild(DeploymentName);
			}
		}

		Initialize_PackagesInfo();

		this->PackageManagerInitializationPromise->SetValue();
	});
}

FDLCPackageManager& FDLCPackageManager::Get()
{
	static const FString DeploymentName = "PatchingDemoLive";
	static const FString ContentBuildId = "PatchingDemoKey";
	static FDLCPackageManager Downloader{ DeploymentName, ContentBuildId };
	return Downloader;
}
	
TFuture<UObject*> FDLCPackageManager::GetLoadedPath(const FSoftObjectPtr& SoftObjectPtr)
{
	using EPrintType = FDLCPackageManager_Debug::EPrintType;
	FDLCPackageManager_Debug::FLogging_Loading Logging{ SoftObjectPtr };

	Logging.PrintLog(EPrintType::Status, TEXT("Start"));
	
	if (SoftObjectPtr.IsNull())
	{
		Logging.PrintLog(EPrintType::Warning, TEXT("Empty soft reference passed"));
		
		return DLCPackageManagerPrivate::FilledFuture<UObject*>(nullptr);
	}

	if (SoftObjectPtr.IsValid())
	{
		Logging.PrintLog(EPrintType::Status, TEXT("Reference is already resolved"));

		return DLCPackageManagerPrivate::FilledFuture<UObject*>(SoftObjectPtr.Get());
	}

	auto LoadingPromise = MakeShared<DLCPackageManagerPrivate::TPromiseWithWorkaroundedCanceling<UObject*>>();
	TFuture<UObject*> LoadingFuture = LoadingPromise->GetFuture();

	Logging.PrintLog(EPrintType::Status, TEXT("Start waiting package manager initialization"));

	PackageManagerInitializationPromise->MakeFuture().Next([LoadingPromise, SoftObjectPtr, this, Logging](int32)
	{
		Logging.PrintLog(EPrintType::Status, TEXT("Finish waiting package manager initialization"));
		
		const TOptional<FString> DLCChunkId = FDLCPackageManager_Private::GetDLCChunkId(SoftObjectPtr);
		
		TFuture<void> DownloadedDLCChunkFuture;

		if (DLCChunkId.IsSet())
		{
			Logging.PrintLog(EPrintType::Status, TEXT("Found chunk [%s] for path. Starting DLC chunk downloading"),
				*DLCChunkId.GetValue());
			
			DownloadedDLCChunkFuture = DownloadDLCChunk(DLCChunkId.GetValue());
		}
		else
		{
			Logging.PrintLog(EPrintType::Status, TEXT("No DLC chunks for path, reference is expected to be placed in the main package"));
			
			DownloadedDLCChunkFuture = DLCPackageManagerPrivate::FilledFuture();
		}

		DownloadedDLCChunkFuture.Next([LoadingPromise, SoftObjectPtr, Logging](int32)
		{
			Logging.PrintLog(EPrintType::Status, TEXT("Asset by soft reference is ready to be loaded to RAM"));
			
			UAssetManager* Manager = UAssetManager::GetIfValid();
			check(Manager);
			Manager->GetStreamableManager().RequestAsyncLoad(
				{ SoftObjectPtr.ToSoftObjectPath() },
				[LoadingPromise, SoftObjectPtr, Logging]()
				{					
					UObject* Result = SoftObjectPtr.Get();

					if (Result)
						Logging.PrintLog(EPrintType::StatusImportant, TEXT("Asset is loaded to RAM and ready for use"));
					else
						Logging.PrintLog(EPrintType::StatusImportant, TEXT("Unexpected asset loading error"));
				
					LoadingPromise->EmplaceValue(MoveTemp(Result));
				});
		});
	});

	return LoadingFuture;
}

FDLCPackageManager::~FDLCPackageManager()
{
	FChunkDownloader::Shutdown();
}

void FDLCPackageManager::Initialize_PackagesInfo()
{
	using EPrintType = FDLCPackageManager_Debug::EPrintType;
	FDLCPackageManager_Debug::FLogging_Initialization Logging{ };

	using FPakFile = DLCPackageManagerPrivate::FHackingType_ChunkDownloader::FPakFile;

	const auto& ChunkDownloaderHacked = GetChunkDownloaderHackedAccess();

	for (const TPair<FString, TSharedRef<FPakFile>>& PakFile : ChunkDownloaderHacked.PakFiles)
	{
		const FPakFileEntry& PakFileEntry = PakFile.Value->Entry;

		const FString& DLCChunkID = FDLCPackageManager_Private::GetDLCChunkIDForPakFileEntry(PakFileEntry);

		TOptional<FDLCPackageManager_Private::FParsedDLCChunkID> ParsedDLCChunkIDResult = FDLCPackageManager_Private::ParseDLCChunkID(DLCChunkID);
		if (!ParsedDLCChunkIDResult.IsSet())
		{
			Logging.PrintLog(EPrintType::Error, TEXT("Package info parse failure. Cannot parse package with DLC chunk id [%s], chunk id [%d]. Version ignored"),
				*DLCChunkID, PakFileEntry.ChunkId);

			continue;
		}

		const FDLCPackageManager_Private::FParsedDLCChunkID& ParsedDLCChunkID = ParsedDLCChunkIDResult.GetValue();
		const FString& DLCPackageName = ParsedDLCChunkID.Name;

		FDLCPackage* PackagePtr = FindDLCPackage(DLCPackageName);
		if (!PackagePtr)
		{
			TSharedPtr<FDLCPackage>& NewPackageSharedPtr = DLCPackages[DLCPackages.Emplace()];
			NewPackageSharedPtr = MakeShared<FDLCPackage>();

			PackagePtr = NewPackageSharedPtr.Get();
			PackagePtr->Name = DLCPackageName;
		}

		TArray<FDLCPackage::FVersionInfo>& VersionInfos = PackagePtr->VersionInfos;
		FDLCPackage::FVersionInfo& NewVersion = VersionInfos[VersionInfos.Emplace()];
		NewVersion.Version = MakeShared<DLCPackageManagerPrivate::FVersion>(ParsedDLCChunkID.Version);
	}
}

const FDLCPackageManager::FDLCPackage::FVersionInfo& FDLCPackageManager::FDLCPackage::GetLatestVersionInfo() const
{
	return *Algo::MaxElementBy(
		VersionInfos,
		[](const FDLCPackage::FVersionInfo& Version) {
			return *Version.Version;
		});
}

FString FDLCPackageManager::GetChunkDownloaderCachedManifestFilePath() const
{
	const auto& ChunkDownloaderHacked = GetChunkDownloaderHackedAccess();

	return ChunkDownloaderHacked.CacheFolder / ChunkDownloaderHacked.CACHED_BUILD_MANIFEST;
}

TFuture<void> FDLCPackageManager::DownloadDLCChunk(const FString& DLCChunkID)
{
	using EPrintType = FDLCPackageManager_Debug::EPrintType;
	FDLCPackageManager_Debug::FLogging_DLCChunkDownloading Logging{ DLCChunkID };

	Logging.PrintLog(EPrintType::Status, TEXT("Start"));
	
	FDLCPackage* DLCPackage = FindDLCPackage(DLCChunkID);
	//TODO: Return error "No chunk with proivded Id"
	if (!DLCPackage)
	{
		Logging.PrintLog(EPrintType::Warning, TEXT("Cannot find Chunk Id for DLC Chunk Id"));
		
		return DLCPackageManagerPrivate::FilledFuture();
	}

	FDLCPackage::FStatus& PackageStatus = DLCPackage->Status;
	if (PackageStatus.IsType<FDLCPackage::FStatus_NotDownloaded>())
	{
		PackageStatus.Emplace<FDLCPackage::FStatus_DownloadingAndMounting>();
		PackageStatus.Get<FDLCPackage::FStatus_DownloadingAndMounting>().Promise = MakeShared<TMultiPromise<void>>();
		
		const FDLCPackageManager::FDLCPackage::FVersionInfo& VersionInfo = DLCPackage->GetLatestVersionInfo();
		const int32 VersionChunkId = VersionInfo.ChunkId;

		Logging.PrintLog(EPrintType::Status, TEXT("DLC Chunk had [NotDownloaded] state. Switching to [DownloadingAndMounting] state. DLC version [%s] aka chunk pak [%d]"),
			*VersionInfo.Version->ToString(), VersionChunkId);
		
		//TODO: Put here tracking of concrete chunk loading process
		ChunkDownloader->DownloadChunks({ VersionChunkId }, [](const bool bSuccess){ }, 1);

		ChunkDownloader->BeginLoadingMode([this, &PackageStatus, VersionChunkId, Debug_DLCChunkID = DLCChunkID, Logging](const bool bSuccess)
		{
			//TODO: Check if "this" is OK
			this->ChunkDownloader->MountChunk(VersionChunkId, [&PackageStatus, Debug_DLCChunkID, Logging](const bool bSuccess)
			{
				Logging.PrintLog(EPrintType::Status, TEXT("DLC Chunk had [DownloadingAndMounting] state. After filling promise it finaly will have [Mounted] state"));
				
				//TODO: Check if "this" is OK
				//TODO: Move promise and setup "FChunkState_Mounted" state before filling promise for more consistent state in callbacks after filling promise
				PackageStatus.Get<FDLCPackage::FStatus_DownloadingAndMounting>().Promise->SetValue();

				PackageStatus.Emplace<FDLCPackage::FStatus_Mounted>();
			});
		});
	}
	
	if (auto* ChunkState_DownloadingAndMounting = PackageStatus.TryGet<FDLCPackage::FStatus_DownloadingAndMounting>())
	{
		Logging.PrintLog(EPrintType::Status, TEXT("Actually started downloading request. Waiting [DownloadingAndMounting] finish"));
		
		return ChunkState_DownloadingAndMounting->Promise->MakeFuture();
	}

	//TODO: Put "Success" result info
	check(PackageStatus.IsType<FDLCPackage::FStatus_Mounted>());

	Logging.PrintLog(EPrintType::Status, TEXT("DLC Chunk is finaly in [FChunkState_Mounted] state. Return filled future"));
	
	return DLCPackageManagerPrivate::FilledFuture();
}

const DLCPackageManagerPrivate::FHackingType_ChunkDownloader& FDLCPackageManager::GetChunkDownloaderHackedAccess() const
{
	return DLCPackageManagerPrivate::GetHackedType<DLCPackageManagerPrivate::FHackingType_ChunkDownloader>(*ChunkDownloader.Get());
}

const FDLCPackageManager::FDLCPackage* FDLCPackageManager::FindDLCPackage(const FString& PackageName) const
{
	const TSharedPtr<FDLCPackage>* DLCPackagePtrPtr = DLCPackages.FindByPredicate(
		[&PackageName](const TSharedPtr<FDLCPackage>& Package) {
			return (Package->Name == PackageName);
		});
	return DLCPackagePtrPtr ? DLCPackagePtrPtr->Get() : nullptr;
}

FDLCPackageManager::FDLCPackage* FDLCPackageManager::FindDLCPackage(const FString& PackageName)
{
	const FDLCPackage* ConstPackage = const_cast<const FDLCPackageManager*>(this)->FindDLCPackage(PackageName);
	return const_cast<FDLCPackage*>(ConstPackage);
}
