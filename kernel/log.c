#include "winpmem.h"
#include "log.h"


_IRQL_requires_max_(PASSIVE_LEVEL)
VOID writeToEventLog(
    _In_                             PDRIVER_OBJECT DriverObject,
    _In_                             UCHAR MajorFunctionCode,
    _In_                             ULONG UniqueErrorValue,
    _In_                             NTSTATUS FinalStatus,
    _In_                             NTSTATUS ErrorCode,
    _In_                             ULONG LengthOfInsert1,
    _In_reads_bytes_opt_(LengthOfInsert1) PWCHAR Insert1,
    _In_                             ULONG LengthOfInsert2,
    _In_reads_bytes_opt_(LengthOfInsert2) PWCHAR Insert2
    )
/*++

A generic log event entry routine, adapted from the official windows driver samples of Microsoft,
https://github.com/Microsoft/Windows-driver-samples/tree/main/serial/serial.
Some more sanity checks have been added, and complexity reduced.

Routine Description:

    This routine allocates an error log entry, copies the supplied data
    to it, and requests that it be written to the error log file.
    Writing to eventlog must be considered optional and guarantees no success!

Arguments:

    DriverObject - A pointer to the driver object for the device.

    MajorFunctionCode - If there is an error associated with the request,
    this is the major function code of that request. Setting this value is optional.

    RetryCount - optional. Indicates the number of times the driver has retried
    the operation and encountered this error. Use zero to indicate the driver
    attempted the operation once, or add one for each retry beyond the initial attempt.

    UniqueErrorValue - A driver-specific value that indicates where the error was
    detected in the driver. Setting this value is optional.

    FinalStatus - Specifies the NTSTATUS value to be returned for the operation
    that triggered the error. Setting this value is optional.

    ErrorCode - Specifies the type of error. The Event Viewer uses the error
    code to determine which string to display as the Description value for the
    error. ErrorCode is a system-defined or driver-defined constant.
    (Details: the Event Viewer takes the string template for the error supplied
    in the driver's message catalog, replaces "%1" in the template with the name
    of the driver's device object, and replaces "%2" through "%n" with the
    insertion strings supplied with the error log entry.)

    LengthOfInsert1 - The length in bytes (including the terminating NULL)
                      of the first insertion string.

    Insert1 - The first optional insertion string.

    LengthOfInsert2 - The length in bytes (including the terminating NULL)
                      of the second insertion string.  NOTE, there must
                      be a first insertion string for their to be
                      a second insertion string.

    Insert2 - The second optional insertion string.

Return Value:

    None.

--*/

{
   PIO_ERROR_LOG_PACKET errorLogEntry;

   PVOID objectToUse = DriverObject;
   SHORT noDumpToAllocate = 0;
   ULONG totalInsertLength = 0;
   UCHAR entrySize = 0;
   PUCHAR ptrToFirstInsert;
   PUCHAR ptrToSecondInsert;

   PAGED_CODE();

    if (Insert1 == NULL)
    {
      LengthOfInsert1 = 0;
    }

    if (Insert2 == NULL)
    {
      LengthOfInsert2 = 0;
    }

    totalInsertLength = LengthOfInsert1 + LengthOfInsert2;

    if (totalInsertLength > ERROR_LOG_MAXIMUM_SIZE)
    {
        DbgPrint("Skippable warning: too lengthy string for event entry. It will be cut off.\n");
    }

    entrySize = (UCHAR) (sizeof(IO_ERROR_LOG_PACKET) + noDumpToAllocate +  totalInsertLength);

    errorLogEntry = IoAllocateErrorLogEntry(objectToUse,entrySize);

    if (errorLogEntry == NULL )
    {
        DbgPrint("Error: Allocating an even entry failed.\n");
        return;
    }

    // (I don't trust Windows to zero out correctly.)
    memset(errorLogEntry,0,sizeof(entrySize));

    errorLogEntry->ErrorCode = ErrorCode;
    errorLogEntry->MajorFunctionCode = MajorFunctionCode;
    errorLogEntry->UniqueErrorValue = UniqueErrorValue;
    errorLogEntry->FinalStatus = FinalStatus;
    errorLogEntry->DumpDataSize = noDumpToAllocate;

    ptrToFirstInsert = (PUCHAR) &errorLogEntry->DumpData[0];

    ptrToSecondInsert = ptrToFirstInsert + LengthOfInsert1;

    if (LengthOfInsert1)
    {

        errorLogEntry->NumberOfStrings = 1;
        errorLogEntry->StringOffset = (USHORT)(ptrToFirstInsert - (PUCHAR)errorLogEntry);

        RtlCopyMemory(ptrToFirstInsert, Insert1, LengthOfInsert1);

        if (LengthOfInsert2)
        {
            errorLogEntry->NumberOfStrings = 2;

            RtlCopyMemory(ptrToSecondInsert, Insert2, LengthOfInsert2);
        }

    }

    IoWriteErrorLogEntry(errorLogEntry);

    return;
}


