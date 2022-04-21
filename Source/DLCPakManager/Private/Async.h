#pragma once

#include "Async/Future.h"

namespace DLCPackageManagerPrivate
{
	template<typename TValue>
	TFuture<typename TRemoveReference<TValue>::Type> FilledFuture(TValue&& Value);

	template<typename TValue>
	TFuture<typename TRemoveReference<TValue>::Type> FilledFuture(const TValue& Value);

	inline TFuture<void> FilledFuture();

	// - - - - - - - - - - - - -
	
	template<typename ResultType>
	void CancelPromiseWorkaround(TPromise<ResultType>& Promise);

	//TODO: Find how to move implementation to ".inl" file
	template<>
	inline void CancelPromiseWorkaround<void>(TPromise<void>& Promise)
	{
		using FPromiseBase = TPromiseBase<int>;
		using FPromiseBaseStateType = TSharedPtr<TFutureState<int>, ESPMode::ThreadSafe>;
		FPromiseBase& PromiseBase = static_cast<FPromiseBase&>(Promise);
		FPromiseBaseStateType& PromiseBaseState = reinterpret_cast<FPromiseBaseStateType&>(PromiseBase);
		PromiseBaseState.Reset();
	}
	
	// - - - - - - - - - - - - -
	
	template<typename T>
	class TPromiseWithWorkaroundedCanceling : public TPromise<T>
	{
	public:
		using TPromise<T>::TPromise;
		TPromiseWithWorkaroundedCanceling(const TPromiseWithWorkaroundedCanceling&) = delete;
		TPromiseWithWorkaroundedCanceling() = default;

		~TPromiseWithWorkaroundedCanceling();
	};

	//------------------------------------- TMultiPromise -----------------------------------------------
	
	template<typename T>
	class TMultiPromise
	{
	public:
		void SetValue(const T& InValue);
		void EmplaceValue(T&& InValue);

		TFuture<TSharedPtr<T>> MakeFuture();

		bool IsSet() const;

		void Reset();

		~TMultiPromise();

	private:
		void Notify_ValueSet();
		void CancelPromises();

		TSharedPtr<T> Value;
		TArray<TPromise<TSharedPtr<T>>> WaitingPromises;
	};

	// - - - - TMultiPromise<void> - - - -

	template<>
	class TMultiPromise<void>
	{
	public:
		void SetValue();

		TFuture<void> MakeFuture();

		bool IsSet() const;

		~TMultiPromise();

	private:
		void Notify_ValueSet();

		bool bIsSet = false;
		TArray<TPromise<void>> WaitingPromises;
	};
}

#include "Async.inl"
