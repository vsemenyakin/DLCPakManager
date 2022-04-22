#include "DLCPackageManager.h"
#include "Async.h"
#include "PrivateHacking.h"
#include "PrivateHacking_ChunkDownloader.h"
#include "DLCPackageManager_Debug.h"

#include "ChunkDownloader.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "GenericPlatform/GenericPlatformFile.h"

namespace DLCPackageManagerPrivate
{
	FString GetLoadingLogPrefix(const FSoftObjectPtr& SoftObjectPtr)
	{
		return FString::Printf(TEXT("Loading of object [%s]"), *SoftObjectPtr.ToString());
	}

	TOptional<FString> GetDLCChunkId(const FSoftObjectPtr& SoftObjectPtr)
	{
		const FSoftObjectPath& SoftObjectPath = SoftObjectPtr.ToSoftObjectPath();
		
		if (SoftObjectPath.IsNull())
			return { };

		TArray<FString> PathElements;
		SoftObjectPath.ToString().ParseIntoArray(PathElements, TEXT("/"));

		// Check path root: is "Game" root and is not last in path

		static const FString RootName{ TEXT("Game") };
		const int32 RootPathElementIndex = PathElements.IndexOfByKey(RootName);
		if (RootPathElementIndex == INDEX_NONE || (RootPathElementIndex == PathElements.Num() - 1))
			return { };

		// Check if sub-root is DLC root

		const FString& SubRootPathElement = PathElements[RootPathElementIndex + 1];
		static const FString DLCSubRootPathElementPrefix{ TEXT("DLC_") };

		FString BeforePrefixString;
		FString AfterPrefixString;
		SubRootPathElement.Split(DLCSubRootPathElementPrefix, &BeforePrefixString, &AfterPrefixString, ESearchCase::CaseSensitive);

		//Check if there is some text before DLC prefix (should be no text)
		if (!BeforePrefixString.IsEmpty())
			return { };

		return { MoveTemp(AfterPrefixString) };
	}

	const FString& GetDLCChunkIDForPakFileEntry(const FPakFileEntry& PakFileEntry)
	{
		return PakFileEntry.FileVersion;
	}

	FString GetDLCChunkDownloadingLogPrefix(const FString& DLCChunkID)
	{
		return FString::Printf(TEXT("Downloading DLC chunk [%s]"), *DLCChunkID);
	}

	const FString MovedFilePrefix = TEXT(".renamed");
}

