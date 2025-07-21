#pragma once
#include <cstdint>

typedef uint8_t		u8;
typedef uint16_t	u16;
typedef uint32_t	u32;
typedef uint64_t	u64;
typedef size_t      usize;

static_assert(sizeof(u8) == 1,  "u8 type is not 1 byte.");
static_assert(sizeof(u16) == 2, "u16 type is not 2 bytes.");
static_assert(sizeof(u32) == 4, "u32 type is not 4 bytes.");
static_assert(sizeof(u64) == 8, "u64 type is not 8 bytes.");

typedef int8_t		i8;
typedef int16_t		i16;
typedef int32_t		i32;
typedef int64_t		i64;

static_assert(sizeof(i8) == 1,  "i8 type is not 1 byte.");
static_assert(sizeof(i16) == 2, "i16 type is not 2 bytes.");
static_assert(sizeof(i32) == 4, "i32 type is not 4 bytes.");
static_assert(sizeof(i64) == 8, "i64 type is not 8 bytes.");

typedef float		f32;
typedef double		f64;

static_assert(sizeof(f32) == 4, "f32 type is not 4 byte.");
static_assert(sizeof(f64) == 8, "f64 type is not 8 bytes.");