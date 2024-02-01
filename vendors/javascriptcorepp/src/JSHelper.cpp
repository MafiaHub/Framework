#include "JavaScriptCorePP/JSHelper.h"
#include <JavaScriptCore/JSContextRef.h>

namespace JavaScriptCorePP
{
	bool _js_doSafeExit;

	JSValue JSEval(const JSContext& context, const std::string& js_eval)
	{
		auto val = JSEvaluateScript(context.GetContextRef(), JSString(context, js_eval).GetJSC(), NULL, NULL, 1, NULL);
		return JSValue(context, val);
	}

	void JSDoSafeExit()
	{ 
		_js_doSafeExit = true;
	}
}