#include "JSContext.h"
#include "JSObject.h"
#include "JSFunction.h"

namespace JavaScriptCorePP
{
	class JSValue;

	class JSPromise
	{
	public:
		JSPromise(const JSContext& context, JSObjectRef promiseObj, JSObjectRef resolveFunc, JSObjectRef rejectFunc);
		JSPromise(const JSContext& context, const JSObject& promiseObj, const JSFunction& resolveFunc, const JSFunction& rejectFunc);

		JSValue Resolve() const;
		JSValue Reject() const;

		JSValue Resolve(const std::vector<JSValue>& args) const;
		JSValue Reject(const std::vector<JSValue>& args) const;

		JSObject GetPromise() const;
		JSFunction GetResolve() const;
		JSFunction GetReject() const;

	protected:
		JSContext _context = NULL;
		JSObject _promise;
		JSFunction _resolve;
		JSFunction _reject;
	};
}
