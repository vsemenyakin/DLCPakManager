#include "Version.h"

#include "Misc/DefaultValueHelper.h"

namespace DLCPackageManagerPrivate
{
	FVersion::FVersion(const int32 InMajor, const int32 InMinor, const int32 InPatch)
		: Major(InMajor), Minor(InMinor), Patch(InPatch) { }

	const FVersion FVersion::VersionOne = FVersion{ 1, 0, 0 };

	TOptional<FVersion> FVersion::FromString(const FString& Version)
	{
		//TODO: Form an error here
		if (Version.IsEmpty())
		{
			return TOptional<FVersion>{ };
		}

		TOptional<FVersion> Result;
		FVersion& ResultValue = Result.Emplace();

		TArray<FString> PathElements;
		Version.ParseIntoArray(PathElements, TEXT("."));

		ResultValue.Major = FromString_Member(PathElements, 0).Get(0);
		ResultValue.Minor = FromString_Member(PathElements, 1).Get(0);
		ResultValue.Patch = FromString_Member(PathElements, 2).Get(0);

		return Result;
	}

	FString FVersion::ToString() const
	{
		return FString::Printf(TEXT("%d.%d.%d"), Major, Minor, Patch);
	}

	int32 FVersion::GetMajor() const
	{
		return Major;
	}
	
	int32 FVersion::GetMinor() const
	{
		return Minor;
	}
	
	int32 FVersion::GetPatch() const
	{
		return Patch;
	}

	bool FVersion::operator<(const FVersion& Other) const
	{
		if (Major != Other.Major)
			return (Major < Other.Major);

		if (Minor != Other.Minor)
			return (Minor < Other.Minor);

		return Patch < Other.Patch;
	}

	bool FVersion::operator==(const FVersion& Other) const
	{
		return
			(Major == Other.Major) &&
			(Minor == Other.Minor) &&
			(Patch == Other.Patch);
	}

	TOptional<int32> FVersion::FromString_Member(const TArray<FString>& PathElements, const int32 MemberIndex)
	{
		if (PathElements.Num() <= MemberIndex)
			return TOptional<int32>{ };

		int32 Value;
		const bool bSuccess = FDefaultValueHelper::ParseInt(PathElements[MemberIndex], Value);

		if (!bSuccess)
		{
			//TODO: Print warning
		}

		return bSuccess ?
			TOptional<int32>{ Value } :
			TOptional<int32>{ };
	}
}
