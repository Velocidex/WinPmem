#ifndef _WARNINGS_H_
#define _WARNINGS_H_

#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union
#pragma warning(disable:6320)  // Exception-filter expression is the constant EXCEPTION_EXECUTE_HANDLER.
#pragma warning(disable:28175)  // The 'FastIoDispatch' member of _DRIVER_OBJECT should not be accessed by a driver
#pragma warning(disable:28170)  // neither PAGED_CODE nor PAGED_CODE_LOCKED was found
#pragma warning(disable:28023)  // The function being assigned or passed should have a _Function_class_ annotation for at least one of the class(es) in: 'FAST_IO_READ'

#pragma warning(disable:30029) // A call was made to MmMapIoSpace(). If executable memory is not required, please use MmMapIoSpaceEx(). MmMapIoSpaceEx(): "Available starting with Windows 10". Definitely no.
#pragma warning(disable:30030) // Allocating executable memory via specifying a MM_PAGE_PRIORITY type. Sigh. :-/ "| MdlMappingNoExecute" not tested with Win7 yet. Could work or not.

#endif
