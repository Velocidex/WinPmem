/*
  Copyright 2012-2014 Michael Cohen <scudette@gmail.com>
  Authors: Michael Cohen <mike@velocidex.com>, Viviane Zwanger

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/


#include "winpmem.h"
#include <time.h>

constexpr auto BUFF_SIZE = (4096 * 4096);

/**
 * Pad file in pad range with zeros.
*/
__int64 WinPmem::pad(unsigned __int64 start, unsigned __int64 length)
{
        DWORD bytes_written = 0;
        BOOL result = FALSE;
        unsigned __int64 count = 0;
        unsigned char * paddingbuffer = (unsigned char * ) malloc(BUFF_SIZE);
        if (!paddingbuffer) {
                return 0;
        };
        ZeroMemory(paddingbuffer, BUFF_SIZE);

        printf("pad\n");
        printf(" - length: 0x%llx\n", length);
        fflush(stdout);

        while (length > 0) {
                DWORD to_write = (DWORD)min((BUFF_SIZE), length);
                result = WriteFile(out_fd_, paddingbuffer, to_write, &bytes_written, NULL);
                if ((!(result)) || (bytes_written != to_write))
                {
                        LogLastError(TEXT("Failed to write padding"));
                        goto error;
                }

                // === Progress printing ===
                if ((count % 50) == 0) {
                    Log(TEXT("\n%02lld%% 0x%08llX "), (start * 100) / max_physical_memory_,
                        start);
                }

                Log(TEXT("."));

                count++;
                out_offset += bytes_written;
                length -= bytes_written;
        };

        if (paddingbuffer) free(paddingbuffer);

        return 1;

        error:
        return 0;
}

// Copy memory from start to end in 1 page increments.
__int64 WinPmem::copy_memory_small(unsigned __int64 start, unsigned __int64 end) {
    LARGE_INTEGER large_start;

    // Total number of pages written.
    unsigned __int64 count = 0;

    BOOL result = FALSE;
    const int buff_size = 0x1000;
    unsigned char* largebuffer = (unsigned char*)malloc(buff_size);
    unsigned char* nullbuffer = (unsigned char*)calloc(buff_size, 1);

    if (start > max_physical_memory_) {
        return 0;
    }

    // Clamp the region to the top of physical memory.
    if (end > max_physical_memory_) {
        end = max_physical_memory_;
    }

    while (start < end) {
        DWORD to_write = (DWORD)min((buff_size), end - start);
        DWORD bytes_read = 0;
        DWORD bytes_written = 0;

        large_start.QuadPart = start;

        // seek input stream to the required offset.
        result = SetFilePointerEx(fd_, large_start, NULL, FILE_BEGIN);

        if (!(result)) {
            LogLastError(TEXT("Failed to seek in the pmem device.\n"));
            goto error;
        }

        // read from memory stream
        result = ReadFile(fd_, largebuffer, to_write, &bytes_read, NULL);

        // Page read failed - pad with nulls and keep going.
        if ((!(result)) || (bytes_read != to_write)) {
            result = WriteFile(out_fd_, nullbuffer, buff_size, &bytes_written, NULL);
        }
        else {
            result = WriteFile(out_fd_, largebuffer, bytes_read, &bytes_written, NULL);
        }

        if (!result) {
            LogLastError(TEXT("Failed to write image file"));
            goto error;
        }

        start += to_write;
        count++;
    }
    if (largebuffer) free(largebuffer);
    if (nullbuffer) free(nullbuffer);
    return 1;

error:
    if (largebuffer) free(largebuffer);
    if (nullbuffer) free(nullbuffer);
    return 0;
}

