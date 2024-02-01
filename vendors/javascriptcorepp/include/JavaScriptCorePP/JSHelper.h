#pragma once

#include "JSContext.h"
#include "JSType.h"
#include "JSValue.h"
#include "JSObject.h"
#include "JSFunction.h"
#include "JSString.h"
#include "JSPromise.h"
#include <string>

namespace JavaScriptCorePP
{
	JSValue JSEval(const JSContext& context, const std::string& js_eval);
}