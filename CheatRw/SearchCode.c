#include "SearchCode.h"
#include "Unrevealed.h"
#include <ntimage.h>

// 字节 转 hex
UCHAR CharToHex(char* ch)
{
	unsigned char temps[2] = { 0 };
	for (int i = 0; i < 2; i++)
	{
		if (ch[i] >= '0' && ch[i] <= '9')
		{
			temps[i] = (ch[i] - '0');
		}
		else if (ch[i] >= 'A' && ch[i] <= 'F')
		{
			temps[i] = (ch[i] - 'A') + 0xA;
		}
		else if (ch[i] >= 'a' && ch[i] <= 'f')
		{
			temps[i] = (ch[i] - 'a') + 0xA;
		}
	}
	return ((temps[0] << 4) & 0xf0) | (temps[1] & 0xf);
}

// 字节串转大写
char* CharToUper(char* wstr, BOOLEAN isAllocateMemory)
{
	char* ret = NULL;
	if (isAllocateMemory)
	{
		ULONG len = strlen(wstr) + 2;
		ret = (char*)ExAllocatePool(PagedPool, len);
		if (!ret)
		{
			ret = wstr;
		}
		else
		{
			memset(ret, 0, len);
			memcpy(ret, wstr, len - 2);
		}
	}
	else
	{
		ret = wstr;
	}
	_strupr(ret);
	return ret;
}

// 初始化字节码结构体
void InitFindCodeStruct(PFindCode findCode, char* code, ULONG_PTR offset, ULONG_PTR lastAddrOffset)
{
	char* pTemp = code;
	ULONG_PTR i = 0;

	memset(findCode, 0, sizeof(FindCode));
	findCode->lastAddressOffset = lastAddrOffset;
	findCode->offset = offset;
	for (i = 0; *pTemp != '\0'; i++)
	{
		if (*pTemp == '*' || *pTemp == '?')
		{
			findCode->code[i] = *pTemp;
			pTemp++;
			continue;
		}
		findCode->code[i] = CharToHex(pTemp);
		pTemp += 2;

	}
	findCode->len = i;
}

// -------------------------------------------------------------------------------

//返回值为模块的大小
ULONG_PTR QuerySysModule(char* MoudleName, _Out_opt_ ULONG_PTR* module)
{
	RTL_PROCESS_MODULES info;
	ULONG retPro = NULL;
	ULONG_PTR moduleSize = 0;
	NTSTATUS ststas = ZwQuerySystemInformation(SystemModuleInformation, &info, sizeof(info), &retPro);
	char* moduleUper = CharToUper(MoudleName, TRUE);
	if (ststas == STATUS_INFO_LENGTH_MISMATCH)
	{
		//申请长度
		ULONG len = retPro + sizeof(RTL_PROCESS_MODULES);
		PRTL_PROCESS_MODULES mem = (PRTL_PROCESS_MODULES)ExAllocatePool(PagedPool, len);
		if (!mem)
		{
			return 0;
		}

		memset(mem, 0, len);
		ststas = ZwQuerySystemInformation(SystemModuleInformation, mem, len, &retPro);
		if (!NT_SUCCESS(ststas))
		{
			ExFreePool(moduleUper);
			ExFreePool(mem);
			return 0;
		}

		//开始查询
		if (strstr(MoudleName, "ntkrnlpa.exe") || strstr(MoudleName, "ntoskrnl.exe"))
		{
			PRTL_PROCESS_MODULE_INFORMATION ModuleInfo = &(mem->Modules[0]);
			if (module != 0)
			{
				*module = (ULONG_PTR)ModuleInfo->ImageBase;
			}
			moduleSize = ModuleInfo->ImageSize;
		}
		else
		{
			for (int i = 0; i < mem->NumberOfModules; i++)
			{
				PRTL_PROCESS_MODULE_INFORMATION processModule = &mem->Modules[i];
				CharToUper((char*)processModule->FullPathName, FALSE);
				if (strstr((char*)processModule->FullPathName, moduleUper))
				{
					if (module != 0)
					{
						*module = (ULONG_PTR)processModule->ImageBase;
					}
					moduleSize = processModule->ImageSize;
					break;
				}
			}
		}
		ExFreePool(mem);
	}
	ExFreePool(moduleUper);
	return moduleSize;
}

// 通过特征码找地址
ULONG_PTR FindAddressByCode(ULONG_PTR beginAddr, ULONG_PTR endAddr, PFindCode  findCode, ULONG numbers)
{
	ULONG64 j = 0;
	LARGE_INTEGER rtna = { 0 };

	for (ULONG_PTR i = beginAddr; i <= endAddr; i++)
	{
		if (!MmIsAddressValid((PVOID)i))
		{
			i = i & (~0xfff) + PAGE_SIZE - 1;
			continue;
		}
		for (j = 0; j < numbers; j++)
		{
			FindCode  fc = findCode[j];
			ULONG_PTR tempAddress = i;

			UCHAR* code = (UCHAR*)(tempAddress + fc.offset);
			BOOLEAN isFlags = FALSE;

			for (ULONG_PTR k = 0; k < fc.len; k++)
			{
				if (!MmIsAddressValid((PVOID)(code + k)))
				{
					isFlags = TRUE;
					break;
				}
				if (fc.code[k] == '*' || fc.code[k] == '?') continue;

				if (code[k] != fc.code[k])
				{
					isFlags = TRUE;
					break;
				}
			}
			if (isFlags) break;
		}

		//找到了
		if (j == numbers)
		{
			rtna.QuadPart = i;
			rtna.LowPart += findCode[0].lastAddressOffset;
			break;
		}
	}
	return rtna.QuadPart;
}

// 搜索NT特征码
ULONG_PTR SearchNtCode(char* code, int offset)
{
	FindCode fs[1] = { 0 };
	InitFindCodeStruct(&fs[0], code, 0, offset);

	SIZE_T moduleBase = 0;
	ULONG size = QuerySysModule("ntoskrnl.exe", &moduleBase);
	ULONG_PTR func = FindAddressByCode(moduleBase, size + moduleBase, fs, 1);
	return func;
}

// 搜索特征码
ULONG_PTR SearchCode(char* moduleName, char* segmentName, char* code, int offset)
{
	FindCode fs[1] = { 0 };
	InitFindCodeStruct(&fs[0], code, 0, offset);
	SIZE_T moduleBase = 0;
	ULONG size = QuerySysModule(moduleName, &moduleBase);
	if (!moduleBase) return 0;

	PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)moduleBase;
	PIMAGE_NT_HEADERS pNts = (PIMAGE_NT_HEADERS)((PUCHAR)moduleBase + pDos->e_lfanew);
	PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNts);
	PIMAGE_SECTION_HEADER pTemp = NULL;
	for (int i = 0; i < pNts->FileHeader.NumberOfSections; i++)
	{
		char bufName[9] = { 0 };
		memcpy(bufName, pSection->Name, 8);
		if (_stricmp(bufName, segmentName) == 0)
		{
			pTemp = pSection;
			break;
		}
		pSection++;
	}

	if (pTemp)
	{
		moduleBase = pSection->VirtualAddress + moduleBase;
		size = pSection->SizeOfRawData;
	}

	PVOID mem = ExAllocatePool(NonPagedPool, size);
	SIZE_T retSize = 0;
	MmCopyVirtualMemory(IoGetCurrentProcess(), (PVOID)moduleBase, IoGetCurrentProcess(), mem, size, KernelMode, &retSize);	// 读一下,锁物理页

	ULONG_PTR func = FindAddressByCode(moduleBase, size + moduleBase, fs, 1);

	if (mem) ExFreePool(mem);
	return func;
}