__int64 WinPmem::copy_memory(unsigned __int64 start, unsigned __int64 end) {
        LARGE_INTEGER large_start;

        // Total number of pages written.
        unsigned __int64 count = 0;

        BOOL result = FALSE;
        unsigned char * largebuffer = (unsigned char *) malloc(BUFF_SIZE); // ~ 16 MB

        if (start > max_physical_memory_)
        {
                return 0;
        }

        // Clamp the region to the top of physical memory.
        if (end > max_physical_memory_)
        {
                end = max_physical_memory_;
        }

        printf("\ncopy_memory\n");
        printf(" - start: 0x%llx\n", start);
        printf(" - end: 0x%llx\n", end);
        fflush(stdout);

        while(start < end)
        {
                DWORD to_write = (DWORD) min((BUFF_SIZE), end - start); // ReadFile wants a DWORD, for whatever reason.
                DWORD bytes_read = 0;
                DWORD bytes_written = 0;

                large_start.QuadPart = start;

                //printf(" - to_write: 0x%lx\n", (DWORD)to_write);
                //printf(" - start: 0x%llx\n", start);

                // seek
                result = SetFilePointerEx(fd_, large_start, NULL, FILE_BEGIN);

                if (!(result))
                {
                  LogLastError(TEXT("Failed to seek in the pmem device.\n"));
                  goto error;
                }

                auto indicator = TEXT(".");

                // read
                result = ReadFile(fd_, largebuffer, to_write, &bytes_read, NULL);

                if (!result || bytes_read != to_write) {
                    // Indicates this 16mb buffer was read slowly.
                    indicator = TEXT("x");
                    result = copy_memory_small( start, start + to_write);
                } else {
                    result = WriteFile(out_fd_, largebuffer, bytes_read, &bytes_written, NULL);
                }

                if (!result) {
                  LogLastError(TEXT("Failed to write image file"));
                  goto error;
                }

                out_offset += bytes_written;

                // === Progress printing ===
                if ((count % 50) == 0) {
                        Log(TEXT("\n%02lld%% 0x%08llX "), (start * 100) / max_physical_memory_,
                            start);
                }

                Log(indicator);

                start += to_write;
                count ++;
        }

        Log(TEXT("\n"));
        if (largebuffer) free(largebuffer);
        return 1;

error:
        if (largebuffer) free(largebuffer);
        return 0;
}


// Turn on write support in the driver.
__int64 WinPmem::set_write_enabled(void)
{
        unsigned _int32 mode = 0;
        DWORD size;
        BOOL result = FALSE;

        result = DeviceIoControl(fd_, IOCTL_WRITE_ENABLE,
                                 &mode, 4, // in
                                 NULL, 0, // out
                                 &size, NULL);

        if (!(result)) {
            LogError(TEXT("Failed to set write mode. Maybe these drivers do not support this mode?\n"));
            return -1;
        }

        Log(TEXT("Write mode enabled! Hope you know what you are doing.\n"));
        return 1;
}


void WinPmem::print_mode_(unsigned __int32 mode)
{
        switch(mode)
        {
                case PMEM_MODE_IOSPACE:
                        Log(TEXT("MMMapIoSpace"));
                        break;

                case PMEM_MODE_PHYSICAL:
                        Log(TEXT("\\\\.\\PhysicalMemory"));
                        break;

                case PMEM_MODE_PTE:
                        Log(TEXT("PTE Remapping"));
                        break;

                default:
                        Log(TEXT("Unknown"));
        }
}


// Display information about the memory geometry.
// Simply drop a 'care package' info struct (you get that from the driver) into this function to get a nice printout.
void WinPmem::print_memory_info(PWINPMEM_MEMORY_INFO pinfo)
{
        __int64 i=0;
        BOOL result = FALSE;

        if (!pinfo) return;

        Log(TEXT("CR3: 0x%010llX\n %d memory ranges:\n"), pinfo->CR3.QuadPart, pinfo->NumberOfRuns);

        for (i=0; i < pinfo->NumberOfRuns.QuadPart; i++)
        {
                Log(TEXT("Start 0x%08llX - Length 0x%08llX\n"), pinfo->Run[i].BaseAddress.QuadPart, pinfo->Run[i].NumberOfBytes.QuadPart);
                max_physical_memory_ = pinfo->Run[i].BaseAddress.QuadPart + pinfo->Run[i].NumberOfBytes.QuadPart;
        }

        Log(TEXT("max_physical_memory_ 0x%llx\n"), max_physical_memory_);

        Log(TEXT("Acquitision mode "));
        print_mode_(mode_);
        Log(TEXT("\n"));

        return;
}

