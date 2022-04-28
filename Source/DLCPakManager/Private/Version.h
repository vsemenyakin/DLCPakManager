#pragma once

namespace DLCPackageManagerPrivate
{
	class FVersion
	{
	public:
		FVersion(const int32 InMajor = 1, const int32 InMinor = 0, const int32 InPatch = 0);

		static TOptional<FVersion> FromString(const FString& Version);
		static const FVersion VersionOne;

		FString ToString() const;

		int32 GetMajor() const;
		int32 GetMinor() const;
		int32 GetPatch() const;

		bool operator<(const FVersion& Other) const;
		bool operator==(const FVersion& Other) const;

	private:
		static TOptional<int32> FromString_Member(const TArray<FString>& PathElements, const int32 MemberIndex);

		int32 Major = 0;
		int32 Minor = 0;
		int32 Patch = 0;
	};
}
