#pragma once

namespace JavaScriptCorePP
{
	enum class JSType : unsigned char
	{
		Undefined	= 0, //kJSTypeUndefined,
		Null		= 1, //kJSTypeNull,
		Boolean		= 2, //kJSTypeBoolean,
		Number		= 3, //kJSTypeNumber,
		String		= 4, //kJSTypeString,
		Object		= 5, //kJSTypeObject,
		Function	= 6, //kJSTypeObject + 1
		Invalid		= 7	 //kJSTypeObject + 2
	};
}