__int64 WinPmem::set_acquisition_mode(unsigned __int32 mode)
{
        DWORD size;
        BOOL result = FALSE;

        // let's do some sanity checking first.
        if (! ((mode == PMEM_MODE_IOSPACE) || (mode == PMEM_MODE_PHYSICAL) || (mode == PMEM_MODE_PTE)) )
        {
                Log(TEXT("This mode does not exist!"));
                return -1;
        }

        result = DeviceIoControl(fd_, IOCTL_SET_MODE,
                                                &mode, 4, // in
                                                NULL, 0,  // out
                                          &size, NULL);

        // Set the acquisition mode.
        if(!(result))
        {
                Log(TEXT("Failed to set acquisition mode %lu "), mode);
                LogLastError(L"");
                print_mode_(mode);
                Log(TEXT("\n"));
                return -1;
        }

        mode_ = mode;
        return 1;
}

__int64 WinPmem::create_output_file(TCHAR *output_filename)
{
        __int64 status = 1;

        // The special file name of - means we should use stdout.

        if (!_tcscmp(output_filename, TEXT("-")))
        {
                out_fd_ = GetStdHandle(STD_OUTPUT_HANDLE);
                suppress_output = TRUE;
                status = 1;
                goto exit;
        }

        // Create the output file.
        out_fd_ = CreateFile(output_filename,
                                           GENERIC_WRITE,
                                           FILE_SHARE_READ,
                                           NULL,
                                           CREATE_ALWAYS,
                                           FILE_ATTRIBUTE_NORMAL,
                                           NULL);

        if (out_fd_ == INVALID_HANDLE_VALUE)
        {
                LogLastError(TEXT("Unable to create output file."));
                status = -1;
                goto exit;
        }

exit:
        return status;
}

__int64 WinPmem::write_raw_image()
{
        // Somewhere to store the info from the driver;
        WINPMEM_MEMORY_INFO info;
        DWORD size;
        BOOL result = FALSE;
        __int64 i;
        __int64 status = -1;
        SYSTEMTIME st, lt;
        BYTE infoBuffer[sizeof(WINPMEM_MEMORY_INFO) + sizeof(LARGE_INTEGER) * 32] = { 0 };

        if(out_fd_==INVALID_HANDLE_VALUE)
        {
                LogError(TEXT("Must open an output file first."));
                goto exit;
        }

        RtlZeroMemory(&info, sizeof(WINPMEM_MEMORY_INFO));

        // Get the memory ranges.
        result = DeviceIoControl(fd_, IOCTL_GET_INFO,
                                                NULL, 0, // in
                                                (char *)&infoBuffer, sizeof(infoBuffer), // out
                                                &size, NULL);

        if (!(result))
        {
                LogLastError(TEXT("Failed to get memory geometry,"));
                status = -1;
                goto exit;
        }
#ifdef _WIN64
        RtlCopyMemory(&info, infoBuffer, sizeof(WINPMEM_MEMORY_INFO));
#else
        {
            SYSTEM_INFO sys_info = { 0 };

            GetNativeSystemInfo(&sys_info);

            switch (sys_info.wProcessorArchitecture)
            {
                case PROCESSOR_ARCHITECTURE_AMD64:
                {
                    DWORD dwOffset = FIELD_OFFSET(WINPMEM_MEMORY_INFO, PfnDataBase);
                    RtlCopyMemory(&info, infoBuffer, dwOffset);
                    RtlCopyMemory(&info.PfnDataBase, infoBuffer + dwOffset + 32 * sizeof(LARGE_INTEGER), sizeof(WINPMEM_MEMORY_INFO) - dwOffset);
                    break;
                }
                default:
                    RtlCopyMemory(&info, infoBuffer, sizeof(WINPMEM_MEMORY_INFO));
                    break;
            }
        }
#endif

        GetSystemTime(&st);
        printf("The system time is: %02d:%02d:%02d\n", st.wHour, st.wMinute, st.wSecond);
        Log(TEXT("Will generate a RAW image \n"));

        #ifdef _WIN64
        printf(" - buffer_size_: 0x%llx\n", buffer_size_);
        #else
        printf(" - buffer_size_: 0x%x\n", buffer_size_);
        #endif

        print_memory_info(&info);
        fflush(stdout);

        // write ranges and pass non ranges

        __int64 current = 0;

        for(i=0; i < info.NumberOfRuns.QuadPart; i++)
        {
                if(info.Run[i].BaseAddress.QuadPart > current)
                {
                        // pad zeros from current until begin of next RAM memory region.
                  printf("Padding from 0x%08llX to 0x%08llX\n", current, info.Run[i].BaseAddress.QuadPart);
                  fflush(stdout);
                  if (!pad(current, info.Run[i].BaseAddress.QuadPart - current))
                  {
                        printf("padding went terribly wrong! Cancelling & terminating. \n");
                        fflush(stdout);
                        goto exit;
                  }
                }

                // write next RAM memory region to file.

                copy_memory(info.Run[i].BaseAddress.QuadPart, info.Run[i].BaseAddress.QuadPart + info.Run[i].NumberOfBytes.QuadPart);

                // update current cursor offset.

                current = info.Run[i].BaseAddress.QuadPart + info.Run[i].NumberOfBytes.QuadPart;
        }

        // All is well.
        status = 1;

        exit:
        CloseHandle(out_fd_);
        out_fd_ = INVALID_HANDLE_VALUE;

        GetSystemTime(&st);
        printf("The system time is: %02d:%02d:%02d\n", st.wHour, st.wMinute, st.wSecond);
        fflush(stdout);
        return status;
}


