// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>

int32_t LegacyIOS_SetErrno(int32_t value)
{
    errno = value;
    return -1;
}

double LegacyIOS_OddDouble(int32_t a, double b, int32_t c)
{
    return a * 100 + b * 10 + c;
}

double LegacyIOS_SplitDouble(int32_t a, int32_t b, int32_t c, double d)
{
    return a + b + c + d;
}

int64_t LegacyIOS_OddInt64(int32_t a, int64_t b, int32_t c)
{
    return a + b + c;
}

int64_t LegacyIOS_SplitInt64(int32_t a, int32_t b, int32_t c, int64_t d)
{
    return a + b + c + d;
}

double LegacyIOS_InvokeOddDouble(double (*callback)(int32_t, double, int32_t))
{
    return callback(1, 2.5, 7);
}

typedef double (*MixedCallback)(int64_t, double, double, double, double, double, double, double, double);

struct CallbackState
{
    MixedCallback Callback;
    double Result;
};

static void* RunCallback(void* argument)
{
    struct CallbackState* state = argument;
    state->Result = state->Callback(37, 1, 2, 3, 4, 5, 6, 7, 8);
    return NULL;
}

double LegacyIOS_InvokeCallback(MixedCallback callback, int32_t foreignThread)
{
    struct CallbackState state = {callback, 0};
    if (foreignThread)
    {
        pthread_t thread;
        if (pthread_create(&thread, NULL, RunCallback, &state) != 0)
            return -1;
        if (pthread_join(thread, NULL) != 0)
            return -2;
    }
    else
    {
        RunCallback(&state);
    }
    return state.Result;
}


typedef struct { uint8_t Value; } Small;
typedef struct { int32_t X, Y; } Pair;
typedef struct { int32_t X, Y, Z; } Triple;
typedef struct { float X, Y, Z, W; } Float4;
typedef struct { double X, Y; } Double2;
typedef struct { int32_t Tag; double Value; int16_t Tail; } Mixed;
typedef struct __attribute__((packed)) { uint8_t Tag; double Value; int16_t Tail; } Packed;
typedef struct { Pair Pair; Double2 Doubles; int32_t Tail; } Nested;

int LegacyIOS_Layout(int selector)
{
    const int sizes[] = {sizeof(Small), sizeof(Pair), sizeof(Triple), sizeof(Float4), sizeof(Double2),
        sizeof(Mixed), offsetof(Mixed, Value), offsetof(Mixed, Tail), sizeof(Packed), offsetof(Packed, Value), sizeof(Nested)};
    return selector >= 0 && selector < (int)(sizeof(sizes) / sizeof(sizes[0])) ? sizes[selector] : -1;
}

Small LegacyIOS_Small(Small value)
{
    value.Value ^= 0x5A;
    return value;
}

Pair LegacyIOS_Pair(int prefix, Pair value)
{
    value.X += prefix;
    value.Y -= prefix;
    return value;
}

Triple LegacyIOS_Triple(Triple value)
{
    Triple result = {value.Z, value.X, value.Y};
    return result;
}

int LegacyIOS_SplitTriple(int a, int b, int c, Triple value)
{
    return a + b + c + value.X + 2 * value.Y + 3 * value.Z;
}

Float4 LegacyIOS_Float4(int prefix, Float4 value, double scale)
{
    value.X = value.X * scale + prefix;
    value.Y = value.Y * scale - prefix;
    value.Z *= scale;
    value.W *= scale;
    return value;
}

Double2 LegacyIOS_Double2(int prefix, Double2 value, float scale)
{
    value.X = value.X * scale + prefix;
    value.Y = value.Y * scale - prefix;
    return value;
}

Mixed LegacyIOS_Mixed(Mixed value)
{
    value.Tag++;
    value.Value *= 2;
    value.Tail--;
    return value;
}

Packed LegacyIOS_Packed(int prefix, Packed value)
{
    value.Tag++;
    value.Value += prefix;
    value.Tail -= prefix;
    return value;
}

Nested LegacyIOS_Nested(Nested value)
{
    Nested result = {{value.Pair.Y, value.Pair.X}, {value.Doubles.Y, value.Doubles.X}, value.Tail + 1};
    return result;
}

void LegacyIOS_ByRef(Mixed* value, Double2* result)
{
    result->X = value->Value;
    *value = LegacyIOS_Mixed(*value);
    result->Y = value->Value;
}

double LegacyIOS_Floats(float a, double b, float c, double d, float e, double f,
    float g, double h, float i, double j, float k, double l)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j + 11 * k + 12 * l;
}

float LegacyIOS_FloatIdentity(float value) { return value; }
double LegacyIOS_DoubleIdentity(double value) { return value; }

typedef Mixed (*StructCallback)(int, Float4, Double2, Packed, int64_t);
struct StructCallbackState { StructCallback Callback; int Result; };

static void* RunStructCallback(void* argument)
{
    struct StructCallbackState* state = argument;
    Float4 floats = {1.25f, -2.5f, 3.75f, 4.5f};
    Double2 doubles = {8.25, -9.5};
    Packed packed = {0xAB, 12.5, -1234};
    Mixed result = state->Callback(17, floats, doubles, packed, INT64_C(0x123456789ABCDEF));
    state->Result = result.Tag == 18 && result.Value == 84.5 && result.Tail == 122;
    return NULL;
}

int LegacyIOS_InvokeStructCallback(StructCallback callback, int foreignThread)
{
    struct StructCallbackState state = {callback, 0};
    if (foreignThread)
    {
        pthread_t thread;
        if (pthread_create(&thread, NULL, RunStructCallback, &state) != 0)
            return -1;
        if (pthread_join(thread, NULL) != 0)
            return -2;
    }
    else
    {
        RunStructCallback(&state);
    }
    return state.Result;
}
