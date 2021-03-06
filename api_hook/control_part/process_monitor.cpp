// process_monitor.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#define DEBUG
using namespace std;

void logError(const char* notice) {
	cout << "Error:" << notice << "--" << "Error num is" << GetLastError() << endl;
}

void setDebugPrivilege(int flag)
{
	HANDLE pid_handle;
	TOKEN_PRIVILEGES tp;
	LUID luid;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &pid_handle))
		puts("failed to OpenProcessToken!");


	if (!LookupPrivilegeValue(
		NULL,            // lookup privilege on local system
		SE_DEBUG_NAME,   // privilege to lookup
		&luid))        // receives LUID of privilege
	{
		CloseHandle(pid_handle);
		puts("failed to lookup privilegeValue!");
	}
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = flag ? SE_PRIVILEGE_ENABLED : 0;

	// Enable the privilege or disable all privileges.

	if (!AdjustTokenPrivileges(
		pid_handle,
		FALSE,
		&tp,
		sizeof(tp),
		(PTOKEN_PRIVILEGES)NULL,
		(PDWORD)NULL))
	{
		CloseHandle(pid_handle);
		puts("failed to set privilege!");
	}


	if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
		puts("do not hava specified privilege!");
}

//获取其他进程内函数的地址
LPVOID _getProcAddrEx(HANDLE process, BYTE* addr, const char* name) {
	int i;
	unsigned char buf[10240];
	char func_name_buf[100];

	if (ReadProcessMemory(process, addr, buf, 1024, NULL) == 0) {
		cout << "读取内存失败：" << GetLastError() << endl;
		return 0;
	}

	UINT32 pe_header_offset;
	UINT32 optional_header_offset;
	UINT16 optional_header_size;
	UINT16 magic;
	UINT32 ET_offset;

	pe_header_offset = *(UINT32*)(buf + 0x3c);
	optional_header_offset = pe_header_offset + 24;
	optional_header_size = *(UINT16*)(buf + pe_header_offset + 20);
	magic = *(UINT16*)(buf + optional_header_offset);
	ET_offset = *(UINT32*)(buf + optional_header_offset + 112);

	if (ReadProcessMemory(process, addr + ET_offset, buf, 1024, NULL) == 0) {
		cout << "读取内存失败：" << GetLastError() << endl;
		return 0;
	}

	UINT32 name_list_size;
	UINT32 name_list_offset;
	UINT32 addr_list_offset;
	UINT32 ordinal_list_offset;
	UINT32 ordinal_base;
	name_list_size = *(UINT32*)(buf + 24);
	ordinal_list_offset = *(UINT32*)(buf + 36);
	name_list_offset = *(UINT32*)(buf + 32);
	addr_list_offset = *(UINT32*)(buf + 28);
	//ordinal_base = *(UINT32*)(buf + 16); //这个好像没有

	if (ReadProcessMemory(process, addr + name_list_offset, buf, 10240, NULL) == 0) {
		cout << "读取内存失败：" << GetLastError() << endl;
		return 0;
	}

	UINT32 func_offset;
	UINT32 name_offset;
	for(i = 0; i < name_list_size; i++) {
		name_offset = *(UINT32*)(buf + i * 4);

		if (ReadProcessMemory(process, addr + name_offset, func_name_buf, 100, NULL) == 0) {
			cout << "读取内存失败：" << GetLastError() << endl;
			return 0;
		}
		if (name == NULL) {
			cout << func_name_buf << endl;
			continue;
		}
		if(strcmp(func_name_buf, name) == 0) {
			if (ReadProcessMemory(process, addr + ordinal_list_offset + i * 2, buf, 2, NULL) == 0) {
				cout << "读取内存失败：" << GetLastError() << endl;
				return 0;
			}
			UINT16 func_ordinal = *(UINT16*)buf;
			if (ReadProcessMemory(process, addr + addr_list_offset + func_ordinal * 4, buf, 4, NULL) == 0) {
				cout << "读取内存失败：" << GetLastError() << endl;
				return 0;
			}
			
			func_offset = *(UINT32*)(buf);
			return (addr + func_offset);
		}
	}
	return 0;

}