WinPmem::WinPmem():
        fd_(INVALID_HANDLE_VALUE),
        buffer_size_(0x1000), // can be used for write enabled mode.
        buffer_(NULL),
        suppress_output(FALSE),
        service_name(PMEM_SERVICE_NAME),
        max_physical_memory_(0),
        mode_(PMEM_MODE_PHYSICAL),
        default_mode_(PMEM_MODE_PHYSICAL),
        metadata_(NULL),
        metadata_len_(0),
        driver_filename_(NULL),
        driver_is_tempfile_(false),
        out_offset(0)

        {}


WinPmem::~WinPmem()
{
        if (fd_ != INVALID_HANDLE_VALUE)
        {
                CloseHandle(fd_);
        }

        if (buffer_)
        {
                delete [] buffer_;
        }

        if (driver_filename_ && driver_is_tempfile_) free(driver_filename_);
}

void WinPmem::LogError(TCHAR *message)
{
  _tcsncpy_s(last_error, message, sizeof(last_error));

  if (suppress_output) return;

  wprintf(L"%s", message);
  fflush(stdout);
}

void WinPmem::Log(const TCHAR *message, ...)
{
  if (suppress_output) return;

  va_list ap;
  va_start(ap, message);
  vwprintf(message, ap);
  va_end(ap);
  fflush(stdout);
}


void WinPmem::LogLastError(TCHAR *message)
{
  TCHAR *buffer;
  DWORD dw = GetLastError();

  FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&buffer,
        0, NULL );

  Log(L"%s", message);
  Log(L": %s\n", buffer);

}

__int64 WinPmem::extract_file_(__int64 resource_id, TCHAR *filename)
{
        // Locate the driver resource in the .EXE file.
        HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(resource_id), L"FILE");

        if (hRes == NULL)
        {
                LogError(TEXT("Could not locate driver resource."));
                goto error;
        }

        HGLOBAL hResLoad = LoadResource(NULL, hRes);

        if (hResLoad == NULL)
        {
                LogError(TEXT("Could not load driver resource."));
                goto error;
        }

        VOID *lpResLock = LockResource(hResLoad);

        if (lpResLock == NULL)
        {
                LogError(TEXT("Could not lock driver resource."));
                goto error;
        }

        DWORD size = SizeofResource(NULL, hRes);

        // Now open the filename and write the driver image on it.
        HANDLE out_fd = CreateFile(filename, GENERIC_WRITE, 0, NULL,
                                                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

        if(out_fd == INVALID_HANDLE_VALUE)
        {
                LogError(TEXT("Can not create temporary file."));
                goto error_resource;
        }

        if (!WriteFile(out_fd, lpResLock, size, &size, NULL))
        {
                LogError(TEXT("Can not write to temporary file. Maybe you do not have the rights?"));
                goto error_file;
        }

        CloseHandle(out_fd);

        return 1;

error_file:
        CloseHandle(out_fd);

error_resource:
error:
        return -1;

}


void WinPmem::set_driver_filename(TCHAR *driver_filename)
{
        DWORD res;

        if(driver_filename_)
        {
                free(driver_filename_);
                driver_filename_ = NULL;
        }

        if (driver_filename)
        {
                driver_filename_ = (TCHAR *)malloc(MAX_PATH * sizeof(TCHAR));

                if (driver_filename_)
                {
                  res = GetFullPathName(driver_filename, MAX_PATH, driver_filename_, NULL);
                }
        }
}


