#pragma once

DECLARE_LOG_CATEGORY_EXTERN(LogDLCLoading, VeryVerbose, All);


struct FDLCPackageManager_Debug
{
	enum class EPrintType
	{
		Status,
		StatusImportant,
		Warning,
		Error
	};

	static const TCHAR* GetLogPerfixForLogType(const EPrintType PrintType)
	{
		switch (PrintType)
		{
			case EPrintType::Status:          return TEXT("status");
			case EPrintType::StatusImportant: return TEXT("status");
			case EPrintType::Warning:         return TEXT("warning");
			case EPrintType::Error:           return TEXT("error");
			default: check(false);            return TEXT("error");
		}
	}

	struct FLogging
	{
	public:
		template<typename FmtType, typename... Types>
		void PrintLog(const EPrintType LogType, const FmtType& MessageFmt, Types ... MessageArgs) const
		{
			const FString Message = FString::Printf(MessageFmt, MessageArgs ...);
			const auto StringToPrint = FString::Printf(TEXT("%s %s: %s"),
				*GetLogPrefix(), FDLCPackageManager_Debug::GetLogPerfixForLogType(LogType), *Message);

			switch (LogType)
			{
				case EPrintType::Status:          { UE_LOG(LogDLCLoading, VeryVerbose, TEXT("%s"), *StringToPrint);  break; };
				case EPrintType::StatusImportant: { UE_LOG(LogDLCLoading, Verbose, TEXT("%s"), *StringToPrint);      break; };
				case EPrintType::Warning:         { UE_LOG(LogDLCLoading, Warning, TEXT("%s"), *StringToPrint);      break; };
				case EPrintType::Error:           { UE_LOG(LogDLCLoading, Error, TEXT("%s"), *StringToPrint);        break; };
				default: check(false);            { };
			}
		}

		virtual ~FLogging() { }

	protected:
		virtual FString GetLogPrefix() const = 0;
	};

	// - - -

	struct FLogging_Initialization : public FLogging
	{
	protected:
		FString GetLogPrefix() const override { return TEXT("Initialization"); }
	};

	// - - -

	struct FLogging_Loading : public FLogging
	{
	public:
		FLogging_Loading(const FSoftObjectPtr& SoftObjectPtr)
			: Prefix(FString::Printf(TEXT("Loading of object [%s]"), *SoftObjectPtr.ToString())) { }

	protected:
		FString GetLogPrefix() const override { return Prefix; }

	private:
		const FString Prefix;
	};

	// - - -

	struct FLogging_DLCChunkDownloading : public FLogging
	{
	public:
		FLogging_DLCChunkDownloading(const FString& DLCChunkID)
			: Prefix(FString::Printf(TEXT("Downloading DLC chunk [%s]"), *DLCChunkID)) { }

	protected:
		FString GetLogPrefix() const override { return Prefix; }

	private:
		const FString Prefix;
	};
};