/*
This routine will setup the registry eventlog entries
in order to allow eventlog writing. It supports loading a temporary
random-named kernel binary blob loaded from temp folder (as Scudettes usermode program does).

Argument 1: "the Registry String" provided in DriverEntry.
Returns: STATUS_SUCCESS, if able to setup the driver for eventlog writing.

*/

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS setupEventLogging(_In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG i = 0;
    HANDLE driverKey = NULL;
    HANDLE eventLogKey = NULL;
    HANDLE parentEventLogKey = NULL;

    UNICODE_STRING driver_imagepath;
    UNICODE_STRING PathToEventlog;
    UNICODE_STRING typesSupported;
    UNICODE_STRING eventMessageFile;

    ULONG requiredSize = 0;
    OBJECT_ATTRIBUTES objAttr1 = {0};
    OBJECT_ATTRIBUTES objAttr2 = {0};
    ULONG objAttrFlags = OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE;
    ULONG typesSupportedValue = 7;
    PWCHAR serviceNamePtr = NULL;
    PWCHAR imagepathPtr = NULL;
    WCHAR messageFileBuffer[DEFAULT_SIZE_STR] = {0};
    char pathToDriver[DEFAULT_SIZE_STR] = {0};
    PKEY_VALUE_PARTIAL_INFORMATION imagepathBuffer = (PKEY_VALUE_PARTIAL_INFORMATION) pathToDriver;

    PAGED_CODE();

    // Find the currently used service name and (absolute) path to driver image.

    // The RegistryString offers many things, among them the current (possibly random-generated) servicename of this driver.
    // The servicename is just cut off from the end of the RegistryString. The RegistryString is a safe source.
    for (i=((RegistryPath->Length>>1)-1);i>0;i--)
    {
        if (RegistryPath->Buffer[i] == L'\\')
        {
            serviceNamePtr = &RegistryPath->Buffer[i+1];
            break;
        }
    }

    InitializeObjectAttributes(&objAttr1, RegistryPath, objAttrFlags, NULL, NULL);

    // open self key
    status = ZwOpenKeyEx(&driverKey, KEY_READ, &objAttr1, 0);

    if ( status != STATUS_SUCCESS )
    {
        DbgPrint("Error 0x%x: failed reading own driver key.\n", status);
        goto endOfSetupEventLog;
    }

    RtlInitUnicodeString(&driver_imagepath, L"ImagePath");

    // read image path
    status = ZwQueryValueKey(driverKey, &driver_imagepath, KeyValuePartialInformation, imagepathBuffer , sizeof(pathToDriver), &requiredSize);

    if ( status != STATUS_SUCCESS )
    {
        DbgPrint("Error 0x%x: failed reading driver path. Required size: 0x%x.\n", status, requiredSize);
        goto endOfSetupEventLog;
    }


    // Eventlog entry: create key and values.

    RtlInitUnicodeString(&PathToEventlog, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\EventLog\\System"); // this is a fixed string.

    // Check for previous setup.
    RtlZeroMemory(eventLogKeyEntry.Buffer,DEFAULT_SIZE_STR);
    status = RtlUnicodeStringPrintf(&eventLogKeyEntry, L"%wZ\\%ws", &PathToEventlog, serviceNamePtr);

    InitializeObjectAttributes(&objAttr2, &eventLogKeyEntry, objAttrFlags, NULL, NULL);

    status = ZwOpenKeyEx(&eventLogKey, KEY_READ, &objAttr2, 0); // Check for previous setup.

    if (status == STATUS_OBJECT_NAME_NOT_FOUND) // <= no entries in registry. Need to create all the key values.
    {
        eventLogKey = NULL; // No handle to close, it didn't exist. Also, this line isn't strictly necessary, it's more for clarification purpose.
        memset(&objAttr2,0,sizeof(objAttr2));
        InitializeObjectAttributes(&objAttr2, &PathToEventlog, objAttrFlags, NULL, NULL);

        status = ZwOpenKeyEx(&parentEventLogKey, KEY_ALL_ACCESS, &objAttr2, 0); // open parent key, to create a new key.

        if (status != STATUS_SUCCESS)
        {
            DbgPrint("Error 0x%x: failed to open for write: %wZ.\n", status, &PathToEventlog);
            goto endOfSetupEventLog;
        }

        memset(&objAttr2,0,sizeof(objAttr2));
        InitializeObjectAttributes(&objAttr2, &eventLogKeyEntry, objAttrFlags, NULL, NULL);

        status = ZwCreateKey(&eventLogKey, KEY_WRITE, &objAttr2, 0, NULL, 0, NULL );

        if (status != STATUS_SUCCESS)
        {
            DbgPrint("Error 0x%x: failed to create the pmem EventLog registry key: %wZ.\n", status, &eventLogKeyEntry);
            goto endOfSetupEventLog;
        }

        // Set TypesSupported to 7.

        RtlInitUnicodeString(&typesSupported, L"TypesSupported");

        status = ZwSetValueKey(eventLogKey, &typesSupported, 0, REG_DWORD,
                                &typesSupportedValue, sizeof(ULONG));

        if (status != STATUS_SUCCESS)
        {
            DbgPrint("Error 0x%x: failed to create TypesSupported value key.\n", status);
            goto endOfSetupEventLog;
        }

        // EventMessageFile, with path to IoLogMsg.dll and the binary containing the custom event message file.

        RtlInitUnicodeString(&eventMessageFile, L"EventMessageFile");

        imagepathPtr = (PWCHAR) &(imagepathBuffer->Data[0]);

        // Cut "\??\" -- a path beginning with "\??\" in the messageFileBuffer will not be understood!
        if ((imagepathPtr[1] == L'?') && (imagepathPtr[2] == L'?') && (imagepathPtr[3] == L'\\'))
        {
            imagepathPtr = &imagepathPtr[4];
        }

        RtlStringCchPrintfW(messageFileBuffer, DEFAULT_SIZE_STR, L"%s;%s", L"%SystemRoot%\\System32\\IoLogMsg.dll", imagepathPtr);

        DbgPrint("ressources string: %ws\n",messageFileBuffer);

        status = ZwSetValueKey( eventLogKey, &eventMessageFile, 0, REG_EXPAND_SZ,
                                (PVOID) messageFileBuffer, (ULONG) (2*(wcslen(messageFileBuffer)+1)) );
                                // (wcslen(messageFileBuffer)+1)*2: it's a wide char string, but ZwSetValueKey wants the size in *bytes*.

        if (status != STATUS_SUCCESS)
        {
            DbgPrint("Error 0x%x: failed to create EventMessageFile value key.\n", status);
            goto endOfSetupEventLog;
        }
    } // end of STATUS_OBJECT_NAME_NOT_FOUND / no entries in registry before.
    else
    {
        status = STATUS_SUCCESS;  // this is ok, too: there is already an entry which does not need to be recreated.
    }

endOfSetupEventLog:

    if (driverKey) ZwClose(driverKey);
    if (eventLogKey) ZwClose(eventLogKey);
    if (parentEventLogKey) ZwClose(parentEventLogKey);

    return status;
}