__int64 WinPmem::install_driver()
{
        SC_HANDLE scm, service;
        __int64 status = -1;

        // Try to load the driver from the resource section.
        if (extract_driver() < 0) goto error;

        uninstall_driver();

        scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);

        if (!scm)
        {
                LogError(TEXT("Can not open SCM. Are you administrator?\n"));
                goto error;
        }

        service = CreateService(scm,
                                                  service_name,
                                                  service_name,
                                                  SERVICE_ALL_ACCESS,
                                                  SERVICE_KERNEL_DRIVER,
                                                  SERVICE_DEMAND_START,
                                                  SERVICE_ERROR_NORMAL,
                                                  driver_filename_,
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  NULL);

        if (GetLastError() == ERROR_SERVICE_EXISTS)
        {
                service = OpenService(scm, service_name, SERVICE_ALL_ACCESS);
        }

        if (!service)
        {
                goto error;
        }

        if (!StartService(service, 0, NULL))
        {
                DWORD le = GetLastError();

                if (le != ERROR_SERVICE_ALREADY_RUNNING)
                {
                  printf("Error (0x%lx): StartService(), Cannot start the driver.\n", le);
                  fflush(stdout);
                  LogError(TEXT("Error: StartService(), Cannot start the driver.\n"));
                  goto service_error;
                }
        }

        Log(L"Loaded Driver %s.\n", driver_filename_);

        fd_ = CreateFile(TEXT("\\\\.\\") TEXT(PMEM_DEVICE_NAME_ASCII),
                                   // Write is needed for IOCTL.
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   NULL);

        if(fd_ == INVALID_HANDLE_VALUE)
                {
                LogError(TEXT("Can not open raw device."));
                status = -1;
        }

        status = 1;

        service_error:
        CloseServiceHandle(service);
        CloseServiceHandle(scm);

        error:
        // Only remove the driver file if it was a temporary file.
        if (driver_is_tempfile_)
        {
                Log(L"Deleting %s\n", driver_filename_);
                DeleteFile(driver_filename_);
        }

        return status;
}


__int64 WinPmem::uninstall_driver()
{
        SC_HANDLE scm, service;
        SERVICE_STATUS ServiceStatus;

        scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);

        if (!scm) return 0;

        service = OpenService(scm, service_name, SERVICE_ALL_ACCESS);

        if (service)
        {
                ControlService(service, SERVICE_CONTROL_STOP, &ServiceStatus);
        }

        DeleteService(service);
        CloseServiceHandle(service);
        Log(TEXT("Driver Unloaded.\n"));

        return 1;

        CloseServiceHandle(scm);
        return 0;
}


/* Create a YAML file describing the image encoded into a null terminated
   string. Caller will own the memory.
 */
char *store_metadata_(PWINPMEM_MEMORY_INFO info)
{
        SYSTEM_INFO sys_info;
        struct tm newtime;
        __time32_t aclock;

        char time_buffer[32];
        errno_t errNum;
        char *arch = NULL;

        _time32( &aclock );   // Get time in seconds.
        _gmtime32_s( &newtime, &aclock );   // Convert time to struct tm form.

        // Print local time as a string.
        errNum = asctime_s(time_buffer, 32, &newtime);

        if (errNum)
        {
                time_buffer[0] = 0;
        }


        // dumps - even on 32 bit platforms).
        ZeroMemory(&sys_info, sizeof(sys_info));
        GetNativeSystemInfo(&sys_info);

        switch(sys_info.wProcessorArchitecture)
        {
        case PROCESSOR_ARCHITECTURE_AMD64:
          arch = "AMD64";
          break;

        case PROCESSOR_ARCHITECTURE_INTEL:
          arch = "I386";
          break;

        default:
          arch = "Unknown";
        }

        return asprintf(// A YAML File describing metadata about this image.
                                  "# PMEM\n"
                                  "---\n"   // The start of the YAML file.
                                  "acquisition_tool: 'WinPMEM, driver version: " PMEM_DRIVER_VERSION "'\n"
                                  "acquisition_timestamp: %s\n"
                                  "CR3: %#llx\n"
                                  "NtBuildNumber: %#llx\n"
                                  "NtBuildNumberAddr: %#llx\n"
                                  "KernBase: %#llx\n"
                                  "Arch: %s\n"
                                  "...\n",  // This is the end of a YAML file.
                                  time_buffer,
                                  info->CR3.QuadPart,
                                  info->NtBuildNumber.QuadPart,
                                  info->NtBuildNumberAddr.QuadPart,
                                  info->KernBase.QuadPart,
                                  arch
                                  );

}


