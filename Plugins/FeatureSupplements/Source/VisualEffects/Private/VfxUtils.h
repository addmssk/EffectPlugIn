
#pragma once
#include "CoreMinimal.h"
#include "Containers/AllocatorFixedSizeFreeList.h"

/**************************************************************************************************
*
*    Logs
*
***/

DECLARE_LOG_CATEGORY_EXTERN(LogVfxGeneral, Log, All);


/**************************************************************************************************
*
*    FVfxUniqueNameGenerator
*
***/

class FVfxUniqueNameGenerator
{
public:
	explicit FVfxUniqueNameGenerator (const TCHAR* InBaseName, const int32 BaseCount = 0)
	{
		BaseName = InBaseName;
		Count = BaseCount;
	}
	explicit FVfxUniqueNameGenerator (const FName& InBaseName, const int32 BaseCount = 0)
	{
		BaseName = InBaseName;
		Count = BaseCount;
	}

	inline FName GetUniqueName ()
	{
		return FName(BaseName, ++Count);
	}

private:
	FName BaseName;
	int32 Count;
};


/**************************************************************************************************
*
*   TVfxCommonStructPool
*
***/

template <typename T, uint32 TYPE_SIZE, uint32 CHUNK_COUNT>
struct TVfxCommonStructPool
{
	TVfxCommonStructPool() = delete;
	TVfxCommonStructPool (const TCHAR* InTypeName)
		: FreeList(CHUNK_COUNT)
		, AllocedCount(0)
		, cTypeSize(sizeof(T))
		, cTypeSizePadded(TYPE_SIZE)
		, cTypeName(InTypeName)
	{
		static_assert(sizeof(T) <= TYPE_SIZE, "");
		static_assert((TYPE_SIZE & 0xf) == 0, "");
	}
	virtual ~TVfxCommonStructPool ()
	{
		check(AllocedCount == 0);
	}

	TAllocatorFixedSizeFreeList<TYPE_SIZE, CHUNK_COUNT> FreeList;
	uint32 AllocedCount;

	const uint32 cTypeSize;
	const uint32 cTypeSizePadded;
	const TCHAR* cTypeName;

	inline void GenerateReallocWarning () const
	{
#if !UE_BUILD_SHIPPING
		const uint32 CurrentCapacity = FreeList.GetAllocatedSize() / TYPE_SIZE;
		if (AllocedCount > CurrentCapacity)
		{
			UE_LOG(LogVfxGeneral, Error, TEXT("========================================================================="));
			UE_LOG(LogVfxGeneral, Error, TEXT("========================================================================="));
			UE_LOG(LogVfxGeneral, Error, TEXT("Increase TVfxCommonStructPool budget (Type %s)"), cTypeName);
			UE_LOG(LogVfxGeneral, Error, TEXT("Budget %d, Current requested %d"), CHUNK_COUNT, AllocedCount);
			UE_LOG(LogVfxGeneral, Error, TEXT("========================================================================="));
			UE_LOG(LogVfxGeneral, Error, TEXT("========================================================================="));
		}
#endif
	}

	
	void* AllocRaw ()
	{
		++AllocedCount;
		GenerateReallocWarning();

		return FreeList.Allocate();
	}
	
	T* New ()
	{
		++AllocedCount;
		GenerateReallocWarning();

		T* NewTypePtr = new(FreeList.Allocate()) T();
		return NewTypePtr;
	}

	template<typename P0>
	T* New (
		P0 Param0
	)
	{
		++AllocedCount;
		GenerateReallocWarning();

		T* NewTypePtr = new(FreeList.Allocate()) T(Param0);
		return NewTypePtr;
	}

	template<typename P0, typename P1>
	T* New (
		P0 Param0,
		P1 Param1
	)
	{
		++AllocedCount;
		GenerateReallocWarning();

		T* NewTypePtr = new(FreeList.Allocate()) T(Param0, Param1);
		return NewTypePtr;
	}

	template<typename P0, typename P1, typename P2>
	T* New (
		P0 Param0,
		P1 Param1,
		P2 Param2
	)
	{
		++AllocedCount;
		GenerateReallocWarning();

		T* NewTypePtr = new(FreeList.Allocate()) T(Param0, Param1, Param2);
		return NewTypePtr;
	}

	inline void Delete (T* InPtr)
	{
		--AllocedCount;
		check(AllocedCount >= 0);
		InPtr->~T();
		FreeList.Free(InPtr);
	}

	void FreeRaw (void* InPtr)
	{
		--AllocedCount;
		check(AllocedCount >= 0);
		FreeList.Free(InPtr);
	}

	bool Exit ()
	{
		if (AllocedCount != 0)
		{
			UE_LOG(LogVfxGeneral, Error, TEXT("========================================================================="));
			UE_LOG(LogVfxGeneral, Error, TEXT("========================================================================="));
			UE_LOG(LogVfxGeneral, Error, TEXT("TVfxCommonStructPool(%s) is not fully cleared. "), cTypeName);
			UE_LOG(LogVfxGeneral, Error, TEXT("========================================================================="));
			UE_LOG(LogVfxGeneral, Error, TEXT("========================================================================="));
			return false;
		}
		return true;
	}
};

#define DEF_TYPE_SIZE(STRUCT) STRUCT, (sizeof(STRUCT) + (0x10 - (sizeof(STRUCT) & 0xf)))