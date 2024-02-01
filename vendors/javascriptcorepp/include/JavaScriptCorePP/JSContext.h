#pragma once
#include <string>
#include <functional>
#include <vector>

#include "JSC_Pointers.h"

namespace JavaScriptCorePP
{
	class JSContext;
	class JSValue;
	typedef std::function<void(const JSContext&, const std::vector<JSValue>&, JSValue&, JSValue&)> JSCallback;

	class JSObject;
	class JSFunction;
	class JSString;
	class JSPromise;

	class JSContext
	{
	public:

		JSContext();
		JSContext(JSContextRef contextRef);
		JSContext(const JSContext& copy);
		JSContext(JSContext&& move) noexcept;
		~JSContext();

		JSGlobalContextRef _context = NULL;

		JSObject GetGlobalObject() const;

		JSValue CreateUndefined() const;
		JSValue CreateNull() const;
		JSString CreateString(const std::string& str) const;
		JSString CreateString(const std::wstring& str) const;
		JSValue CreateNumber(double num) const;
		JSValue CreateBoolean(bool val) const;
		JSObject CreateArray() const;
		JSObject CreateObject() const;
		JSFunction CreateFunction(const JSCallback& callback) const;
		JSObject CreateError(const std::string& errorMessage) const;
		JSPromise CreatePromise() const;
		
		JSValue FromJSON(const std::string& json_str) const;

		JSContextRef GetContextRef() const;

		bool Valid() const;

		JSContext& operator=(const JSContext& other);
		JSContext& operator=(JSContext&& other);
		JSContextRef operator*() const;

		bool operator==(const JSContext& rhs);
	};
}