FDLCPackageManager::FDLCPackageManager(const FString& DeploymentName, const FString& ContentBuildId)
{
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
	CacheMovingState.MovedCachePath = CacheMovingState.OriginalCachePath + DLCPackageManagerPrivate::MovedFilePrefix;
	CacheMovingState.bMovedCacheExist = IPlatformFile::GetPlatformPhysical().MoveFile(*CacheMovingState.OriginalCachePath, *CacheMovingState.MovedCachePath);
	
	ChunkDownloader->UpdateBuild(DeploymentName, ContentBuildId, [this, CacheMovingState = MoveTemp(CacheMovingState), DeploymentName](bool bSuccess)
	{
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
	UE_LOG(LogDLCLoading, VeryVerbose, TEXT("%s status: Requested"),
		*DLCPackageManagerPrivate::GetLoadingLogPrefix(SoftObjectPtr));
	
	if (SoftObjectPtr.IsNull())
	{
		UE_LOG(LogDLCLoading, Warning, TEXT("%s error: Empty soft reference passed"),
			*DLCPackageManagerPrivate::GetLoadingLogPrefix(SoftObjectPtr));
		
		return DLCPackageManagerPrivate::FilledFuture<UObject*>(nullptr);
	}

	if (SoftObjectPtr.IsValid())
	{
		UE_LOG(LogDLCLoading, VeryVerbose, TEXT("%s result: Reference is already resolved"),
			*DLCPackageManagerPrivate::GetLoadingLogPrefix(SoftObjectPtr));
		
		return DLCPackageManagerPrivate::FilledFuture<UObject*>(SoftObjectPtr.Get());
	}

	auto LoadingPromise = MakeShared<DLCPackageManagerPrivate::TPromiseWithWorkaroundedCanceling<UObject*>>();
	TFuture<UObject*> LoadingFuture = LoadingPromise->GetFuture();

	UE_LOG(LogDLCLoading, VeryVerbose, TEXT("%s status: Start waiting package manager initialization"),
		*DLCPackageManagerPrivate::GetLoadingLogPrefix(SoftObjectPtr));
	
	PackageManagerInitializationPromise->MakeFuture().Next([LoadingPromise, SoftObjectPtr, this](int32)
	{
		UE_LOG(LogDLCLoading, VeryVerbose, TEXT("%s status: Finish waiting package manager initialization"),
			*DLCPackageManagerPrivate::GetLoadingLogPrefix(SoftObjectPtr));
		
		const TOptional<FString> DLCChunkId = DLCPackageManagerPrivate::GetDLCChunkId(SoftObjectPtr);
		
		TFuture<void> DownloadedDLCChunkFuture;

		if (DLCChunkId.IsSet())
		{
			UE_LOG(LogDLCLoading, VeryVerbose, TEXT("%s status: Found chunk [%s] for path. Starting DLC chunk downloading"),
				*DLCPackageManagerPrivate::GetLoadingLogPrefix(SoftObjectPtr), *DLCChunkId.GetValue());
			
			DownloadedDLCChunkFuture = DownloadDLCChunk(DLCChunkId.GetValue());
		}
		else
		{
			UE_LOG(LogDLCLoading, VeryVerbose, TEXT("%s status: No DLC chunks for path, reference is expected to be placed in the main package"),
				*DLCPackageManagerPrivate::GetLoadingLogPrefix(SoftObjectPtr));
			
			DownloadedDLCChunkFuture = DLCPackageManagerPrivate::FilledFuture();
		}

		DownloadedDLCChunkFuture.Next([LoadingPromise, SoftObjectPtr](int32)
		{
			UE_LOG(LogDLCLoading, VeryVerbose, TEXT("%s status: Asset by soft reference is ready to be loaded to RAM"),
				*DLCPackageManagerPrivate::GetLoadingLogPrefix(SoftObjectPtr));
			
			UAssetManager* Manager = UAssetManager::GetIfValid();
			check(Manager);
			Manager->GetStreamableManager().RequestAsyncLoad(
				{ SoftObjectPtr.ToSoftObjectPath() },
				[LoadingPromise, SoftObjectPtr]()
				{					
					UObject* Result = SoftObjectPtr.Get();

					if (Result)
					{
						UE_LOG(LogDLCLoading, Verbose, TEXT("%s status: Asset is loaded to RAM and ready for use"),
							*DLCPackageManagerPrivate::GetLoadingLogPrefix(SoftObjectPtr));
					}
					else
					{
						UE_LOG(LogDLCLoading, Warning, TEXT("%s error: Unexpected asset loading error"),
							*DLCPackageManagerPrivate::GetLoadingLogPrefix(SoftObjectPtr));
					}
				
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

TOptional<int32> FDLCPackageManager::GetChunkIdForDLCChunkId(const FString& DLCChunkId) const
{
	const auto& ChunkDownloaderHacked = GetChunkDownloaderHackedAccess();
	
	using FPakFile = DLCPackageManagerPrivate::FHackingType_ChunkDownloader::FPakFile;
	for (const TPair<FString, TSharedRef<FPakFile>>& PakFile : ChunkDownloaderHacked.PakFiles)
		if (DLCPackageManagerPrivate::GetDLCChunkIDForPakFileEntry(PakFile.Value->Entry) == DLCChunkId)
			return { PakFile.Value->Entry.ChunkId };

	return { };
}

FString FDLCPackageManager::GetChunkDownloaderCachedManifestFilePath() const
{
	const auto& ChunkDownloaderHacked = GetChunkDownloaderHackedAccess();

	return ChunkDownloaderHacked.CacheFolder / ChunkDownloaderHacked.CACHED_BUILD_MANIFEST;
}

TFuture<void> FDLCPackageManager::DownloadDLCChunk(const FString& DLCChunkID)
{
	UE_LOG(LogDLCLoading, VeryVerbose, TEXT("%s status: Start"),
		*DLCPackageManagerPrivate::GetDLCChunkDownloadingLogPrefix(DLCChunkID));
	
	const TOptional<int32> PossibleChunkId = GetChunkIdForDLCChunkId(DLCChunkID);
	//TODO: Return error "No chunk with proivded Id"
	if (!PossibleChunkId.IsSet())
	{
		UE_LOG(LogDLCLoading, Warning, TEXT("%s error: Cannot find Chunk Id for DLC Chunk Id"),
			*DLCPackageManagerPrivate::GetDLCChunkDownloadingLogPrefix(DLCChunkID));
		
		return DLCPackageManagerPrivate::FilledFuture();
	}

	TSharedPtr<FChunkState>* ChunkStatePtr = DLCChunkStates.Find(DLCChunkID);
	if (!ChunkStatePtr)
	{
		UE_LOG(LogDLCLoading, VeryVerbose, TEXT("%s status: DLC chunk with Chunk Id [%d] is downloading first time"),
			*DLCPackageManagerPrivate::GetDLCChunkDownloadingLogPrefix(DLCChunkID), PossibleChunkId.GetValue());
		
		auto NewChunkState = MakeShared<FChunkState>(TInPlaceType<FChunkState_NotDownloaded>{ }, FChunkState_NotDownloaded{ });
		ChunkStatePtr = &DLCChunkStates.Add(DLCChunkID, MoveTemp(NewChunkState));
	}

	FChunkState& ChunkState = **ChunkStatePtr;

	if (ChunkState.IsType<FChunkState_NotDownloaded>())
	{
		ChunkState.Emplace<FChunkState_DownloadingAndMounting>();
		auto& ChunkState_DownloadingAndMounting = ChunkState.Get<FChunkState_DownloadingAndMounting>();
		ChunkState_DownloadingAndMounting.Promise = MakeUnique<TMultiPromise<void>>();
		
		UE_LOG(LogDLCLoading, VeryVerbose,
			TEXT("%s status: DLC Chunk had [NotDownloaded] state. Switching to [DownloadingAndMounting] state"),
			*DLCPackageManagerPrivate::GetDLCChunkDownloadingLogPrefix(DLCChunkID), PossibleChunkId.GetValue());
		
		const int32 ChunkId = PossibleChunkId.GetValue();
		
		//TODO: Put here tracking of concrete chunk loading process
		ChunkDownloader->DownloadChunks({ ChunkId }, [](const bool bSuccess){ }, 1);

		ChunkDownloader->BeginLoadingMode([this, &ChunkState, ChunkId, Debug_DLCChunkID = DLCChunkID](const bool bSuccess)
		{
			//TODO: Check if "this" is OK
			this->ChunkDownloader->MountChunk(ChunkId, [&ChunkState, Debug_DLCChunkID](const bool bSuccess)
			{
				UE_LOG(LogDLCLoading, VeryVerbose,
					TEXT("%s status: DLC Chunk had [DownloadingAndMounting] state. After filling promise it finaly will have [Mounted] state"),
					*DLCPackageManagerPrivate::GetDLCChunkDownloadingLogPrefix(Debug_DLCChunkID));
				
				//TODO: Check if "this" is OK
				//TODO: Move promise and setup "FChunkState_Mounted" state before filling promise for more consistent state in callbacks after filling promise
				ChunkState.Get<FChunkState_DownloadingAndMounting>().Promise->SetValue();

				ChunkState.Emplace<FChunkState_Mounted>();
			});
		});
	}
	
	if (auto* ChunkState_DownloadingAndMounting = ChunkState.TryGet<FChunkState_DownloadingAndMounting>())
	{
		UE_LOG(LogDLCLoading, VeryVerbose,
			TEXT("%s status: Actually started downloading request. Waiting [DownloadingAndMounting] finish"),
			*DLCPackageManagerPrivate::GetDLCChunkDownloadingLogPrefix(DLCChunkID));
		
		return ChunkState_DownloadingAndMounting->Promise->MakeFuture();
	}

	//TODO: Put "Success" result info
	check(ChunkState.IsType<FChunkState_Mounted>());

	UE_LOG(LogDLCLoading, Verbose,
		TEXT("%s status: DLC Chunk is finaly in [FChunkState_Mounted] state. Immediately return filled future"),
		*DLCPackageManagerPrivate::GetDLCChunkDownloadingLogPrefix(DLCChunkID));
	
	return DLCPackageManagerPrivate::FilledFuture();
}

const DLCPackageManagerPrivate::FHackingType_ChunkDownloader& FDLCPackageManager::GetChunkDownloaderHackedAccess() const
{
	return DLCPackageManagerPrivate::GetHackedType<DLCPackageManagerPrivate::FHackingType_ChunkDownloader>(*ChunkDownloader.Get());
}
