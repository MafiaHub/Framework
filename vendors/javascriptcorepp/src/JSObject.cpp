#include "JavaScriptCorePP/JSObject.h"
#include "JavaScriptCorePP/JSValue.h"
#include "JavaScriptCorePP/JSFunction.h"
#include "JavaScriptCorePP/JSContext.h"
#include "JavaScriptCorePP/JSString.h"

#include <JavaScriptCore/JSContextRef.h>
#include <JavaScriptCore/JSStringRef.h>
#include <JavaScriptCore/JSObjectRef.h>

#include <JavaScriptCorePP/JSSafeExit.h>

#include <stdexcept>
#include <cassert>

namespace JavaScriptCorePP
{
	JSObject::JSObject() :
		_value(NULL),  _context(NULL) {}

	JSObject::JSObject(const JSContext& context) :
		_context(context), _value(JSObjectMake(context.GetContextRef(), NULL, NULL))
	{
		JSValueProtect(_context.GetContextRef(), _value);
	}

	JSObject::JSObject(const JSContext& context, JSValueRef value) :
		_context(context)
	{
		assert(JSValueIsObject(context.GetContextRef(), value));
		_value = JSValueToObject(context.GetContextRef(), value, NULL);
		JSValueProtect(_context.GetContextRef(), _value);
	}

	JSObject::JSObject(const JSContext& context, JSObjectRef value) :
		_context(context), _value(value)
	{
		JSValueProtect(_context.GetContextRef(), _value);
	}

	JSObject::JSObject(const JSValue& value) : _context(value._context)
	{
		assert(value.IsObject());
		_value = JSValueToObject(_context.GetContextRef(), value._value, NULL);
		JSValueProtect(_context.GetContextRef(), _value);
	}

	JSObject::JSObject(const JSObject& copy) : _value(copy._value), _context(copy._context)
	{
		JSValueProtect(_context.GetContextRef(), _value);
	}

	JSObject::JSObject(JSObject&& move) noexcept : _value(move._value), _context(move._context)
	{
		move._value = NULL;
		move._context = NULL;
	}

	JSObject::~JSObject()
	{
		if (_value != NULL && !_js_doSafeExit)
		{
			JSValueUnprotect(_context.GetContextRef(), _value);
		}
	}

	IndexedJSValue<std::string> JSObject::operator[](const std::string& key)
	{
		return IndexedJSValue(*this, key);
	}

	IndexedJSValue<unsigned int> JSObject::operator[](unsigned int key)
	{
		return IndexedJSValue(*this, key);
	}

	JSValue JSObject::GetValue(const std::string& index)
	{
		return JSValue(_context, JSObjectGetProperty(_context.GetContextRef(), _value, JSString(_context, index), NULL));
	}

	const JSValue JSObject::GetValue(const std::string& index) const
	{
		return JSValue(_context, JSObjectGetProperty(_context.GetContextRef(), _value, JSString(_context, index), NULL));
	}

	JSValue JSObject::GetValue(unsigned int index)
	{
		return JSValue::Create(_context, JSObjectGetPropertyAtIndex(_context.GetContextRef(), _value, index, NULL));
	}

	const JSValue JSObject::GetValue(unsigned int index) const
	{
		return JSValue::Create(_context, JSObjectGetPropertyAtIndex(_context.GetContextRef(), _value, index, NULL));
	}

	void JSObject::SetValue(const std::string& index, const JSValue& value)
	{
		JSObjectSetProperty(_context.GetContextRef(), _value, JSString(_context, index), value._value, NULL, NULL);
	}

	void JSObject::SetValue(const std::string& index, const JSFunction& value)
	{
		SetValue(index, value.ToValue());
	}

	void JSObject::SetValue(const std::string& index, const JSObject& value)
	{
		SetValue(index, value.ToValue());
	}

	void JSObject::SetValue(const std::string& index, const JSCallback& callback)
	{
		SetValue(index, JSFunction(_context, callback));
	}

	void JSObject::SetValue(const std::string& index, const JSString& str)
	{
		SetValue(index, str.ToJSValue());
	}

	void JSObject::SetValue(unsigned int index, const JSValue& value)
	{
		JSObjectSetPropertyAtIndex(_context.GetContextRef(), _value, index, value._value, NULL);
	}

	void JSObject::SetValue(unsigned int index, const JSObject& value)
	{
		SetValue(index, value.ToValue());
	}

	void JSObject::SetValue(unsigned int index, const JSFunction& value)
	{
		SetValue(index, value.ToValue());
	}

	void JSObject::SetValue(unsigned int index, const JSCallback& callback)
	{
		SetValue(index, JSFunction(_context, callback));
	}

	void JSObject::SetValue(unsigned int index, const JSString& str)
	{
		SetValue(index, str.ToJSValue());
	}

	bool JSObject::HasValue(const std::string& index) const
	{
		return JSObjectHasProperty(_context.GetContextRef(), _value, JSString(_context, index));
	}

	int JSObject::Length() const
	{
		// TODO: Find if there's a better way to check for array length
		const JSValue length = GetValue("length");

		// If it's an array, it should exist
		return length.IsNumber() ? (int)length.GetNumber() : 0;
	}

	bool JSObject::Valid() const
	{
		return _value != NULL;
	}

	JSObject::operator JSValue()
	{
		return JSValue(_context, (JSValueRef)_value);
	}

	JSObject::operator const JSValue() const
	{
		return JSValue(_context, (JSValueRef)_value);
	}

	JSValue JSObject::ToValue()
	{
		return JSValue(_context, (JSValueRef)_value);
	}

	const JSValue JSObject::ToValue() const
	{
		return JSValue(_context, (JSValueRef)_value);
	}

	JSObject& JSObject::operator=(const JSObject& other)
	{
		if (_value != NULL)
		{
			JSValueUnprotect(_context.GetContextRef(), _value);
		}

		_context = other._context;
		_value = other._value;

		if (_value != NULL)
		{
			JSValueProtect(_context.GetContextRef(), _value);
		}

		return *this;
	}

	JSObject& JSObject::operator=(JSObject&& other)
	{
		if (_value != NULL)
		{
			JSValueUnprotect(_context.GetContextRef(), _value);
		}

		this->_context = other._context;
		this->_value = other._value;

		other._context = NULL;
		other._value = NULL;

		return *this;
	}

	JSValueRef JSObject::operator*() const
	{
		return _value;
	}

	bool JSObject::operator ==(const JSObject& rhs) const
	{
		return _value == rhs._value;
	}
}