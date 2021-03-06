// monitor.cpp: 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "monitor.h"
#define DEBUG
using namespace std;


HMODULE g_orgkl32;
HMODULE g_modkl32;
char* g_workpath;
size_t pid = GetCurrentProcessId();
HANDLE process;
BYTE hook_code[12] = { 0x48, 0xB8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xFF, 0xE0 };
char org_kl32_path[200];
FILE* log_file = NULL;
char process_name[268];
char log_file_path[200];
char custom_log_file_path[200];


void logError(const char* notice) {
	cout << "Error:" << notice << "--" << "Error num is " << GetLastError() << endl;
}

void logTitle(const char* action) {
	SYSTEMTIME st = { 0 };
	if (log_file == NULL) {
		if (fopen_s(&log_file, log_file_path, "a+") != 0) {
			if (fopen_s(&log_file, custom_log_file_path, "a+") != 0) {
				logError("Can't Create LOG");
			}
		}
	}
	GetLocalTime(&st);
	fprintf(log_file, "______________________________________________________________________________________________\n");
	fprintf(log_file,"TIME: %d-%02d-%02d %02d:%02d:%02d\n",
		st.wYear,
		st.wMonth,
		st.wDay,
		st.wHour,
		st.wMinute,
		st.wSecond);
	fprintf(log_file, "PROCESS NAME: %s\n", process_name);

	fprintf(log_file, "ACTION: %s\n", action);
}



void generatePath(char* full_path, const char* file_name) {
	strcpy_s(full_path, 200, g_workpath);
	strcat_s(full_path, 200, file_name);
}

UINT64 _getProcAddr(void* _addr, const char* name)
{
	if (_addr == NULL) {
		logError("No module");
		system("pause");
		exit(-1);
	}
	int i;
	UINT64 addr = (UINT64)_addr;
	UINT32 pe_header_offset = *(UINT32*)(addr + 0x3c);
	UINT32 optional_header_offset = pe_header_offset + 24;
	UINT16 optional_header_size = *(UINT16*)(addr + pe_header_offset + 20);
	UINT16 magic = *(UINT16*)(addr + optional_header_offset);
	UINT32 ET_offset = *(UINT32*)(addr + optional_header_offset + 112);

	UINT64 ET_addr = addr + ET_offset;
	UINT32 name_list_size = *(UINT32*)(ET_addr + 24);
	UINT32 name_list_offset = *(UINT32*)(ET_addr + 32);
	UINT32 addr_list_offset = *(UINT32*)(ET_addr + 28);
	UINT32 ordinal_list_offset = *(UINT32*)(ET_addr + 36);

	UINT64 ordinal_list_addr = ordinal_list_offset + addr;
	UINT64 name_list_addr = name_list_offset + addr;
	UINT64 addr_list_addr = addr_list_offset + addr;
	UINT32 func_offset;
	UINT16 func_ordinal;
	UINT32 name_offset;

	char* func_name;
	for (i = 0; i < name_list_size; i++) {
		name_offset = *(UINT32*)(name_list_addr + i * 4);
		func_ordinal = *(UINT16*)(ordinal_list_addr + i * 2);
		func_name = (char*)(addr + name_offset);

		if (strcmp(func_name, name) == 0) {
			func_offset = *(UINT32*)(addr_list_addr + func_ordinal * 4);
			return (addr + func_offset);
		}
	}
	return 0;
}

//根据pid获取module entry
BOOL _getModuleEntryrEx(MODULEENTRY32* me32, size_t pid, const WCHAR* module_name) {
	HANDLE process_snap = INVALID_HANDLE_VALUE;
	me32->dwSize = sizeof(MODULEENTRY32);

	process_snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
	if (process_snap == INVALID_HANDLE_VALUE) {
		cout << "error get snapshot" << endl;
		return FALSE;
	}
	if (!Module32First(process_snap, me32)) {
		cout << "error get module" << endl;
		return FALSE;
	}

	do {
		if (module_name == NULL) {
			wcout << me32->szModule << endl;
		}
		else {
			if (wcscmp(me32->szModule, module_name) == 0) {
				return TRUE;
			}
		}
	} while (Module32Next(process_snap, me32));
	return FALSE;

}

BOOL initHook(char* workpath) {
#ifdef DEBUG
	cout << "Hook init begin" << endl;
#endif // DEBUG
	process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (process == NULL) {
		logError("Error when opening process");
	}

	//构造未修改的kernel32的路径
	g_workpath = workpath;
	generatePath(org_kl32_path, "original_kernel32.dll");
#ifdef DEBUG
	cout << "original kernel32 path is:" << org_kl32_path << endl;
#endif // DEBUG


	//初始化log文件路径属性
	generatePath(log_file_path, "j_monitor.log");
	GetModuleFileNameA(NULL, process_name, sizeof(process_name));
	_itoa_s(pid, custom_log_file_path, 10);
	strcat_s(custom_log_file_path, 200, "_jlog.log");
	
#ifdef DEBUG
	cout << "second log file is:" << custom_log_file_path << endl;
	cout << "currrent process is :" << process_name << endl;
#endif // DEBUG

	MODULEENTRY32 me32;
	BYTE* kl32_cp;
	if (_getModuleEntryrEx(&me32, pid, L"KERNEL32.DLL") == 0) {
		logError("Can't get KERNEL32.DLL module entry");
		return FALSE;
	}
	size_t mod_size = me32.modBaseSize;
	BYTE* mod_addr = me32.modBaseAddr;
	kl32_cp = (BYTE*)LoadLibraryA(org_kl32_path);
	g_modkl32 = (HMODULE)mod_addr;
	g_orgkl32 = (HMODULE)kl32_cp;

	//需要hook的函数名称和数量
	int hook_func_total = 2;
	const char* org_func_name[] = {
		"CreateProcessA",
		"CreateProcessW"
	};
	void* mod_func_addr[] = {
		mycreateProcessA,
		mycreateProcessW
	};

	//执行hook
	for (int i = 0; i < hook_func_total; i++) {
		void* org_func_addr = (void*)GetProcAddress(g_modkl32, org_func_name[i]);
		*(UINT64*)(hook_code + 2) = (UINT64)mod_func_addr[i];
		if (WriteProcessMemory(process, org_func_addr, hook_code, 12, NULL) == 0) {
			logError("write error");
		}
		else {
			cout << "write at:" << hex << (UINT64)org_func_addr << endl;
		}
	

		//针对不同的函数处理通用hook方式无法处理的部分
		//处理CreateProcess
		pacthCreateProcess(org_func_addr, org_func_name[i]);

		
	}
	
#ifdef DEBUG
	cout << "hook init end" << endl;
#endif // DEBUG

}


