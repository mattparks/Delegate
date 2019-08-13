#pragma once

#include <mutex>
#include <memory>
#include <functional>

namespace delg
{
/// This could be inherited by a functions owner class, or used as a normal value field.
///
/// class test : public observer {
/// 	test(delegate<void(float, int) &del>) {
/// 		del.add(&test::called, this);
/// 	}
/// 
/// 	void called(float f, int i) {
/// 		printf("%f %i\n", f, i);
/// 	}
/// }
/// 
/// class test {
/// 	test(delegate<void(float, int) &del>) {
/// 		del.add(&test::called, our_observer);
/// 	}
/// 
/// 	void called(float f, int i) {
/// 		printf("%f %i\n", f, i);
/// 	}
/// 
/// 	observer our_observer;
/// }
///
class observer
{
public:
	observer() : valid(std::make_shared<bool>(true)) {}
	virtual ~observer() = default;
	std::shared_ptr<bool> valid;
};

template<typename>
class delegate;

// An internal specialized function invoker (with return).
template<typename TReturnType, typename... TArgs>
class invoker
{
public:
	using ReturnType = std::vector<TReturnType>;

	static ReturnType invoke(delegate<TReturnType(TArgs...)> &del, TArgs... params)
	{
		std::lock_guard<std::mutex> lock(del.mutex);
		ReturnType returnValues;

		for (auto it = del.functions.begin(); it != del.functions.end();)
		{
			if (it->is_expired())
			{
				it = del.functions.erase(it);
				continue;
			}

			returnValues.emplace_back((*it->function)(params...));
			++it;
		}

		return returnValues;
	}
};

// An internal specialized function invoker (void).
template<typename... TArgs>
class invoker<void, TArgs...>
{
public:
	using ReturnType = void;

	static void Invoke(delegate<void(TArgs...)> &del, TArgs... params)
	{
		std::lock_guard<std::mutex> lock(del.mutex);

		if (del.functions.empty())
		{
			return;
		}

		for (auto it = del.functions.begin(); it != del.functions.end();)
		{
			if (it->is_expired())
			{
				it = del.functions.erase(it);
				continue;
			}

			it->function(params...);
			++it;
		}
	}
};

// The delegate class that holds functions that are called when invoked.
template<typename TReturnType, typename... TArgs>
class delegate<TReturnType(TArgs...)>
{
public:
	using FunctionType = std::function<TReturnType(TArgs...)>;

	delegate() = default;
	virtual ~delegate() = default;

	template<typename... KArgs>
	void add(FunctionType &&function, KArgs... args)
	{
		std::lock_guard<std::mutex> lock(mutex);
		ObserversType observers;

		if constexpr (sizeof...(args) != 0)
		{
			for (const auto &arg : {args...})
			{
				observers.emplace_back(__as_ptr(arg)->valid);
			}
		}

		functions.emplace_back(FunctionPair{ std::move(function), observers });
	}

	void remove(const FunctionType &function)
	{
		std::lock_guard<std::mutex> lock(mutex);
		functions.erase(std::remove_if(functions.begin(), functions.end(), [function](FunctionPair &f)
		{
			return hash(f.function) == hash(function);
		}), functions.end());
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(mutex);
		functions.clear();
	}

	typename Invoker::ReturnType invoke(TArgs... args) { return Invoker::invoke(*this, args...); }

	delegate &operator+=(FunctionType &&function) { return add(std::move(function)); }
	delegate &operator-=(const FunctionType function) { return remove(function); }
	typename Invoker::ReturnType operator()(TArgs... args) { return Invoker::invoke(*this, args...); }

private:
	friend Invoker;

	using Invoker = delg::invoker<TReturnType, TArgs...>;
	using ObserversType = std::vector<std::weak_ptr<bool>>;

	struct FunctionPair
	{
		FunctionType function;
		ObserversType observers;

		bool is_expired()
		{
			for (const auto &observer : observers)
			{
				if (observer.expired())
				{
					return true;
				}
			}

			return false;
		}
	};

	static constexpr size_t hash(const FunctionType &function)
	{
		return function.target_type().hash_code();
	}

	// TODO C++20: std::to_address
	template<typename T> static T *__as_ptr(T &obj) { return &obj; }
	template<typename T> static T *__as_ptr(T *obj) { return obj; }
	template<typename T> static T *__as_ptr(const std::shared_ptr<T> &obj) { return obj.get(); }
	template<typename T> static T *__as_ptr(const std::unique_ptr<T> &obj) { return obj.get(); }

	std::mutex mutex;
	std::vector<FunctionPair> functions;
};

/// A simple on-value-change delegate implementation. Wraps a type with get and set access operators.
template<typename T>
class delegate_value : public delegate<void(T)>
{
public:
	template<typename... Args>
	delegate_value(Args... args) : value(std::forward<Args>(args)...) {}
	virtual ~delegate_value() = default;

	// TODO: A more advanced equals operator (allow implicit conversions).
	delegate_value &operator=(T value)
	{
		value = value;
		invoke(value);
		return *this;
	}

	// TODO: Implicit conversion from delegate_value to the underlying type.

	const T &get() const { return value; }
	const T &operator*() const { return value; }
	const T *operator->() const { return &value; }

protected:
	T value;
};
}