__int64 WinPmem::extract_driver(TCHAR *driver_filename)
{
        set_driver_filename(driver_filename);
        return extract_driver();
}


__int64 WinPmem64::extract_driver()
{
        // 64 bit drivers use PTE acquisition by default.
        default_mode_ = PMEM_MODE_PTE;

        if (!driver_filename_)
        {
                TCHAR path[MAX_PATH + 1];
                TCHAR filename[MAX_PATH + 1];

                // Gets the temp path env string (no guarantee it's a valid path).
                if(!GetTempPath(MAX_PATH, path))
                {
                        LogError(TEXT("Unable to determine temporary path."));
                        goto error;
                }

                GetTempFileName(path, service_name, 0, filename);
                set_driver_filename(filename);

                driver_is_tempfile_ = true;
        }

        Log(L"Extracting driver to %s\n", driver_filename_);

        return extract_file_(WINPMEM_64BIT_DRIVER, driver_filename_);

error:
        return -1;
}

__int64 WinPmem32::extract_driver()
{
        // 32 bit acquisition defaults to physical device.
        default_mode_ = PMEM_MODE_PHYSICAL;

        if (!driver_filename_)
        {
                TCHAR path[MAX_PATH + 1];
                TCHAR filename[MAX_PATH + 1];

                // Gets the temp path env string (no guarantee it's a valid path).
                if(!GetTempPath(MAX_PATH, path))
                {
                        LogError(TEXT("Unable to determine temporary path."));
                        goto error;
                }

                GetTempFileName(path, service_name, 0, filename);
                Log(L"extract_driver\n");
                Log(L" - service_name: %s\n", service_name);
                Log(L" - filename: %s\n", filename);
                set_driver_filename(filename);

                driver_is_tempfile_ = true;
        }

        Log(L" - Extracting driver to %s\n", driver_filename_);

        return extract_file_(WINPMEM_32BIT_DRIVER, driver_filename_);

error:
        return -1;
}


#ifdef _WIN32
#define vsnprintf _vsnprintf
#define vsnwprintf _vsnwprintf
#endif


// irghs! =>

char *asprintf(const char *fmt, ...) {
  /* Guess we need no more than 1000 bytes. */
  int n, size = 1000;
  char *p, *np;
  va_list ap;

  p = (char *)malloc (size);
  if (!p)
    return NULL;

  while (1) {
    /* Try to print in the allocated space. */
    va_start(ap, fmt);
    n = vsnprintf (p, size, fmt, ap);
    va_end(ap);

    /* If that worked, return the string. */
    if (n > -1 && n < size)
      return p;

    /* Else try again with more space. */
    if (n > -1)    /* glibc 2.1 */
      size = n+1;  /* precisely what is needed */

    else           /* glibc 2.0 */
      size *= 2;   /* twice the old size */

    np = (char *)realloc (p, size);
    if (np == NULL) {
      free(p);
      return NULL;

    } else {
      p = np;
    }

  }
}

TCHAR *aswprintf(const TCHAR *fmt, ...) {
  /* Guess we need no more than 1000 bytes. */
  int n, size = 1000;
  TCHAR *p, *np;
  va_list ap;

  p = (TCHAR *)malloc (size * sizeof(TCHAR));
  if (!p)
    return NULL;

  while (1) {
    /* Try to print in the allocated space. */
    va_start(ap, fmt);
    n = vsnwprintf (p, size, fmt, ap);
    va_end(ap);

    /* If that worked, return the string. */
    if (n > -1 && n < size)
      return p;

    /* Else try again with more space. */
    if (n > -1)    /* glibc 2.1 */
      size = n+1;  /* precisely what is needed */

    else           /* glibc 2.0 */
      size *= 2;   /* twice the old size */

    np = (TCHAR *)realloc (p, size * sizeof(TCHAR));
    if (np == NULL) {
      free(p);
      return NULL;

    } else {
      p = np;
    }

  }
}
