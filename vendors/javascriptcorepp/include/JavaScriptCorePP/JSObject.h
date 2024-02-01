#pragma once

#include <string>
#include "JSValue.h"
#include "JSContext.h"
#include "JSFunction.h"
#include "JSString.h"

#include "JSC_Pointers.h"

namespace JavaScriptCorePP
{
	template<typename T>
	class IndexedJSValue;

	class JSObject
	{
	public:
		// Make a new object from context
		JSObject();
		JSObject(const JSContext& context);
		JSObject(const JSContext& context, JSValueRef value);
		JSObject(const JSContext& context, JSObjectRef value);
		JSObject(const JSValue& value);
		JSObject(const JSObject& copy);
		JSObject(JSObject&& move) noexcept;
		~JSObject();

		IndexedJSValue<std::string> operator[](const std::string& key);
		IndexedJSValue<unsigned int> operator[](unsigned int key);

		JSValue GetValue(const std::string& index);
		const JSValue GetValue(const std::string& index) const;
		JSValue GetValue(unsigned int index);
		const JSValue GetValue(unsigned int index) const;

		void SetValue(const std::string& index, const JSValue& value);
		void SetValue(const std::string& index, const JSFunction& value);
		void SetValue(const std::string& index, const JSObject& value);
		void SetValue(const std::string& index, const JSCallback& callback);
		void SetValue(const std::string& index, const JSString& str);
		void SetValue(unsigned int index, const JSValue& value);
		void SetValue(unsigned int index, const JSObject& value);
		void SetValue(unsigned int index, const JSFunction& value);
		void SetValue(unsigned int index, const JSCallback& callback);
		void SetValue(unsigned int index, const JSString& str);

		bool HasValue(const std::string& index) const;

		bool Valid() const;

		int Length() const;

		operator JSValue();
		operator const JSValue() const;

		JSValue ToValue();
		const JSValue ToValue() const;

		JSObject& operator=(const JSObject &other);
		JSObject& operator=(JSObject&& other);

		JSValueRef operator*() const;

		bool operator==(const JSObject& rhs) const;

	protected:
		JSObjectRef _value = NULL;
		JSContext _context = NULL;

		friend class JSFunction;

		template<typename T>
		friend class IndexedJSValue;
	};

	template<typename T>
	class IndexedJSValue : public JSValue
	{
	public:
		JSValue& operator=(const JSValue& other);
		JSValue& operator=(JSValue&& other);

		IndexedJSValue& operator=(const JSFunction& func);
		IndexedJSValue& operator=(const JSObject& val);
		IndexedJSValue& operator=(const JSString& str);
		IndexedJSValue& operator=(const JSCallback& func);

		operator JSValue();

	private:
		IndexedJSValue(const JSObject& obj, const T& index);
		IndexedJSValue(const IndexedJSValue& other);
		IndexedJSValue(IndexedJSValue&& other);

		JSObject obj;
		T index;

		friend JSObject;
	};

	template<typename T>
	inline IndexedJSValue<T>::operator JSValue()
	{
		return JSValue(*this);
	}

	template<typename T>
	inline IndexedJSValue<T>::IndexedJSValue(const JSObject& obj, const T& index) : obj(obj), index(index), JSValue(obj.GetValue(index)) {}

	template<typename T>
	inline JSValue& IndexedJSValue<T>::operator=(const JSValue& other)
	{
		JSValue::operator=(other);
		obj.SetValue(index, *this);
		return *this;
	}

	template<typename T>
	inline JSValue& IndexedJSValue<T>::operator=(JSValue&& other)
	{
		JSValue::operator=(other);
		obj.SetValue(index, *this);
		return *this;
	}

	template<typename T>
	inline IndexedJSValue<T>& IndexedJSValue<T>::operator=(const JSFunction& func)
	{
		JSValue::operator=(func);
		obj.SetValue(index, *this);
		return *this;
	}

	template<typename T>
	inline IndexedJSValue<T>& IndexedJSValue<T>::operator=(const JSObject& val)
	{
		JSValue::operator=(val);
		obj.SetValue(index, *this);
		return *this;
	}

	template<typename T>
	inline IndexedJSValue<T>& IndexedJSValue<T>::operator=(const JSString& str)
	{
		JSValue::operator=(str.ToJSValue());
		obj.SetValue(index, *this);
		return *this;
	}

	template<typename T>
	inline IndexedJSValue<T>& IndexedJSValue<T>::operator=(const JSCallback& func)
	{
		JSValue::operator=(JSFunction(obj._context, func));
		obj.SetValue(index, *this);
		return *this;
	}
}
