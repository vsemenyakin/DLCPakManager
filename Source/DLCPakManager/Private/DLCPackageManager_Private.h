#pragma once

#include "Version.h"

struct FPakFileEntry;

struct FDLCPackageManager_Private
{
	static const FString MovedFilePrefix;

	static TOptional<FString> GetDLCChunkId(const FSoftObjectPtr& SoftObjectPtr);

	static const FString& GetDLCChunkIDForPakFileEntry(const FPakFileEntry& PakFileEntry);

	struct FParsedDLCChunkID
	{
		FString Name;
		DLCPackageManagerPrivate::FVersion Version;
	};

	struct FParseDLCChunkIDSettings
	{
		TOptional<DLCPackageManagerPrivate::FVersion> DefaultVersion = DLCPackageManagerPrivate::FVersion::VersionOne;
	};

	static TOptional<FParsedDLCChunkID> ParseDLCChunkID(const FString& DLCChunkID, const FParseDLCChunkIDSettings& Settings = {});
};
