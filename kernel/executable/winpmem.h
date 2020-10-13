#include "windows.h"
#include "stdio.h"
#include "tchar.h"
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <varargs.h>

typedef LARGE_INTEGER PHYSICAL_ADDRESS, *PPHYSICAL_ADDRESS;

typedef struct _PHYSICAL_MEMORY_RANGE {
    PHYSICAL_ADDRESS BaseAddress;
    LARGE_INTEGER NumberOfBytes;
} PHYSICAL_MEMORY_RANGE, *PPHYSICAL_MEMORY_RANGE;

#include "..\userspace_interface\ctl_codes.h"
#include "..\userspace_interface\winpmem_shared.h"

static TCHAR version[] = TEXT(PMEM_DRIVER_VERSION) TEXT(" ") TEXT(__DATE__);

// These numbers are set in the resource editor for the FILE resource.
#define WINPMEM_64BIT_DRIVER 104
#define WINPMEM_32BIT_DRIVER 105


class WinPmem
{
public:

        WinPmem();
        virtual ~WinPmem();

        virtual __int64 install_driver();
        virtual __int64 uninstall_driver();
        virtual __int64 set_write_enabled();
        virtual __int64 set_acquisition_mode(unsigned __int32 mode);
        virtual void set_driver_filename(TCHAR *driver_filename);
        virtual void print_memory_info(PWINPMEM_MEMORY_INFO pinfo);

        // In order to create an image:

        // 1. Create an output file with create_output_file()
        // 2. Select either write_raw_image() or write_crashdump().
        // 3. When this object is deleted, the file is closed.
        virtual __int64 create_output_file(TCHAR *output_filename);
        virtual __int64 write_raw_image();

        // This is set if output should be suppressed (e.g. if we pipe the
        // image to the STDOUT).
        __int64 suppress_output;
        TCHAR last_error[1024];

        virtual __int64 extract_driver() = 0;
        virtual __int64 extract_driver(TCHAR *driver_filename);

protected:

        __int64 extract_file_(__int64 resource_id, TCHAR *filename);

        virtual void LogError(TCHAR *message);
        virtual void Log(const TCHAR *message, ...);
        virtual void LogLastError(TCHAR *message);

        __int64 pad(unsigned __int64 start, unsigned __int64 length);
        __int64 copy_memory_small(unsigned __int64 start, unsigned __int64 end);
        __int64 copy_memory(unsigned __int64 start, unsigned __int64 end);

        // The file handle to the pmem device.
        HANDLE fd_;

        // The file handle to the image file.
        HANDLE out_fd_;
        TCHAR *service_name;
        char * buffer_;
        size_t buffer_size_;
        TCHAR *driver_filename_;
        bool driver_is_tempfile_;

        // This is the maximum size of memory calculated.
        unsigned __int64 max_physical_memory_;

        // Current offset in output file (Total bytes written so far).
        unsigned __int64 out_offset;

        // The current acquisition mode.
        unsigned __int32 mode_;
        unsigned __int32 default_mode_;

private:
        void print_mode_(unsigned __int32 mode);
        char * metadata_;
        DWORD metadata_len_;

};

class WinPmem32: public WinPmem
{
  virtual __int64 extract_driver();
};

class WinPmem64: public WinPmem
{
  virtual __int64 extract_driver();
};



char *asprintf(const char *fmt, ...);
TCHAR *aswprintf(const TCHAR *fmt, ...);