//获取其他进程模块的地址
HMODULE _getModuleAddrEx(UINT32 pid, const WCHAR* module_name) {
	HANDLE process_snap = INVALID_HANDLE_VALUE;
	MODULEENTRY32 me32;
	me32.dwSize = sizeof(MODULEENTRY32);
	process_snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
	if (process_snap == INVALID_HANDLE_VALUE) {
		cout << "error get snapshot" << endl;
	}
	if (!Module32First(process_snap, &me32)) {
		cout << "error get module" << endl;
	}
	
	do {
		if (module_name == NULL) {
			wcout << me32.szModule << endl;
		}
		else {
			if (wcscmp(me32.szModule, module_name) == 0) {
				return (HMODULE)me32.modBaseAddr;
			}
		}
		
	} while (Module32Next(process_snap, &me32));
	return NULL;
	
}



//构造当前目录下文件的绝对地址
int getCurrentDirFilePath(char* dll_path, const char* dll_name) {
	GetCurrentDirectoryA(248, (LPSTR)dll_path);
	int pwd_len = strlen(dll_path);
	int dll_name_len = strlen(dll_name);
	memcpy(dll_path + pwd_len, dll_name, dll_name_len + 1);
	int dll_path_len = strlen(dll_path);
	//cout << dll_path_len << endl;
	//cout << dll_path << endl;
	return pwd_len;
}

void _getImportTableEx(HANDLE process, BYTE* addr) {
	int i;
	unsigned char buf[10240];
	char func_name_buf[100];

	if (ReadProcessMemory(process, addr, buf, 1024, NULL) == 0) {
		cout << GetLastError() << endl;
		return;
	}
	UINT32 pe_header_offset = *(UINT32*)(buf + 0x3c);
	UINT32 optional_header_offset = pe_header_offset + 24;
	UINT16 optional_header_size = *(UINT16*)(buf + pe_header_offset + 20);
	UINT16 magic = *(UINT16*)(buf + optional_header_offset);
	UINT32 IT_entry_offset = optional_header_offset + 120;
	UINT32 IT_offset = *(UINT32*)(buf + IT_entry_offset);


	if (ReadProcessMemory(process, addr + IT_offset, buf, 1024, NULL) == 0) {
		cout << "error when reading IT" << endl;
		cout << dec << GetLastError() << endl;
	}
	cout << "IT VA:" << hex << (UINT64)(addr + IT_offset) << endl;
	UINT32 ILT_offset = *(UINT32*)(buf);
	UINT32 IAT_offset = *(UINT32*)(buf + 16);
	cout << "ILT_offset:" << hex << ILT_offset << endl;

	if (ReadProcessMemory(process, addr + ILT_offset, buf, 1024, NULL) == 0) {
		cout << "error when reading ILT" << endl;
		cout << dec << GetLastError() << endl;
	}

	UINT64 ILT_first_item = 0;
	int offset = 0;
	do {
		ILT_first_item = *(UINT64*)(buf + offset);
		cout << "Func name address:" << hex << ILT_first_item << endl;
		if (ReadProcessMemory(process, addr + ILT_first_item, func_name_buf, 1024, NULL) == 0) {
			cout << "error when reading ILT item" << endl;
			cout << dec << GetLastError() << endl;
		}

		UINT16 hint = *(UINT16*)(func_name_buf);
		char* name = func_name_buf + 2;
		cout << "import func hint:" << hint << endl;
		cout << "import func name:" << name << endl;
		offset += 8;
	} while (ILT_first_item != 0);

	if (ReadProcessMemory(process, addr + IAT_offset, buf, 100, NULL) == 0) {
		cout << "error when reading IAT" << endl;
		cout << dec << GetLastError() << endl;
	}
	cout << "First func address:" << hex << *(UINT64*)buf << endl;
}


void dohook(int pid, HANDLE process, unsigned char* hook_code, HMODULE module, const char* original_proc, const char* hook_proc) {


	UINT64 original_process = (UINT64)_getProcAddrEx(process, (BYTE*)module, original_proc);

	HMODULE monitor = _getModuleAddrEx(pid, TEXT("monitor.dll"));
	//_getProcAddrEx(process, (BYTE*)monitor, NULL);
	cout << "monitor.dll remote address：" << hex << (UINT64)monitor << endl;
	UINT64 myProcess_addr = (UINT64)_getProcAddrEx(process, (BYTE*)monitor, hook_proc);
	*(UINT64*)(hook_code + 2) = myProcess_addr;
	cout << "myProcess addr: " << hex << myProcess_addr << endl;

	if (WriteProcessMemory(process, (LPVOID)original_process, hook_code, 12, NULL) == 0) {
		cout << "error when writing" << endl;
		cout << dec << GetLastError() << endl;
	}
	else {
		cout << "write success:" << original_process << endl;
	}

}

