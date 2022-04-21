#pragma once

//#include "GenericPlatform/GenericPlatformMisc.h"
#include "ChunkDownloader.h"//for FPakFileEntry

namespace DLCPackageManagerPrivate
{
	class FHackingType_ChunkDownloader : public TSharedFromThis<FHackingType_ChunkDownloader>
	{
	public:
		//TODO: Add static_assert for checking Unreal version range

		typedef TFunction<void(bool bSuccess)> FCallback;

		struct FStats
		{
			// number of pak files downloaded
			int FilesDownloaded = 0;
			int TotalFilesToDownload = 0;

			// number of bytes downloaded
			uint64 BytesDownloaded = 0;
			uint64 TotalBytesToDownload = 0;

			// number of chunks mounted (chunk is an ordered array of paks)
			int ChunksMounted = 0;
			int TotalChunksToMount = 0;

			// UTC time that loading began (for rate estimates)
			FDateTime LoadingStartTime = FDateTime::MinValue();
			FText LastError;
		};

		FPlatformChunkInstallMultiDelegate OnChunkMounted;

		TFunction<void(const FString& FileName, const FString& Url, uint64 SizeBytes, const FTimespan& DownloadTime, int32 HttpStatus)> OnDownloadAnalytics;

		struct FPakFile
		{
			FPakFileEntry Entry;
			bool bIsCached = false;
			bool bIsMounted = false;

			bool bIsEmbedded = false;
			uint64 SizeOnDisk = 0; // grows as the file is downloaded. See Entry.FileSize for the target size

			// async download
			int32 Priority = 0;
			TSharedPtr<FDownload> Download;
			TArray<FChunkDownloader::FCallback> PostDownloadCallbacks;
		};

		class FMountTask; //No need to hack this type

		struct FChunk
		{
			int32 ChunkId = -1;
			bool bIsMounted = false;

			TArray<TSharedRef<FPakFile>> PakFiles;

			inline bool IsCached() const
			{
				for (const auto& PakFile : PakFiles)
				{
					if (!PakFile->bIsCached)
					{
						return false;
					}
				}
				return true;
			}

			// async mount
			FMountTask* MountTask = nullptr;
		};

		// cumulative stats for loading screen mode
		FStats LoadingModeStats;
		TArray<FCallback> PostLoadCallbacks;
		int32 LoadingCompleteLatch = 0;

		FCallback UpdateBuildCallback;

		// platform name (determines the manifest)
		FString PlatformName;

		// folders to save pak files into on disk
		FString CacheFolder;

		// content folder where we can find some chunks shipped with the build
		FString EmbeddedFolder;

		// build specific ID and URL paths
		FString LastDeploymentName;
		FString ContentBuildId;
		TArray<FString> BuildBaseUrls;

		// chunk id to chunk record
		TMap<int32, TSharedRef<FChunk>> Chunks;

		// pak file name to pak file record
		TMap<FString, TSharedRef<FPakFile>> PakFiles;

		// pak files embedded in the build (immutable, compressed)
		TMap<FString, FPakFileEntry> EmbeddedPaks;

		// do we need to save the manifest (done whenever new downloads have started)
		bool bNeedsManifestSave = false;

		// handle for the per-frame mount ticker in the main thread
		FDelegateHandle MountTicker;

		// manifest download request
		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ManifestRequest;

		// maximum number of downloads to allow concurrently
		int32 TargetDownloadsInFlight = 1;

		// list of pak files that have been requested
		TArray<TSharedRef<FPakFile>> DownloadRequests;
	};
}