FARPROC myProcess(HMODULE hmodule, LPCSTR lpProcName)
{
	int i = 0;
	FARPROC(*getProcAddress)(HMODULE, LPCSTR) = (FARPROC(*)(HMODULE, LPCSTR)) _getProcAddr(g_orgkl32, "GetProcAddress");
	printf("process call GetProcAddress\n");
	printf("module_addr: %p\n", hmodule);
	printf("func_name: %s\n", lpProcName);
	printf("func_addr: %p\n", getProcAddress);
	if (strcmp(lpProcName, "FlsFree") == 0) {
		return (*getProcAddress)(hmodule, lpProcName);
	}

	return (*getProcAddress)(hmodule, lpProcName);
}

BOOL mycreateProcessW(
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
) {
	BOOL(*createProcess)(
		LPCWSTR,
		LPWSTR,
		LPSECURITY_ATTRIBUTES,
		LPSECURITY_ATTRIBUTES,
		BOOL,
		DWORD,
		LPVOID,
		LPCWSTR,
		LPSTARTUPINFOW,
		LPPROCESS_INFORMATION) =
		(BOOL(*)(
			LPCWSTR,
			LPWSTR,
			LPSECURITY_ATTRIBUTES,
			LPSECURITY_ATTRIBUTES,
			BOOL,
			DWORD,
			LPVOID,
			LPCWSTR,
			LPSTARTUPINFOW,
			LPPROCESS_INFORMATION))_getProcAddr(g_orgkl32, "CreateProcessW");

	logTitle("call CreateProcessW");
	const WCHAR* tmp = (lpApplicationName != NULL) ? lpApplicationName : lpCommandLine;
	fwprintf(log_file, L"EXE FILE PATH : %s\n", tmp);
	fclose(log_file);
	log_file = NULL;

	if (createProcess == NULL) {
		logError("can't find real function");
		return 0;
	}
	else {
		return (*createProcess)
			(lpApplicationName,
				lpCommandLine,
				lpProcessAttributes,
				lpThreadAttributes,
				bInheritHandles,
				dwCreationFlags,
				lpEnvironment,
				lpCurrentDirectory,
				lpStartupInfo, lpProcessInformation);
	}
};

BOOL mycreateProcessA(
	LPCSTR lpApplicationName,
	LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
) {
	BOOL(*createProcess)(
		LPCSTR,
		LPSTR,
		LPSECURITY_ATTRIBUTES,
		LPSECURITY_ATTRIBUTES,
		BOOL,
		DWORD,
		LPVOID,
		LPCSTR,
		LPSTARTUPINFOA,
		LPPROCESS_INFORMATION) =
		(BOOL(*)(
			LPCSTR,
			LPSTR,
			LPSECURITY_ATTRIBUTES,
			LPSECURITY_ATTRIBUTES,
			BOOL,
			DWORD,
			LPVOID,
			LPCSTR,
			LPSTARTUPINFOA,
			LPPROCESS_INFORMATION))_getProcAddr(g_orgkl32, "CreateProcessA");
	
	//读取和操作参数

	logTitle("call CreateProcessA");
	const char* tmp = (lpApplicationName != NULL) ? lpApplicationName : lpCommandLine;
	fprintf(log_file, "EXE FILE PATH : %s\n", tmp);
#ifdef DETAIL_LOG
	fprintf(log_file, "lpApplicationName is: %s\n", lpApplicationName);
	fprintf(log_file, "lpCommandLine is: %s\n", lpCommandLine);
	fprintf(log_file, "lpCurrentDirectory is: %s\n", lpCurrentDirectory);
#endif // DEBUG
	fclose(log_file);
	log_file = NULL;
		

	//调用原函数
	if (createProcess == NULL) {
		logError("can't find real function");
		return 0;
	}
	else {
		return (*createProcess)
			(lpApplicationName,
				lpCommandLine,
				lpProcessAttributes,
				lpThreadAttributes,
				bInheritHandles,
				dwCreationFlags,
				lpEnvironment,
				lpCurrentDirectory,
				lpStartupInfo, lpProcessInformation);
	}
};

void pacthCreateProcess(void* org_func_addr, const char* org_func_name) {
	UINT64 patch_jmpto_addr = (UINT64)org_func_addr + 0x4d;
	*(UINT64*)(hook_code + 2) = patch_jmpto_addr;
	UINT64 patch_addr = _getProcAddr(g_orgkl32, org_func_name) + 0x4d;
	if (WriteProcessMemory(process, (void*)patch_addr, hook_code, 12, NULL) == 0) {
		logError("write error");
	}
#ifdef DEBUG
	cout << "origin func addr:" << hex << (UINT64)org_func_addr << endl;
	cout << "write at:" << hex << (UINT64)patch_addr << endl;
#endif // DEBUG
}