void initLogFile() {
	FILE* log_file = NULL;
	if (fopen_s(&log_file, "j_monitor.log", "r") != 0) {
		fopen_s(&log_file, "j_monitor.log", "a");
		fprintf(log_file,
			"#######################################################\n"
			"## This file record the monitored processes'actions  ##\n"
			"## Each Item looks like this:                        ##\n"
			"##      Time                                         ##\n"
			"##      Process name                                 ##\n"
			"##      Process action                               ##\n"
			"##      Other infomation about the action            ##\n"
			"#######################################################\n"
		);
	}
	fclose(log_file);
	
}

int main()
{
	int i;
	const char* dll_name = "\\monitor.dll";
	char dll_path[260];
	unsigned char buf[10240] = { 0 };
	unsigned char hook_code[12] = { 0x48, 0xB8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xFF, 0xE0 };

	initLogFile();
	setDebugPrivilege(1);
	//构造注入的dll的绝对路径
	int pwd_len = getCurrentDirFilePath(dll_path, dll_name);
	int dll_path_len = strlen(dll_path);
	//setDebugPrivilege(1);

	int pid = GetCurrentProcessId();
	cout <<
		"##########################################\n"
		"##     welcome to hook monitor          ##\n"
		"##########################################\n";
	cout << "Info:" << endl;
	cout << "current process id is:" << pid << endl;
	cout << "Inject dll path is:" << dll_path << endl;
	cout << endl;
	while (true) {
		cout << "Input target pid to start monitoring" << endl;
		cin >> pid;
		HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
		if (process == NULL) {
			cout << "fail to open process" << endl;
		}

		//在目标进程中分配储存dll地址的内存
		LPVOID dll_path_inj_addr = VirtualAllocEx(process, NULL, dll_path_len + 1, MEM_COMMIT, PAGE_READWRITE);
		//把当前文件夹路径写入目标进程，用于写log文件
		LPVOID work_path = VirtualAllocEx(process, NULL, pwd_len + 1, MEM_COMMIT, PAGE_READWRITE);
		if (WriteProcessMemory(process, dll_path_inj_addr, dll_path, dll_path_len + 1, NULL) == 0) {
			cout << "error when writing" << endl;
			cout << dec << GetLastError() << endl;
		}
		if (WriteProcessMemory(process, work_path, dll_path, pwd_len + 1, NULL) == 0) {
			cout << "error when writing" << endl;
			cout << dec << GetLastError() << endl;
		}
		HMODULE kernel32 = _getModuleAddrEx(pid, TEXT("KERNEL32.DLL"));

		//远程载入dll
		//获取注入进程loadlibrary的地址
		UINT64 loadlib_ex = (UINT64)_getProcAddrEx(process, (BYTE*)kernel32, "LoadLibraryA");
		
		CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)loadlib_ex, dll_path_inj_addr, 0, NULL);
		//等待载入
		Sleep(500);

		HMODULE monitor = _getModuleAddrEx(pid, TEXT("monitor.dll"));
		UINT64 initHook = (UINT64)_getProcAddrEx(process, (BYTE*)monitor, "initHook");
#ifdef DEBUG
		cout << "Injected process modules:" << endl;
		_getModuleAddrEx(pid, NULL);
		cout << "monitor.dll addr:" << hex << (UINT64)monitor << endl;
		cout << "monitor.dll export function:" << endl;
		_getProcAddrEx(process, (BYTE*)monitor, NULL);
		cout << "initHook addr:" << hex << (UINT64)initHook << endl;
		//system("pause");
#endif // DEBUG
		if (initHook == 0) {
			logError("Hook init failed, no initHook founded");
		}
		//远程调用hook初始化模块（可以直接在dllmain里执行）
		CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)initHook, work_path, 0, NULL);
		Sleep(500);

		/*
		const char* original_proc[] =
		{
			"GetProcAddress",
			"CreateProcessW",
			"CreateProcessA"
		};
		const char* hook_proc[] =
		{
			"myProcess",
			"mycreateProcessW",
			"mycreateProcessA"
		};
		int hook_amount = 3;
		
		dohook(pid, process, hook_code, kernel32, original_proc[0], hook_proc[0]);
		
		
		
		HMODULE m = LoadLibraryA("kernel32.dll");
		if (GetProcAddress(m, "CreateProcessW") == 0) {
			printf("not found\n");
		}

		*/
		
	}
}

	


