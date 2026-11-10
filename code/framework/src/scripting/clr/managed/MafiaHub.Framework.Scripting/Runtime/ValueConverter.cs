/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Threading.Tasks;

namespace MafiaHub.Framework.Scripting.Runtime
{
    /// <summary>Turns values that arrived from another resource into the types a handler or an export declares.</summary>
    internal static class ValueConverter
    {
        public static object? To(object? value, Type target, string where)
        {
            if (target == typeof(object))
            {
                return value;
            }
            Type? underlying = Nullable.GetUnderlyingType(target);
            if (value is null)
            {
                if (!target.IsValueType || underlying != null)
                {
                    return null;
                }
                throw new ArgumentException($"{where}: null cannot become a {target.Name}");
            }
            target = underlying ?? target;
            if (target.IsInstanceOfType(value))
            {
                return value;
            }

            if (value is ScriptFunction function && typeof(Delegate).IsAssignableFrom(target) && target != typeof(Delegate) && target != typeof(MulticastDelegate))
            {
                return ToDelegate(function, target);
            }

            if (value is double number)
            {
                if (target.IsEnum)
                {
                    return Enum.ToObject(target, (long)number);
                }
                if (target.IsPrimitive || target == typeof(decimal))
                {
                    return Convert.ChangeType(number, target, CultureInfo.InvariantCulture);
                }
            }

            if (value is object?[] items)
            {
                if (target.IsArray)
                {
                    Type element = target.GetElementType()!;
                    Array array = Array.CreateInstance(element, items.Length);
                    for (int i = 0; i < items.Length; ++i)
                    {
                        array.SetValue(To(items[i], element, $"{where}[{i}]"), i);
                    }
                    return array;
                }
                if (target.IsGenericType && IsListLike(target.GetGenericTypeDefinition()))
                {
                    Type element = target.GetGenericArguments()[0];
                    var list = (IList)Activator.CreateInstance(typeof(List<>).MakeGenericType(element))!;
                    for (int i = 0; i < items.Length; ++i)
                    {
                        list.Add(To(items[i], element, $"{where}[{i}]"));
                    }
                    return list;
                }
            }

            if (value is Dictionary<string, object?> properties && target.IsGenericType && IsDictionaryLike(target.GetGenericTypeDefinition()) && target.GetGenericArguments()[0] == typeof(string))
            {
                Type element = target.GetGenericArguments()[1];
                var dictionary = (IDictionary)Activator.CreateInstance(typeof(Dictionary<,>).MakeGenericType(typeof(string), element))!;
                foreach (var (key, item) in properties)
                {
                    dictionary[key] = To(item, element, $"{where}.{key}");
                }
                return dictionary;
            }

            throw new ArgumentException($"{where}: a {Describe(value)} cannot become a {target.Name}");
        }

        /// <summary>The handler's parameters filled from the arguments, positionally; missing ones take their default.</summary>
        public static object?[] Arguments(MethodInfo method, object?[] args, string where)
        {
            ParameterInfo[] parameters = method.GetParameters();
            if (parameters.Length == 1 && parameters[0].ParameterType == typeof(object?[]))
            {
                return new object?[] { args };
            }
            var result = new object?[parameters.Length];
            for (int i = 0; i < parameters.Length; ++i)
            {
                ParameterInfo parameter = parameters[i];
                if (i < args.Length)
                {
                    result[i] = To(args[i], parameter.ParameterType, $"{where}: parameter '{parameter.Name}'");
                }
                else if (parameter.HasDefaultValue)
                {
                    result[i] = parameter.DefaultValue;
                }
                else
                {
                    result[i] = parameter.ParameterType.IsValueType ? Activator.CreateInstance(parameter.ParameterType) : null;
                }
            }
            return result;
        }

        /// <summary>What a completed task produced, or the value itself; an unfinished task cannot be answered now.</summary>
        public static object? Settled(object? result, string where)
        {
            if (result is not Task task)
            {
                return result;
            }
            if (!task.IsCompleted)
            {
                throw new InvalidOperationException($"{where} returned a task that has not completed; exports called from another resource must answer synchronously");
            }
            task.GetAwaiter().GetResult();
            Type type = task.GetType();
            if (type.IsGenericType)
            {
                object? value = type.GetProperty("Result")!.GetValue(task);
                // Task<VoidTaskResult> backs a plain async Task.
                return value?.GetType().Name == "VoidTaskResult" ? null : value;
            }
            return null;
        }

        // A delegate of the target's shape that calls the JavaScript function: its result converted to the return type,
        // a Task or Task<T> return awaiting the promise.
        private static Delegate ToDelegate(ScriptFunction function, Type delegateType)
        {
            MethodInfo invoke = delegateType.GetMethod("Invoke")!;
            ParameterExpression[] parameters = invoke.GetParameters().Select(p => Expression.Parameter(p.ParameterType, p.Name)).ToArray();
            NewArrayExpression args = Expression.NewArrayInit(typeof(object), parameters.Select(p => Expression.Convert(p, typeof(object))));
            ConstantExpression callee = Expression.Constant(function);
            Type returnType = invoke.ReturnType;

            Expression body;
            if (returnType == typeof(void))
            {
                body = Expression.Block(typeof(void), Expression.Call(callee, nameof(ScriptFunction.Invoke), null, args));
            }
            else if (returnType == typeof(Task))
            {
                body = Expression.Convert(Expression.Call(callee, nameof(ScriptFunction.InvokeAsync), null, args), typeof(Task));
            }
            else if (returnType.IsGenericType && returnType.GetGenericTypeDefinition() == typeof(Task<>))
            {
                MethodInfo convert = typeof(ValueConverter).GetMethod(nameof(ConvertTask), BindingFlags.NonPublic | BindingFlags.Static)!.MakeGenericMethod(returnType.GetGenericArguments()[0]);
                body = Expression.Call(convert, Expression.Call(callee, nameof(ScriptFunction.InvokeAsync), null, args));
            }
            else
            {
                MethodInfo convert = typeof(ValueConverter).GetMethod(nameof(ConvertResult), BindingFlags.NonPublic | BindingFlags.Static)!.MakeGenericMethod(returnType);
                body = Expression.Call(convert, Expression.Call(callee, nameof(ScriptFunction.Invoke), null, args));
            }
            return Expression.Lambda(delegateType, body, parameters).Compile();
        }

        private static T ConvertResult<T>(object? value) => (T)To(value, typeof(T), "Function result")!;

        private static async Task<T> ConvertTask<T>(Task<object?> task) => (T)To(await task, typeof(T), "Function result")!;

        private static bool IsListLike(Type definition) => definition == typeof(List<>) || definition == typeof(IList<>) || definition == typeof(IEnumerable<>) || definition == typeof(IReadOnlyList<>) || definition == typeof(ICollection<>) || definition == typeof(IReadOnlyCollection<>);

        private static bool IsDictionaryLike(Type definition) => definition == typeof(Dictionary<,>) || definition == typeof(IDictionary<,>) || definition == typeof(IReadOnlyDictionary<,>);

        private static string Describe(object value) => value switch
        {
            double => "number",
            string => "string",
            bool => "boolean",
            object?[] => "list",
            Dictionary<string, object?> => "object",
            _ => value.GetType().Name,
        };
    }
}
