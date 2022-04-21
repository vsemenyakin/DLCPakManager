
namespace DLCPackageManagerPrivate
{
	template<typename TValue>
	TFuture<typename TRemoveReference<TValue>::Type> FilledFuture(TValue&& Value)
	{
		TPromise<typename TRemoveReference<TValue>::Type> DummyPromise;
		DummyPromise.EmplaceValue(Value);
		return DummyPromise.GetFuture();
	}

	template<typename TValue>
	TFuture<typename TRemoveReference<TValue>::Type> FilledFuture(const TValue& Value)
	{
		TPromise<typename TRemoveReference<TValue>::Type> DummyPromise;
		DummyPromise.SetValue(Value);
		return DummyPromise.GetFuture();
	}

	inline TFuture<void> FilledFuture()
	{
		TPromise<void> DummyPromise;
		DummyPromise.EmplaceValue();
		return DummyPromise.GetFuture();
	}

	template<typename ResultType>
	void CancelPromiseWorkaround(TPromise<ResultType>& Promise)
	{
		using FPromiseBase = TPromiseBase<ResultType>;
		using FPromiseBaseStateType = TSharedPtr<TFutureState<ResultType>, ESPMode::ThreadSafe>;
		FPromiseBase& PromiseBase = static_cast<FPromiseBase&>(Promise);
		FPromiseBaseStateType& PromiseBaseState = reinterpret_cast<FPromiseBaseStateType&>(PromiseBase);
		PromiseBaseState.Reset();
	}

	// - - - - - -

	template<typename T>
	TPromiseWithWorkaroundedCanceling<T>::~TPromiseWithWorkaroundedCanceling()
	{
		CancelPromiseWorkaround(*this);
	}
	
	// - - - - TMultiPromise<Type> - - - -

	template<typename T>
	void TMultiPromise<T>::SetValue(const T& InValue)
	{
		checkf(!Value.IsValid(), TEXT("Cannot setup promise twice"));
		Value = MakeShared<T>(InValue);
		Notify_ValueSet();
	}

	template<typename T>
	void TMultiPromise<T>::EmplaceValue(T&& InValue)
	{
		checkf(!Value.IsValid(), TEXT("Cannot setup promise twice"));
		Value = MakeShared<T>(MoveTemp(InValue));
		Notify_ValueSet();
	}

	template<typename T>
	TFuture<TSharedPtr<T>> TMultiPromise<T>::MakeFuture()
	{
		return Value.IsValid() ?
			FilledFuture<TSharedPtr<T>>(Value) :
			WaitingPromises[WaitingPromises.Emplace()].GetFuture();
	}

	template<typename T>
	bool TMultiPromise<T>::IsSet() const
	{
		return Value.IsValid();
	}

	template<typename T>
	void TMultiPromise<T>::Reset()
	{
		Value.Reset();
		CancelPromises();
	}

	template<typename T>
	TMultiPromise<T>::~TMultiPromise()
	{
		CancelPromises();
	}

	template<typename T>
	void TMultiPromise<T>::Notify_ValueSet()
	{
		for (TPromise<TSharedPtr<T>>& WaitingPromise : WaitingPromises)
			WaitingPromise.SetValue(Value);

		WaitingPromises.Empty();
	}

	template<typename T>
	void TMultiPromise<T>::CancelPromises()
	{
		for (TPromise<TSharedPtr<T>>& WaitingPromise : WaitingPromises)
			CancelPromiseWorkaround(WaitingPromise);
	}

	// - - - - TMultiPromise<void> - - - -

	void TMultiPromise<void>::SetValue()
	{
		checkf(!bIsSet, TEXT("Cannot setup promise twice"));
		bIsSet = true;
		Notify_ValueSet();
	}

	TFuture<void> TMultiPromise<void>::MakeFuture()
	{
		return bIsSet ? FilledFuture() : WaitingPromises[WaitingPromises.Emplace()].GetFuture();
	}

	bool TMultiPromise<void>::IsSet() const
	{
		return bIsSet;
	}

	TMultiPromise<void>::~TMultiPromise()
	{
		for (TPromise<void>& WaitingPromise : WaitingPromises)
			CancelPromiseWorkaround(WaitingPromise);
	}

	void TMultiPromise<void>::Notify_ValueSet()
	{
		for (TPromise<void>& WaitingPromise : WaitingPromises)
			WaitingPromise.SetValue();

		WaitingPromises.Empty();
	}
}