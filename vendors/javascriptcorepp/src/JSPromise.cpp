#include "JavaScriptCorePP/JSPromise.h"

namespace JavaScriptCorePP
{
	JSPromise::JSPromise(const JSContext& context, JSObjectRef promiseObj, JSObjectRef resolveFunc, JSObjectRef rejectFunc) :
		_context(context), _promise(context, promiseObj), _resolve(context, resolveFunc), _reject(context, rejectFunc) {}

	JSPromise::JSPromise(const JSContext& context, const JSObject& promiseObj, const JSFunction& resolveFunc, const JSFunction& rejectFunc) :
		_context(context), _promise(promiseObj), _resolve(resolveFunc), _reject(rejectFunc) {}

	JSValue JSPromise::Resolve() const
	{
		return _resolve.Call();
	}

	JSValue JSPromise::Reject() const
	{
		return _reject.Call();
	}

	JSValue JSPromise::Resolve(const std::vector<JSValue>& args) const
	{
		return _resolve.Call(args);
	}

	JSValue JSPromise::Reject(const std::vector<JSValue>& args) const
	{
		return _reject.Call(args);
	}

	JSObject JSPromise::GetPromise() const
	{
		return _promise;
	}

	JSFunction JSPromise::GetResolve() const
	{
		return _resolve;
	}

	JSFunction JSPromise::GetReject() const
	{
		return _reject;
	}
}