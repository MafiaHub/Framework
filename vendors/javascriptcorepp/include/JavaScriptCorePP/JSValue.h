#pragma once

#include "JSType.h"
#include "JSContext.h"
#include <string>

namespace JavaScriptCorePP
{
	class JSObject;
	class JSFunction;

	template<typename T>
	class IndexedJSValue;

	class JSValue
	{
	public:

		JSValue(const JSContext& context, JSValueRef value);
		JSValue(const JSValue& copy);
		JSValue(JSValue&& move) noexcept;
		~JSValue();

		virtual JSType GetJSType() const;

		bool IsUndefined() const;
		bool IsNull() const;
		bool IsBoolean() const;
		bool IsNumber() const;
		bool IsString() const;
		bool IsObject() const;
		bool IsFunction() const;

		std::string ToJSON() const;

		bool GetBoolean() const;
		double GetNumber() const;
		std::string GetString() const;
		std::wstring GetWString() const;

		JSObject GetJSObject();
		const JSObject GetJSObject() const;

		JSFunction GetFunction();
		const JSFunction GetFunction() const;

		bool Valid() const;

		static JSValue Create(JSContext context, JSValueRef value);

		virtual IndexedJSValue<std::string> operator[](const std::string& key);
		virtual IndexedJSValue<unsigned int> operator[](unsigned int key);

		JSValue& operator=(const JSValue& other);
		JSValue& operator=(JSValue&& other);

		bool operator==(const JSValue& other);

		JSValueRef operator*() const;

	protected:
		JSValueRef _value = NULL;
		JSType _type = JSType::Undefined;
		JSContext _context = NULL;

		JSValue();

		friend class JSObject;
		friend class JSFunction;
	};
}