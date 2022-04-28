#include "DLCPackageManager_Private.h"

const FString FDLCPackageManager_Private::MovedFilePrefix = TEXT(".renamed");

TOptional<FString> FDLCPackageManager_Private::GetDLCChunkId(const FSoftObjectPtr& SoftObjectPtr)
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

const FString& FDLCPackageManager_Private::GetDLCChunkIDForPakFileEntry(const FPakFileEntry& PakFileEntry)
{
	return PakFileEntry.FileVersion;
}


// - - - -

TOptional<FDLCPackageManager_Private::FParsedDLCChunkID> FDLCPackageManager_Private::ParseDLCChunkID(
	const FString& DLCChunkID, const FParseDLCChunkIDSettings& Settings)
{
	FDLCPackageManager::FDLCPackage::FVersionInfo Result;

	static const FString ChunkIdDelimiter{ TEXT("_") };

	FString DLCPackageNameString;
	FString VersionString;
	DLCChunkID.Split(ChunkIdDelimiter, &DLCPackageNameString, &VersionString, ESearchCase::CaseSensitive);

	TOptional<DLCPackageManagerPrivate::FVersion> Version;
	
	if (!VersionString.IsEmpty())
	{
		Version = DLCPackageManagerPrivate::FVersion::FromString(VersionString);
	}
	else if (Settings.DefaultVersion.IsSet())
	{
		Version = Settings.DefaultVersion.GetValue();
	}

	return Version.IsSet() ?
		TOptional<FParsedDLCChunkID>{ FParsedDLCChunkID{ DLCPackageNameString, Version.GetValue() } } :
		TOptional<FParsedDLCChunkID>{ };
}
